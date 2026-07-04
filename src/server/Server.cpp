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

bool Server::Listen()
{

    serverSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        log_error("Failed to create socket");
        return false;
    }

    auto port = ServerConfiguration::getInstance()->getPortNumber();
    log_info(std::format("Server listening on {}", port));

    // Bind the socket and start listening
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    SocketReuseAddress(serverSocket);
    if (SocketBind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        log_error("Failed to bind socket");
        SocketClose(serverSocket);
        return false;
    }

    if (SocketListen(serverSocket, SOMAXCONN) < 0)
    {
        log_error("Failed to listen on socket");
        SocketClose(serverSocket);
        return false;
    }

    log_info("Server is now listening for connections");
    return true;
}

void Server::AcceptConnections()
{
    log_info("Waiting to accept a new connection...");
    while (running)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        // Accept connection blocking
        SocketFd clientSocket = SocketAccept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
        if (clientSocket < 0)
        {
            log_error("Failed to accept connection");
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

        ssize_t received = RecvTcpDataWithSize(clientSocket, bindBuffer.data(), bindBuffer.size(), 0, sizeof(MsgBind));
        if (received <= 0)
        {
            log_error("Failed to receive client ID from client");
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
                    // close() synchronously disconnects all connections, which runs the VC
                    // disconnect callback: removes the VC from VcManager, removes the peer,
                    // closes the old UDP socket, and clears clientConnSlots. It is idempotent,
                    // so the VC's later destruction won't re-fire it.
                    existingVc->close();
                    if (VcManager::getInstance().Exists(clientId))
                        VcManager::getInstance().Remove(clientId);
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

                tcpVc->replaceConnection(targetSlot, clientSocket);

                // Keep clientConnSlots consistent and bounded: drop any entries pointing
                // at this slot, then register the id the client just sent for it.
                if (bindMsg.connectionId != 0)
                {
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
            socketToConnId[clientSocket] = bindMsg.connectionId;
        }

        // New client — register peer and accumulate sockets until we have enough for a full VC.
        PeerManager::AddPeer(clientId);

        // Get the peer and add the socket
        Peer *peer = PeerManager::GetPeerById(clientId);
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

            // Create the virtual channel
            std::vector<SocketFd> fds = peer->GetSockets();
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
                break;
            }

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
                break;
            }

            // Create a socket address for the UDP socket to send data to
            struct sockaddr_in udpAddr{};
            udpAddr.sin_family = AF_INET;
            udpAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
            udpAddr.sin_port = htons(ServerConfiguration::getInstance()->getUdpTargetPort());

            peer->SetUdpSocket(udpSocket);
            peer->SetUdpAddress(udpAddr);

            vc->setReceiveCallback([udpSocket, udpAddr](const char *data, size_t size) {
                // Send data to the UDP socket
                ssize_t sentBytes = sendto(udpSocket, data, size, 0, (struct sockaddr *)&udpAddr, sizeof(udpAddr));
                if (sentBytes < 0)
                {
                    log_error("Failed to send data to UDP socket");
                }
                else
                {
                    log_debug(std::format("Sent {} bytes to UDP socket", sentBytes));
                }
            });

            ((TcpVirtualChannel *)vc.get())->setDisconnectCallback([this, clientId, udpSocket]() {
                // Remove VC from manager only if it exists to avoid double-removal
                if (VcManager::getInstance().Exists(clientId)) {
                    VcManager::getInstance().Remove(clientId);
                }
                // Do NOT call RemoveAllSockets here: TcpVirtualChannel::disconnectCB already
                // closed the socket FDs via TcpConnection::disconnect(). Calling SocketClose
                // again on the same FD integers would double-close them and risk closing
                // a newly accepted socket that got the same FD number from the OS.
                // Just remove the peer so the next AddPeer call gets a clean slate.
                PeerManager::RemovePeer(clientId);
                // Close the UDP socket to unblock recv() in the UDP receive thread,
                // allowing it to exit. The thread must not close it again after this.
                SocketClose(udpSocket);
                // Clear the connectionId→slot map — the VC is gone.
                clientConnSlots.erase(clientId);
            });

            // start a new thread to receive data from the UDP socket
            std::thread([udpSocket, vc]() {
                char buffer[VC_MAX_DATA_PAYLOAD_SIZE + VC_MIN_DATA_PACKET_SIZE];
                while (true)
                {
                    ssize_t receivedBytes = recv(udpSocket, buffer, sizeof(buffer), 0);
                    if (receivedBytes < 0)
                    {
                        log_error("Failed to receive data from UDP socket");
                        break;
                    }
                    else if (receivedBytes == 0)
                    {
                        log_info("UDP socket closed");
                        break;
                    }
                    else
                    {
                        log_debug(std::format("Received {} bytes from UDP socket", receivedBytes));
                        vc->send(buffer, receivedBytes);
                    }
                }
                // Socket is closed by the disconnect callback; do not close it here.
            }).detach();

            // Now we can open the virtual channel
            vc->open();
        }
    }
}
