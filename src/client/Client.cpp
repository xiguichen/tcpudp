#include "Client.h"
#include "Log.h"
#include "ClientConfiguration.h"
#include "VcProtocol.h"
#include "VirtualChannelFactory.h"
#include "Protocol.h"
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
        auto ip = ClientConfiguration::getInstance()->getSocketAddress();
        auto port = ClientConfiguration::getInstance()->getPortNumber();

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

        SocketSetTcpNoDelay(tcpSocket, true);
        // SocketSetReceiveBufferSize(tcpSocket, 1024 * 1024);
        // SocketSetSendBufferSize(tcpSocket, 1024 * 1024);

        // Send client ID to server immediately after connecting
        MsgBind bindMsg;
        bindMsg.clientId = ClientConfiguration::getInstance()->getClientId();

        std::vector<char> bindBuffer;
        UvtUtils::AppendMsgBind(bindMsg, bindBuffer);

        if (SendTcpData(tcpSocket, bindBuffer.data(), bindBuffer.size(), 0) <= 0)
        {
            log_error("Failed to send client ID to server");
            SocketClose(tcpSocket);
            return false;
        }

        log_info(std::format("Sent client ID {} to server", bindMsg.clientId));

        log_info("Connected to server successfully.");
        tcpSockets.push_back(tcpSocket);

        // Add delay between connections to give server time to process
        if (i == 0) {
            log_info("Waiting 500ms before establishing second TCP connection...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
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
        log_debug(std::format("Virtual channel received {} bytes of data", size));

        auto remoteAddr = this->remoteUdpAddr;

        log_debug(std::format("Sending data to UDP address: {}:{}", inet_ntoa(remoteAddr.sin_addr), ntohs(remoteAddr.sin_port)));

        // send data to UDP socket
        SendUdpData(udpSocket, data, size, 0, (struct sockaddr *)&remoteAddr, sizeof(remoteUdpAddr));
    });

    // Setup disconnect callback
    // ReconnectVC calls vc->close() which joins all VC threads. Since this callback
    // is invoked synchronously from a VC read/write thread, running ReconnectVC on
    // that same thread would cause it to join itself — a guaranteed deadlock.
    // Spawning a detached thread lets the VC thread exit normally while reconnection
    // proceeds independently.
    vc->setDisconnectCallback([this]() {
        log_warnning("Virtual channel disconnected. Attempting reconnection...");
        std::thread([this]() {
            if (!this->ReconnectVC()) {
                log_error("Reconnection failed, stopping client");
                this->Stop();
            }
        }).detach();
    });

    vc->open();

    return true;
}

bool Client::PrepareUdpSocket()
{
    // Logic to create UDP socket
    log_info("Creating UDP socket...");

    // Create UDP socket here
    udpSocket = SocketCreate(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == -1)
    {
        log_error("Failed to create UDP socket");
        return false;
    }

    // Prepare address structure for sending data
    struct sockaddr_in udpAddr{};
    memset(&udpAddr, 0, sizeof(udpAddr));
    auto port = ClientConfiguration::getInstance()->getLocalHostUdpPort();

    log_info(std::format("UDP target address: {}", port));
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
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

    char buffer[2500];
    while (running)
    {
        struct sockaddr_in srcAddr{};
        socklen_t srcAddrLen = sizeof(srcAddr);
        ssize_t receivedBytes =
            RecvUdpData(udpSocket, buffer, sizeof(buffer), 0, (struct sockaddr *)&srcAddr, &srcAddrLen);
        if (receivedBytes < 0)
        {
            log_warnning("UDP receive error, retrying in 10s...");
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }
        else if (receivedBytes == 0)
        {
            log_info("UDP socket closed");
            break;
        }
        else
        {
            log_debug(std::format("Received {} bytes from UDP socket", receivedBytes));
           this->remoteUdpAddr = srcAddr;
            // send data to virtual channel
            {
                std::lock_guard<std::mutex> lock(vcMutex);
                if (vc && vc->isOpen()) {
                    vc->send(buffer, receivedBytes);
                }
            }
        }
    }

    return true;
}

void Client::Stop()
{
    log_info("Client stopping...");
    running = false;
    if(vc != nullptr)
    {
        vc->close();
    }
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

bool Client::ReconnectVC(int maxRetries, int initialBackoffMs)
{
    int retryCount = 0;
    int delayMs = 3000;

    while (running && retryCount < maxRetries) {
        // Close existing VC
        {
            std::lock_guard<std::mutex> lock(vcMutex);
            if (vc) {
                vc->close();
                vc = nullptr;
            }
        }

        // Close all TCP sockets
        for (auto sock : tcpSockets) {
            SocketClose(sock);
        }
        tcpSockets.clear();

        // Wait before reconnect (gives server time to clean up)
        if (!running) break;

        log_warnning(std::format("Waiting {}ms before reconnection attempt {}/{}...",
            delayMs, retryCount + 1, maxRetries));
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

        // Try to reconnect
        if (PrepareVC()) {
            log_info("Reconnection successful!");
            return true;
        }

        if (!running) break;

        retryCount++;

        // Exponential backoff: 1s, 2s, 4s, 8s, 16s
        delayMs = (retryCount == 1) ? initialBackoffMs : initialBackoffMs * (1 << (retryCount - 1));
    }

    return false;
}

