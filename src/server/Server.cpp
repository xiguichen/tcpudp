#include "Server.h"
#include "ServerConfiguration.h"
#include "Peer.h"
#include "Socket.h"
#include "TcpConnection.h"
#include "TcpVirtualChannel.h"
#include "VcManager.h"
#include "VcProtocol.h"
#include "VirtualChannel.h"
#include "VirtualChannelFactory.h"
#include <Log.h>
#include <format>
#include <thread>
#include <cstring>

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
        // SocketSetReceiveBufferSize(clientSocket, 1024 * 1024);
        // SocketSetSendBufferSize(clientSocket, 1024 * 1024);

        // Log client connection
        char clientIP[INET_ADDRSTRLEN+1];
        memset(clientIP, 0, sizeof(clientIP));
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        log_info(std::string(clientIP));
        log_info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

        // Add peer to peer manager
        PeerManager::AddPeer(clientIP);

        // Get the peer and add the socket
        Peer *peer = PeerManager::GetPeerByIp(clientIP);
        if (peer)
        {
            peer->AddSocket(clientSocket);
        }
        else
        {
            log_error(std::format("Failed to find peer for IP {}", clientIP));
            SocketClose(clientSocket);
        }

        log_info(std::format("Added socket to peer {}. Total sockets: {}", clientIP, peer->GetSocketCount()));

        // check if we have enough sockets for this peer to create the virtual channel
        if (peer->GetSocketCount() == VC_TCP_CONNECTIONS)
        {

            // Create the virtual channel
            std::vector<SocketFd> fds = peer->GetSockets();
            VirtualChannelSp vc = VirtualChannelFactory::create(fds);

            // Add the virtual channel to the manager
            VcManager::getInstance().Add(clientIP, vc);
            log_info(std::format("Created virtual channel for peer {}", clientIP));

            // Create a UDP socket for this peer
            SocketFd udpSocket = SocketCreate(AF_INET, SOCK_DGRAM, 0);
            if (udpSocket == -1)
            {
                log_error("Failed to create UDP socket");
                break;
            }

            // Create a socket address for the UDP socket to send data to
            // It's something like 127.0.0.1: port
            struct sockaddr_in udpAddr{};
            udpAddr.sin_family = AF_INET;
            // sin_addr should be local host
            udpAddr.sin_addr.s_addr = INADDR_ANY;
            udpAddr.sin_port = htons(ServerConfiguration::getInstance()->getPortNumber());

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
                    log_info(std::format("Sent {} bytes to UDP socket", sentBytes));
                }
            });

            ((TcpVirtualChannel *)vc.get())->setDisconnectCallback([clientIP, &peer, vc](TcpConnectionSp connection) {
                peer->RemoveAllSockets();
                vc->close();
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
                        log_info(std::format("Received {} bytes from UDP socket", receivedBytes));
                        vc->send(buffer, receivedBytes);
                    }
                }
                SocketClose(udpSocket);
            }).detach();

            // Now we can open the virtual channel
            vc->open();
        }
    }
}
