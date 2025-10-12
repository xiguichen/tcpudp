#include "Client.h"
#include "Log.h"
#include "Configuration.h"
#include "VcProtocol.h"
#include "VirtualChannelFactory.h"
#include <thread>
#include <format>

bool Client::PrepareVC()
{
    log_info("Preparing virtual channel...");

    for (int i = 0; i < VC_TCP_CONNECTIONS; i++)
    {
        // Create TCP socket here
        SocketFd tcpSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
        if (tcpSocket == -1)
        {
            log_error("Failed to create TCP socket");
            return false;
        }

        // Connect to server
        struct sockaddr_in serverAddr{};
        auto ip = Configuration::getInstance()->getSocketAddress();
        auto port = Configuration::getInstance()->getPortNumber();

        log_info(std::format("Connecting to server at {}:{}", ip, port));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
        serverAddr.sin_port = htons(port);

        if (SocketConnect(tcpSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            log_error("Failed to connect to server");
            SocketClose(tcpSocket);
            return false;
        }

        log_info("Connected to server successfully.");
        tcpSockets.push_back(tcpSocket);
    }

    // Create virtual channel with the connected sockets
    log_info("Creating virtual channel with connected TCP sockets...");
    vc = VirtualChannelFactory::create(tcpSockets);
    if (!vc)
    {
        log_error("Failed to create virtual channel");
        return false;
    }

    // Set up receive callback
    vc->setReceiveCallback([this](const char *data, size_t size) {
        log_info(std::format("Virtual channel received {} bytes of data", size));

        // send data to UDP socket
        SendUdpData(udpSocket, data, size, 0, (struct sockaddr *)&udpAddr, sizeof(udpAddr));
    });

    vc->open();

    return true;
}

bool Client::PrepareUdpSocket()
{
    // Logic to create UDP socket
    log_info("Creating UDP socket...");

    // Create UDP socket here
    SocketFd udpSocket = SocketCreate(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == -1)
    {
        log_error("Failed to create UDP socket");
        return false;
    }

    // Prepare address structure for sending data
    struct sockaddr_in udpAddr{};
    auto ip = Configuration::getInstance()->getSocketAddress();
    auto port = Configuration::getInstance()->getPortNumber();

    log_info(std::format("UDP target address: {}:{}", ip, port));
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    udpAddr.sin_port = htons(port);

    // bind the socket to the address
    int bindResult = SocketBind(udpSocket, (struct sockaddr *)&udpAddr, sizeof(udpAddr));
    if (bindResult < 0)
    {
        log_error("Failed to bind UDP socket");
        SocketClose(udpSocket);
        return false;
    }

    log_info("UDP socket created and bound successfully.");

    // Start receiving data in a loop

    while (true)
    {
        char buffer[1500];
        struct sockaddr_in srcAddr{};
        socklen_t srcAddrLen = sizeof(srcAddr);
        ssize_t receivedBytes =
            RecvUdpData(udpSocket, buffer, sizeof(buffer), 0, (struct sockaddr *)&srcAddr, &srcAddrLen);
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
            udpAddr = srcAddr; // Update the address to the source address
        }
    }

    return true;
}

void Client::Stop()
{
    log_info("Client stopping...");
    running = false;
}

void Client::Start()
{
    running = true;
    // Client logic to connect to server would go here
    // For example, create socket, connect, etc.
    log_info("Client started.");

    if (!PrepareVC())
    {
        log_error("Failed to prepare virtual channel");
        return;
    }

    if (!PrepareUdpSocket())
    {
        log_error("Failed to prepare UDP socket");
        return;
    }

    while (running)
    {
        // Main loop
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

