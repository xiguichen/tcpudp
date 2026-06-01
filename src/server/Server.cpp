#include "Server.h"
#include "Peer.h"
#include "Protocol.h"
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

        // Log client connection
        char clientIP[INET_ADDRSTRLEN + 1];
        memset(clientIP, 0, sizeof(clientIP));
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        log_info(std::string(clientIP));
        log_info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

        // Receive client ID from the client immediately after connection
        MsgBind bindMsg;
        std::vector<char> bindBuffer(sizeof(MsgBind));

        ssize_t received = RecvTcpData(clientSocket, bindBuffer.data(), bindBuffer.size(), 0);
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
        // Use a single Get() call to avoid a TOCTOU race between Exists() and Get().
        auto existingVc = VcManager::getInstance().Get(clientId);
        if (existingVc)
        {
            auto *tcpVc = dynamic_cast<TcpVirtualChannel *>(existingVc.get());
            if (tcpVc)
            {
                auto deadSlots = tcpVc->getDeadSlots();
                if (deadSlots.empty())
                {
                    // All 32 slots are alive — this is a stray connection from a
                    // stale watchdog or an earlier PrepareVC.  No need to disrupt
                    // the healthy VC; just close the excess socket.  The sender's
                    // watchdog will retry, and by then any genuine dead slots will
                    // have been detected via select() + recv() = 0.
                    //
                    // However, guard against infinite loops: if the old VC is a
                    // zombie (the client disconnected but the server hasn't detected
                    // it yet), the watchdog's ReconnectSingleSlot sends MsgBind for
                    // a slot it thinks is dead, but the server sees all slots alive.
                    // The socket is rejected, the watchdog retries with a new socket,
                    // and we get an infinite flood of excess sockets.
                    //
                    // After EXCESS_LIMIT excess sockets within EXCESS_WINDOW from the
                    // same client, proactively tear down the old VC so the new one
                    // can be established. This resolves the race between the watchdog
                    // sending MsgBind and ReconnectVC tearing down the old VC.
                    auto now = std::chrono::steady_clock::now();
                    auto &tracker = excessByClient[clientId];
                    if (now - tracker.firstSeen > EXCESS_WINDOW)
                    {
                        tracker.count = 0;
                        tracker.firstSeen = now;
                    }
                    tracker.count++;

                    if (tracker.count >= EXCESS_LIMIT)
                    {
                        log_warnning(std::format(
                            "[Server] Client {} sent {} excess sockets in {}s — "
                            "old VC is likely a zombie, removing it",
                            clientId, tracker.count,
                            std::chrono::duration_cast<std::chrono::seconds>(EXCESS_WINDOW).count()));
                        excessByClient.erase(clientId);
                        // Remove the old VC — the next connection from this client
                        // will start a fresh peer accumulation.
                        if (VcManager::getInstance().Exists(clientId)) {
                            VcManager::getInstance().Remove(clientId);
                        }
                        PeerManager::RemovePeer(clientId);
                        // Close this socket too; PrepareVC will retry.
                        SocketClose(clientSocket);
                        continue;
                    }

                    log_info(std::format("[Server] Excess socket ({}/{}) for clientId {} with no dead slots — "
                                         "closing stray connection, VC continues",
                                         tracker.count, EXCESS_LIMIT, clientId));
                    SocketClose(clientSocket);
                    continue;
                }
                else
                {
                    int targetSlot = -1;
                    if (bindMsg.slotIndex >= 0 &&
                        std::find(deadSlots.begin(), deadSlots.end(), bindMsg.slotIndex) != deadSlots.end())
                    {
                        targetSlot = bindMsg.slotIndex;
                    }
                    else
                    {
                        targetSlot = deadSlots[0];
                    }
                    tcpVc->replaceConnection(targetSlot, clientSocket);
                    log_info(std::format("[Server] Replaced slot {} for clientId {}", targetSlot, clientId));
                    continue;
                }
            }
            else
            {
                log_error(std::format("[Server] Unexpected VC type for clientId {} — closing socket", clientId));
                SocketClose(clientSocket);
                continue;
            }
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

            // Add the virtual channel to the manager using client ID
            VcManager::getInstance().Add(clientId, vc);
            log_info(std::format("Created virtual channel for peer with client ID {}", clientId));

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
                // Clear the excess tracker — the VC is legitimately gone now.
                excessByClient.erase(clientId);
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
