#include "Server.h"
#include "Peer.h"
#include "Protocol.h"
#include "ReplaceSlotPolicy.h"
#include "ServerConfiguration.h"
#include "Socket.h"
#include "TcpConnection.h"
#include "TcpVirtualChannel.h"
#include "VcManager.h"
#include "VcProtocol.h"
#include "VirtualChannel.h"
#include "VirtualChannelFactory.h"
#include <Log.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <thread>

namespace
{
// Owns the per-peer UDP socket and serialises send/recv/close. Without this,
// the disconnect callback could close() the FD while the UDP receive thread was
// blocked in recv() on it. On macOS close() does not wake a blocked recv(), so
// the thread (and the VC it captures) leaked forever — and closing from another
// thread also risks the FD being recycled mid-recv. A short poll timeout makes
// Receive() observe Close() promptly on every platform.
class UdpSocketHandle
{
  public:
    explicit UdpSocketHandle(SocketFd socket) : fd(socket) {}

    void Send(const char *data, size_t size, const struct sockaddr_in &addr)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (fd == -1)
            return;
        ssize_t sent = sendto(fd, data, size, 0, (const struct sockaddr *)&addr, sizeof(addr));
        if (sent < 0)
        {
            // The socket is non-blocking so Close() never waits behind a full
            // send buffer. A full buffer is normal backpressure: drop, don't spam.
#ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                log_error("Failed to send data to UDP socket");
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                log_error("Failed to send data to UDP socket");
#endif
        }
        else
            log_debug(std::format("Sent {} bytes to UDP socket", sent));
    }

    // Returns false when the socket has been closed (thread should exit).
    // received == 0 means "nothing this cycle" (timeout/EINTR).
    bool Receive(char *buffer, size_t size, ssize_t &received)
    {
        received = 0;

        // Do NOT hold mtx across the readiness poll. The VC receive callback
        // calls Send() on this same handle; holding the mutex for the full
        // 250 ms poll timeout blocks that callback (and therefore the VC
        // reorder thread) long enough for the per-connection receive queues to
        // overflow and drop packets under load.
        SocketFd polledFd;
        uint64_t polledGeneration;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (fd == -1)
                return false;
            polledFd = fd;
            polledGeneration = generation;
        }

        if (!IsSocketReadable(polledFd, 250))
            return true;

        std::lock_guard<std::mutex> lock(mtx);
        // Close() may have run while we were polling; revalidate before touching
        // the descriptor (or a recycled fd number).
        if (fd != polledFd || generation != polledGeneration)
            return fd != -1;
        ssize_t n = recv(fd, buffer, size, 0);
        if (n < 0)
        {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAEWOULDBLOCK)
                return true;
#else
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
#endif
            log_error("Failed to receive data from UDP socket");
            return false;
        }
        received = n;
        return true;
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (fd == -1)
            return;
        SocketShutdown(fd, SHUT_RDWR);
        SocketClose(fd);
        fd = -1;
        ++generation;
    }

  private:
    std::mutex mtx;
    SocketFd fd;
    uint64_t generation = 0;
};
} // namespace

void Server::stop()
{
    log_info("Stopping server...");
    requestStop();
    SocketFd fd = serverSocket.exchange(-1);
    if (fd != -1)
    {
        SocketShutdown(fd, SHUT_RDWR);
        SocketClose(fd);
    }

    // Stop all virtual channels (joins their IO/send/reorder threads and closes
    // the peer UDP sockets) before the process tears down globals.
    VcManager::getInstance().CloseAll();

    SocketClearnup();
}

bool Server::Listen()
{

    SocketFd listenSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1)
    {
        log_error("Failed to create socket");
        return false;
    }
    serverSocket.store(listenSocket);

    auto port = ServerConfiguration::getInstance()->getPortNumber();
    log_info(std::format("Server listening on {}", port));

    // Bind the socket and start listening
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    SocketReuseAddress(listenSocket);
    if (SocketBind(listenSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        log_error("Failed to bind socket");
        SocketClose(listenSocket);
        serverSocket.store(-1);
        return false;
    }

    if (SocketListen(listenSocket, SOMAXCONN) < 0)
    {
        log_error("Failed to listen on socket");
        SocketClose(listenSocket);
        serverSocket.store(-1);
        return false;
    }

    log_info("Server is now listening for connections");
    return true;
}

void Server::AcceptConnections()
{
    log_info("Waiting to accept a new connection...");
    while (running.load(std::memory_order_acquire))
    {
        // Poll with a timeout instead of blocking in accept(): on macOS (and BSD)
        // close() from another thread does not reliably wake a blocked accept(),
        // so a blocking accept would make graceful shutdown hang forever. It also
        // lets the loop observe running=false promptly.
        SocketFd listenFd = serverSocket.load(std::memory_order_acquire);
        if (listenFd == -1)
            break;
        if (!IsSocketReadable(listenFd, 250))
            continue;
        // Stop() may have closed the listen fd while we were polling (it no
        // longer shutdowns it from the signal path). Don't accept on a stale
        // descriptor number that the OS may have reused.
        if (!running.load(std::memory_order_acquire))
            break;

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        SocketFd clientSocket = SocketAccept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);
        if (clientSocket < 0)
        {
            // EINTR/EAGAIN are expected (signal or a spurious readiness wake);
            // anything else (EMFILE/ENFILE) must not spin the accept loop at 100% CPU.
            bool transient = false;
#ifdef _WIN32
            int err = WSAGetLastError();
            transient = (err == WSAEINTR || err == WSAEWOULDBLOCK || err == WSAECONNABORTED);
#else
            transient = (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED);
#endif
            if (!transient)
            {
                log_error("Failed to accept connection");
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }

        SocketSetTcpNoDelay(clientSocket, true);
        SocketSetReceiveBufferSize(clientSocket, 512 * 1024);
        SocketSetSendBufferSize(clientSocket, 128 * 1024);
        // Proactively detect half-open client connections so the server reaps a dead
        // socket via keepalive probes instead of leaving it lingering until recv fails.
        // Timing is centralized in Socket.h; see the constants for the macOS tuning rationale.
        SocketSetKeepAlive(clientSocket, true, VC_KEEPALIVE_IDLE_SEC, VC_KEEPALIVE_INTERVAL_SEC,
                           VC_KEEPALIVE_PROBE_COUNT);

        // Log client connection
        char clientIP[INET_ADDRSTRLEN + 1];
        memset(clientIP, 0, sizeof(clientIP));
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        log_info(std::string(clientIP));
        log_info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

        // Receive client ID from the client immediately after connection.
        // Use RecvTcpDataWithSize to guarantee we read the full MsgBind — TCP is a
        // stream protocol and recv() may return fewer bytes than requested if the
        // message is split across segments.  Partial reads leave leftover bytes in
        // the kernel buffer that the VC IO thread later misinterprets as VC packet
        // headers (producing "Unknown packet type: N" errors where N is slotIndex).
        MsgBind bindMsg;
        std::vector<char> bindBuffer(sizeof(MsgBind));

        // Bound the handshake read. The accept loop is single-threaded, so
        // without a timeout one client that connects and never sends its MsgBind
        // (or a half-open socket) stalls every other client indefinitely — they
        // then time out and reconnect, amplifying the outage. The deadline is
        // for the whole fixed-size message: a peer trickling one byte per second
        // must not be able to hold the accept loop for minute-scale periods.
        static constexpr int HANDSHAKE_TIMEOUT_MS = 5000;
        SocketSetReceiveTimeout(clientSocket, HANDSHAKE_TIMEOUT_MS);
        ssize_t received = RecvTcpDataWithSize(clientSocket, bindBuffer.data(), bindBuffer.size(), 0, sizeof(MsgBind),
                                               HANDSHAKE_TIMEOUT_MS);
        SocketSetReceiveTimeout(clientSocket, 0); // restore blocking for the VC path

        if (received != static_cast<ssize_t>(sizeof(MsgBind)))
        {
            // A short read means the peer died mid-handshake. Treating it as
            // success would parse zero/partial bytes as a client ID and bind the
            // socket to the wrong peer.
            log_error(std::format("Failed to receive full client ID from client ({} of {} bytes)",
                                  received, sizeof(MsgBind)));
            SocketClose(clientSocket);
            continue;
        }

        if (!UvtUtils::ExtractMsgBind(bindBuffer, bindMsg))
        {
            log_error("Failed to extract client ID from received data");
            SocketClose(clientSocket);
            continue;
        }

        uint32_t clientId = bindMsg.clientId;
        log_info(std::format("Received client ID {} from client {}", clientId, clientIP));

        // If a VC already exists for this clientId, the incoming socket is a replacement
        // for a dead slot in the existing channel — hot-swap it and skip peer accumulation.
        auto existingVc = VcManager::getInstance().Get(clientId);
        if (existingVc)
        {
            auto *tcpVc = dynamic_cast<TcpVirtualChannel *>(existingVc.get());
            if (tcpVc)
            {
                // slotIndex == -1 is an INITIAL connection: the client is (re)establishing
                // its whole channel, not replacing a single slot. If we still hold a VC for
                // this client it is stale — the client already tore its side down (e.g. after
                // a full outage) and is reconnecting from scratch. Tear our VC down and fall
                // through to fresh accumulation. Without this, the client's 32 fresh sockets
                // are all rejected as "excess" (no connId match, no slotIndex, no dead slot),
                // the client believes it reconnected, the sockets are closed, and it loops.
                if (bindMsg.slotIndex < 0)
                {
                    log_info(std::format("[Server] Initial connect for clientId {} while a VC exists — "
                                         "tearing down stale VC and re-establishing",
                                         clientId));
                    // close() synchronously disconnects all connections and runs
                    // the VC teardown callback: closes the old UDP socket, removes
                    // the peer, clears clientConnSlots, and removes this VC from
                    // VcManager. It is idempotent, so the VC's later destruction
                    // won't re-fire it.
                    existingVc->close();
                    VcManager::getInstance().RemoveIf(clientId, existingVc);
                    // Intentionally fall through to the fresh-accumulation path below.
                }
                else
                {
                // If the client provided a connectionId, use it to force-close the
                // matching connection directly — no deadSlots check needed.
                // The client KNOWS this connection is dead (it detected the TCP
                // disconnection), but the server's IO thread may not have polled
                // the dead socket yet.  Trust the client's connectionId.
                // Resolve the slot from the connectionId map (fast path). The client
                // rotates its connectionId after each reconnect but only transmits the
                // PREVIOUS id, so this map goes stale by one step — hence the slotIndex
                // fallback in DecideReplaceSlot.
                int connIdSlot = -1;
                if (bindMsg.connectionId != 0)
                {
                    std::lock_guard<std::mutex> mapLock(mapsMutex);
                    auto clientIt = clientConnSlots.find(clientId);
                    if (clientIt != clientConnSlots.end())
                    {
                        auto slotIt = clientIt->second.find(bindMsg.connectionId);
                        if (slotIt != clientIt->second.end())
                            connIdSlot = slotIt->second;
                    }
                }

                auto deadSlots = tcpVc->getDeadSlots();
                int targetSlot = DecideReplaceSlot(connIdSlot, bindMsg.slotIndex, VC_TCP_CONNECTIONS, deadSlots);

                if (targetSlot < 0)
                {
                    log_info(std::format(
                        "[Server] Excess socket for clientId {} — no connectionId match, no valid slotIndex, "
                        "and no dead slot; closing stray connection",
                        clientId));
                    SocketClose(clientSocket);
                    continue;
                }

                if (!tcpVc->replaceConnection(targetSlot, clientSocket))
                {
                    // The VC started closing while this socket was in flight;
                    // replaceConnection() closed it. Do not register a mapping for
                    // a slot that was not actually replaced.
                    log_info(std::format("[Server] VC for clientId {} is closing; dropping slot {} replacement",
                                         clientId, targetSlot));
                    continue;
                }

                // Keep clientConnSlots consistent and bounded: drop any entries pointing
                // at this slot, then register the id the client just sent for it.
                if (bindMsg.connectionId != 0)
                {
                    std::lock_guard<std::mutex> mapLock(mapsMutex);
                    auto &slotMap = clientConnSlots[clientId];
                    for (auto it = slotMap.begin(); it != slotMap.end();)
                    {
                        if (it->second == targetSlot)
                            it = slotMap.erase(it);
                        else
                            ++it;
                    }
                    slotMap[bindMsg.connectionId] = targetSlot;
                }

                log_info(std::format(
                    "[Server] Replaced slot {} for clientId {} (connIdSlot={}, sentConnId={}, slotIndex={})",
                    targetSlot, clientId, connIdSlot, bindMsg.connectionId, (int)bindMsg.slotIndex));
                continue;
                } // end else (slotIndex >= 0: targeted per-slot replacement)
            }
            else
            {
                log_error(std::format("[Server] Unexpected VC type for clientId {} — closing socket", clientId));
                SocketClose(clientSocket);
                continue;
            }
        }

        // Record the client-assigned connectionId for this socket so that after VC
        // creation we can build the connectionId→slotIndex map.  This map allows
        // the watchdog to force-close a specific connection by ID during reconnect.
        if (bindMsg.connectionId != 0)
        {
            std::lock_guard<std::mutex> mapLock(mapsMutex);
            socketToConnId[clientSocket] = bindMsg.connectionId;
        }

        // New client — register peer and accumulate sockets until we have enough for a full VC.
        PeerManager::AddPeer(clientId);

        // Get the peer and add the socket. Keep a strong reference: a disconnect
        // callback on another thread may remove the peer from the registry while
        // we are still accumulating sockets for it.
        auto peer = PeerManager::GetPeerShared(clientId);
        if (peer)
        {
            peer->AddSocket(clientSocket);
        }
        else
        {
            log_error(std::format("Failed to find peer for client ID {}", clientId));
            SocketClose(clientSocket);
            continue;
        }

        log_info(
            std::format("Added socket to peer with client ID {}. Total sockets: {}", clientId, peer->GetSocketCount()));

        // check if we have enough sockets for this peer to create the virtual channel
        if (peer->GetSocketCount() >= VC_TCP_CONNECTIONS)
        {

            // Ownership moves to the VC. The peer must stay empty: otherwise a
            // later full/stale flush (or CloseAllSockets) would close fds the VC
            // already owns, potentially killing a reused fd number.
            std::vector<SocketFd> fds = peer->TakeSockets();
            VirtualChannelSp vc = VirtualChannelFactory::create(fds);

            // Match client-side reorder timeout for WAN links.
            ((TcpVirtualChannel *)vc.get())->setReorderTimeout(std::chrono::milliseconds(8000));

            // Add the virtual channel to the manager using client ID
            VcManager::getInstance().Add(clientId, vc);
            log_info(std::format("Created virtual channel for peer with client ID {}", clientId));

            // Build connectionId→slotIndex map for force-reconnect.
            // The order of fds matches the VC's internal connections vector, so
            // fds[i] corresponds to slot i.
            {
                std::lock_guard<std::mutex> mapLock(mapsMutex);
                auto &slotMap = clientConnSlots[clientId];
                for (size_t i = 0; i < fds.size(); i++)
                {
                    auto it = socketToConnId.find(fds[i]);
                    if (it != socketToConnId.end())
                    {
                        slotMap[it->second] = static_cast<int>(i);
                        socketToConnId.erase(it);
                    }
                }
            }

            // Create a UDP socket for this peer
            SocketFd udpSocket = SocketCreate(AF_INET, SOCK_DGRAM, 0);
            if (udpSocket == -1)
            {
                log_error("Failed to create UDP socket");
                VcManager::getInstance().Remove(clientId);
                PeerManager::RemovePeer(clientId);
                std::lock_guard<std::mutex> mapLock(mapsMutex);
                clientConnSlots.erase(clientId);
                continue;
            }

            // UdpSocketHandle::Send() holds its mutex across sendto(). Make the
            // socket non-blocking so a full send buffer cannot block Close() (and
            // therefore the reorder thread join) indefinitely.
            if (SetSocketNonBlocking(udpSocket) != 0)
            {
                log_error("Failed to set UDP socket non-blocking");
                SocketClose(udpSocket);
                VcManager::getInstance().Remove(clientId);
                PeerManager::RemovePeer(clientId);
                std::lock_guard<std::mutex> mapLock(mapsMutex);
                clientConnSlots.erase(clientId);
                continue;
            }

            // Large receive buffer so a burst from the UDP target is not dropped
            // by the kernel while the reorder thread is forwarding to the VC.
            SocketSetReceiveBufferSize(udpSocket, 4 * 1024 * 1024);

            // Bind the UDP socket to an ephemeral port so recv() works immediately.
            // Without bind, recv() on Windows returns WSAEINVAL on unbound sockets.
            struct sockaddr_in bindAddr{};
            bindAddr.sin_family = AF_INET;
            bindAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
            bindAddr.sin_port = 0; // Let OS assign a port
            if (SocketBind(udpSocket, (struct sockaddr *)&bindAddr, sizeof(bindAddr)) < 0)
            {
                log_error("Failed to bind UDP socket");
                SocketClose(udpSocket);
                VcManager::getInstance().Remove(clientId);
                PeerManager::RemovePeer(clientId);
                std::lock_guard<std::mutex> mapLock(mapsMutex);
                clientConnSlots.erase(clientId);
                continue;
            }

            // Create a socket address for the UDP socket to send data to
            struct sockaddr_in udpAddr{};
            udpAddr.sin_family = AF_INET;
            udpAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
            udpAddr.sin_port = htons(ServerConfiguration::getInstance()->getUdpTargetPort());

            // The UdpSocketHandle owns the UDP fd; do not also publish it on the
            // peer, or CloseAllSockets() could double-close the handle's fd.
            auto udpHandle = std::make_shared<UdpSocketHandle>(udpSocket);

            vc->setReceiveCallback([udpHandle, udpAddr](const char *data, size_t size) {
                udpHandle->Send(data, size, udpAddr);
            });

            std::weak_ptr<VirtualChannel> weakVc = vc;

            // Teardown runs for every close path (all-connections-dead, explicit
            // initial-connect replacement, and shutdown), not only when the VC
            // decides to notify a disconnect. Keep the peer/VC mapping until the
            // very end so the accept loop cannot start a new accumulation while
            // cleanup is still in progress.
            vc->setTeardownCallback([this, clientId, udpHandle, weakVc]() {
                auto self = weakVc.lock();
                auto current = VcManager::getInstance().Get(clientId);
                bool ownsClient = !current || current == self;

                // Always close this channel's UDP socket, even if a newer VC
                // has replaced it. This unblocks the UDP receive thread.
                udpHandle->Close();

                if (ownsClient)
                {
                    // Do NOT call RemoveAllSockets here: TcpVirtualChannel::close
                    // already closed the socket FDs via TcpConnection::disconnect().
                    // Calling SocketClose again on the same FD integers would
                    // double-close them and risk closing a newly accepted socket
                    // that got the same FD number from the OS.
                    PeerManager::RemovePeer(clientId);
                    {
                        std::lock_guard<std::mutex> mapLock(mapsMutex);
                        clientConnSlots.erase(clientId);
                    }
                    // Remove the registry entry last: until it is gone, a new
                    // initial connection blocks in close() instead of racing us.
                    if (self)
                        VcManager::getInstance().RemoveIf(clientId, self);
                }
            });

            vc->setDisconnectCallback([weakVc]() {
                // Removing the VC from VcManager drops the owner's reference, but
                // the IO/send thread callbacks each hold a shared_ptr to the VC.
                // Without close() the object (and its reorder thread, its queues
                // and the sent-data cache) leaked on every full disconnect.
                // close() joins the VC threads, so it must NOT run on this
                // callback's thread; a separate thread does the join while a
                // strong reference keeps the VC alive until teardown completes.
                if (auto vcToClose = weakVc.lock())
                {
                    std::thread([vcToClose]() { vcToClose->close(); }).detach();
                }
            });

            // start a new thread to receive data from the UDP socket
            std::thread([udpHandle, vc]() {
                char buffer[VC_MAX_DATA_PAYLOAD_SIZE + VC_MIN_DATA_PACKET_SIZE];
                while (true)
                {
                    ssize_t receivedBytes = 0;
                    if (!udpHandle->Receive(buffer, sizeof(buffer), receivedBytes))
                        break;
                    if (receivedBytes <= 0)
                        continue;
                    log_debug(std::format("Received {} bytes from UDP socket", receivedBytes));
                    vc->send(buffer, receivedBytes);
                }
                // Socket is closed by the disconnect callback; do not close it here.
            }).detach();

            // Now we can open the virtual channel
            vc->open();
        }
    }
}
