#include "Client.h"
#include "Log.h"
#include "ClientConfiguration.h"
#include "VcProtocol.h"
#include "VirtualChannelFactory.h"
#include "TcpVirtualChannel.h"
#include "Protocol.h"
#include <thread>
#include <format>

bool Client::PrepareVC()
{
    log_info("Preparing virtual channel...");

    // Start from a clean slate. A previous attempt (e.g. a Start() where PrepareVC
    // succeeded but PrepareUdpSocket failed) may have left a running watchdog, an open
    // VC, and 32 sockets in the member vectors. Without this, sockets accumulate (the
    // next VC would be built from 64+ sockets) and StartWatchdog() below would assign
    // over a still-joinable watchdog thread, calling std::terminate().
    TeardownVc();

    for (int i = 0; i < VC_TCP_CONNECTIONS; i++)
    {
        // Create TCP socket here
        SocketFd tcpSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
        if (tcpSocket == -1)
        {
            log_error("Failed to create TCP socket");
            TeardownVc(); // close any sockets opened earlier this attempt
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
            TeardownVc(); // close any sockets opened earlier this attempt
            return false;
        }

        SocketSetTcpNoDelay(tcpSocket, true);
        SocketSetReceiveBufferSize(tcpSocket, 512 * 1024);
        SocketSetSendBufferSize(tcpSocket, 128 * 1024);
        // Proactively detect half-open connections (common on macOS) so a silently
        // dead slot is surfaced via keepalive probes instead of only when data flows.
        SocketSetKeepAlive(tcpSocket, true, 10, 5, 3);

        // Assign a unique connection ID for this socket and register it with the server.
        // The server records clientId→connectionId→slotIndex so that when the watchdog
        // reconnects a dead slot, it can tell the server "close connectionId=X" instead
        // of relying on the server having independently detected the disconnection.
        uint32_t connId = nextConnectionId++;

        MsgBind bindMsg;
        bindMsg.clientId = ClientConfiguration::getInstance()->getClientId();
        bindMsg.connectionId = connId;

        std::vector<char> bindBuffer;
        UvtUtils::AppendMsgBind(bindMsg, bindBuffer);

        if (SendTcpData(tcpSocket, bindBuffer.data(), bindBuffer.size(), 0) <= 0)
        {
            log_error("Failed to send client ID to server");
            SocketClose(tcpSocket);
            TeardownVc(); // close any sockets opened earlier this attempt
            return false;
        }

        log_info(std::format("Sent client ID {} to server with connectionId {}",
                             bindMsg.clientId, connId));

        log_info("Connected to server successfully.");
        tcpSockets.push_back(tcpSocket);
        tcpConnectionIds.push_back(connId);

        // Add delay between connections to give server time to process
        if (i == 0) {
            log_info("Waiting 500ms before establishing second TCP connection...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // Create virtual channel with the connected sockets
    log_info("Creating virtual channel with connected TCP sockets...");
    auto newVc = VirtualChannelFactory::create(tcpSockets);
    if (!newVc)
    {
        log_error("Failed to create virtual channel");
        TeardownVc(); // close the 32 sockets we just opened
        return false;
    }

    // Set up receive callback
    newVc->setReceiveCallback([this](const char *data, size_t size) {
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
    newVc->setDisconnectCallback([this]() {
        log_warnning("Virtual channel disconnected. Attempting reconnection...");
        // Stop the watchdog immediately so its per-slot reconnects don't race
        // with the full ReconnectVC. If we delay this, the watchdog may already
        // be sending MsgBind requests that confuse the server.
        watchdogRunning = false;
        std::thread([this]() {
            if (!this->ReconnectVC()) {
                log_error("Reconnection failed, stopping client");
                this->Stop();
            }
        }).detach();
    });

    // Publish the fully-configured VC under the lock so the watchdog / reconnect paths
    // never observe a half-built channel or race on the shared_ptr.
    {
        std::lock_guard<std::mutex> lock(vcMutex);
        vc = newVc;
    }

    vc->open();

    StartWatchdog();

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

    // Allow the local UDP port to be rebound immediately after a restart. Without this,
    // a quick reconnect cycle fails the bind with EADDRINUSE while the previous socket
    // lingers, which cascades into the failed-Start / reconnect storm.
    SocketReuseAddress(udpSocket);

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

void Client::StopWatchdog()
{
    watchdogRunning = false;
    // Serialize with Stop()/ReconnectVC — both may try to join the watchdog thread.
    // Double-join is undefined behaviour (crash on most implementations).
    std::lock_guard<std::mutex> lock(watchdogMutex);
    if (watchdogThread.joinable())
        watchdogThread.join();
}

void Client::TeardownVc()
{
    // Stop the watchdog first so it can't touch the VC while we tear it down, and so a
    // subsequent StartWatchdog() never assigns over a still-joinable std::thread (which
    // would call std::terminate()).
    StopWatchdog();

    bool hadVc = false;
    {
        std::lock_guard<std::mutex> lock(vcMutex);
        if (vc)
        {
            vc->close();
            vc = nullptr;
            hadVc = true;
        }
    }

    // Only close the raw FDs ourselves when no VC owned them. When a VC existed,
    // vc->close() already closed them via TcpConnection::disconnect(); closing again
    // would be a double-close of an FD number the OS may have reassigned.
    if (!hadVc)
    {
        for (SocketFd fd : tcpSockets)
            if (fd != -1)
                SocketClose(fd);
    }
    tcpSockets.clear();
    tcpConnectionIds.clear();
}

void Client::Stop()
{
    log_info("Client stopping...");
    running = false;

    StopWatchdog();

    std::lock_guard<std::mutex> lock(vcMutex);
    if (vc != nullptr)
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
        TeardownVc(); // don't leave a watchdog/VC running for the next Start()
        return;
    }

    if (!PrepareUdpSocket())
    {
        log_error("Failed to prepare UDP socket");
        // PrepareVC already started the VC + watchdog; tear them down so the next
        // Start()/PrepareVC doesn't assign over a live watchdog thread (std::terminate)
        // or accumulate orphaned connections.
        TeardownVc();
        if (udpSocket != -1)
        {
            SocketClose(udpSocket);
            udpSocket = -1;
        }
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
    // Increment the reconnect epoch so any in-flight per-slot reconnect aborts
    // before it can call replaceConnection on the (soon-to-be-destroyed) VC.
    reconnectEpoch.fetch_add(1, std::memory_order_release);

    // Stop the watchdog before tearing down the VC so it doesn't race with us.
    StopWatchdog();

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

        // Clear TCP sockets — they were already closed by vc->close() →
        // TcpConnection::disconnect().  SocketClose again would be a double-close:
        // the OS may have reassigned the FD numbers, silently killing new sockets.
        tcpSockets.clear();
        tcpConnectionIds.clear();

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

void Client::StartWatchdog()
{
    // Guard against double-start. Caller must stop the previous thread before calling again.
    assert(!watchdogThread.joinable() && "StartWatchdog called with a running watchdog thread");

    watchdogRunning = true;
    watchdogThread = std::thread([this]() {
        while (watchdogRunning.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!watchdogRunning.load())
                break;

            // Read dead slots under a brief lock — don't hold the lock during connect.
            std::vector<int> deadSlots;
            {
                std::lock_guard<std::mutex> lock(vcMutex);
                if (!vc || !vc->isOpen())
                    continue;
                deadSlots = static_cast<TcpVirtualChannel *>(vc.get())->getDeadSlots();
            }

            for (int slot : deadSlots)
            {
                if (!watchdogRunning.load())
                    break;
                ReconnectSingleSlot(slot);
            }
        }
        log_info("Watchdog thread stopped.");
    });
}

bool Client::ReconnectSingleSlot(int slotIndex)
{
    // Capture the epoch at the start. If ReconnectVC increments it while we're
    // working, we must abort — our MsgBind targets the old VC which is about to
    // be destroyed, and calling replaceConnection afterwards could corrupt the
    // brand new VC.
    int epochAtStart = reconnectEpoch.load(std::memory_order_acquire);

    // If the watchdog was stopped (e.g. by disconnect callback), abort immediately.
    // The disconnect callback may already have triggered a full ReconnectVC.
    if (!watchdogRunning.load())
    {
        log_info(std::format("Watchdog: aborting reconnect for slot {} (watchdog stopped)", slotIndex));
        return false;
    }

    log_info(std::format("Watchdog: reconnecting slot {}", slotIndex));

    SocketFd tcpSocket = SocketCreate(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket == -1)
    {
        log_error(std::format("Watchdog: failed to create socket for slot {}", slotIndex));
        return false;
    }

    auto ip = ClientConfiguration::getInstance()->getSocketAddress();
    auto port = ClientConfiguration::getInstance()->getPortNumber();

    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    serverAddr.sin_port = htons(port);

    // Non-blocking connect with 2s timeout — short enough that 4 dead resend
    // slots don't block the watchdog for more than ~8s total.
    if (SocketConnectNonBlocking(tcpSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr), 2000) < 0)
    {
        log_error(std::format("Watchdog: failed to connect for slot {}", slotIndex));
        SocketClose(tcpSocket);
        return false;
    }

    // Watchdog may have been stopped while connect was in-flight.
    // Don't send a stale MsgBind to the server.
    if (!watchdogRunning.load() || reconnectEpoch.load(std::memory_order_acquire) != epochAtStart)
    {
        log_info(std::format("Watchdog: aborting reconnect for slot {} (watchdog stopped after connect)", slotIndex));
        SocketClose(tcpSocket);
        return false;
    }

    SocketSetTcpNoDelay(tcpSocket, true);
    SocketSetReceiveBufferSize(tcpSocket, 512 * 1024);
    SocketSetSendBufferSize(tcpSocket, 128 * 1024);
    // Must match PrepareVC(): without keepalive a reconnected slot that goes
    // half-open again (common on macOS) is never detected and becomes a
    // permanent zombie that the scorer routes around but the watchdog can't
    // reap — progressively shrinking the usable connection pool.
    SocketSetKeepAlive(tcpSocket, true, 10, 5, 3);

    // Include the old connection's ID so the server can close it by identity
    // instead of relying on getDeadSlots() timing (which may not have detected
    // the disconnection yet due to half-open TCP or poll latency).
    uint32_t oldConnId = (static_cast<size_t>(slotIndex) < tcpConnectionIds.size())
                             ? tcpConnectionIds[slotIndex]
                             : 0;
    uint32_t newConnId = nextConnectionId++;

    MsgBind bindMsg;
    bindMsg.clientId = ClientConfiguration::getInstance()->getClientId();
    bindMsg.slotIndex = static_cast<int8_t>(slotIndex);
    bindMsg.connectionId = oldConnId;
    std::vector<char> bindBuffer;
    UvtUtils::AppendMsgBind(bindMsg, bindBuffer);

    // Check epoch immediately before sending — ReconnectVC may have started since
    // the earlier check (line 371). If we send a stale MsgBind the server will
    // hot-swap a slot into the old VC, keeping it alive on the server side even
    // after ReconnectVC tears it down.  Then PrepareVC's new connections get
    // rejected as "excess sockets" and we loop forever.
    if (reconnectEpoch.load(std::memory_order_acquire) != epochAtStart)
    {
        log_info(std::format("Watchdog: aborting reconnect for slot {} (epoch changed before MsgBind)", slotIndex));
        SocketClose(tcpSocket);
        return false;
    }

    if (SendTcpData(tcpSocket, bindBuffer.data(), bindBuffer.size(), 0) <= 0)
    {
        log_error(std::format("Watchdog: failed to send MsgBind for slot {}", slotIndex));
        SocketClose(tcpSocket);
        return false;
    }

    // Acquire lock only briefly to hot-swap the connection.
    std::lock_guard<std::mutex> lock(vcMutex);
    if (!vc || !vc->isOpen())
    {
        SocketClose(tcpSocket);
        return false;
    }

    // ReconnectVC may have started while we were connecting. If the epoch changed,
    // the old VC is being torn down — don't splice a fresh socket into a dying VC.
    if (reconnectEpoch.load(std::memory_order_acquire) != epochAtStart)
    {
        log_info(std::format("Watchdog: aborting reconnect for slot {} (epoch changed, ReconnectVC in progress)", slotIndex));
        SocketClose(tcpSocket);
        return false;
    }

    if (static_cast<size_t>(slotIndex) < tcpSockets.size())
    {
        // Don't close tcpSockets[slotIndex] here — TcpConnection::disconnect() (called by
        // the IO thread when it detected the dead connection) already closed the FD via
        // SocketClose(). Calling SocketClose again on the same FD number is a double-close:
        // the OS may have reassigned this FD number to a *different* connection's socket,
        // and we'd silently kill that connection instead. This cascades a single TCP failure
        // into many, eventually killing all 32 connections.
        tcpSockets[slotIndex] = tcpSocket;
    }

    if (static_cast<size_t>(slotIndex) < tcpConnectionIds.size())
    {
        tcpConnectionIds[slotIndex] = newConnId;
    }

    static_cast<TcpVirtualChannel *>(vc.get())->replaceConnection(slotIndex, tcpSocket);
    log_info(std::format("Watchdog: slot {} reconnected successfully.", slotIndex));
    return true;
}
