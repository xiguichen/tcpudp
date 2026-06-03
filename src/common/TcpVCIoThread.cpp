#include "TcpVCIoThread.h"
#include "Log.h"
#include "Socket.h"
#include <format>
#include <thread>

using namespace Logger;

static constexpr int IO_POLL_TIMEOUT_MS = 50;

TcpVCIoThread::TcpVCIoThread(std::vector<TcpConnectionSp> connections_,
                              std::function<void(uint64_t, std::shared_ptr<std::vector<char>>, int)> dataCallback_,
                              std::function<void(uint64_t)> resendRequestCallback_,
                              std::function<void(const std::vector<uint64_t> &)> missingNotifyCallback_,
                              std::function<void(TcpConnectionSp)> disconnectCallback_)
    : connections(std::move(connections_)),
      dataCallback(std::move(dataCallback_)),
      resendRequestCallback(std::move(resendRequestCallback_)),
      missingNotifyCallback(std::move(missingNotifyCallback_)),
      disconnectCallback(std::move(disconnectCallback_))
{
    readBuffers.resize(connections.size());
    for (auto &conn : connections)
    {
        if (conn && conn->isConnected())
        {
            SocketFd fd = conn->getSocketFd();
            if (fd != -1)
                SetSocketNonBlocking(fd);
        }
    }
}

TcpVCIoThread::~TcpVCIoThread() = default;

void TcpVCIoThread::replaceConnection(int slot, TcpConnectionSp conn)
{
    if (slot < 0 || static_cast<size_t>(slot) >= connections.size())
        return;
    SetSocketNonBlocking(conn->getSocketFd());
    std::lock_guard<std::mutex> lock(connectionsMutex);
    connections[slot] = conn;
    // Signal run() to refresh its cached snapshot on the next iteration.
    connGeneration.fetch_add(1, std::memory_order_release);
    // readBuffers[slot] is reset on the next run() iteration when the fd change is detected.
}

void TcpVCIoThread::run()
{
    log_info("TcpVCIoThread started");

    const size_t numConns = connections.size();
    std::vector<struct pollfd> pollfds(numConns);
    // Track per-slot fd values so we can detect when a connection is replaced
    // and clear the stale read buffer before the new connection's data arrives.
    std::vector<SocketFd> prevFds(numConns, -1);

    // Cached connection snapshot, refreshed only when replaceConnection() bumps
    // connGeneration. Copying 32 shared_ptrs (plus the mutex lock) every poll cycle
    // was pure overhead on the hot receive path; replacements are rare (reconnects).
    std::vector<TcpConnectionSp> snapshot;
    uint64_t cachedGen = static_cast<uint64_t>(-1);

    while (this->isRunning())
    {
        // Refresh the snapshot only on change. Disconnections are still detected every
        // iteration because the per-slot fd is re-read from the cached shared_ptrs
        // (getSocketFd()/isConnected()) when building pollfds below.
        uint64_t gen = connGeneration.load(std::memory_order_acquire);
        if (gen != cachedGen)
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            snapshot = connections;
            cachedGen = gen;
        }

        for (size_t i = 0; i < numConns; i++)
        {
            SocketFd currFd = (snapshot[i] && snapshot[i]->isConnected()) ? snapshot[i]->getSocketFd() : -1;
            // If the fd changed (connection replaced), discard buffered data from the old stream.
            if (currFd != prevFds[i])
            {
                readBuffers[i] = ReadBuffer{};
                prevFds[i] = currFd;
            }

            if (currFd != -1)
            {
                pollfds[i].fd = currFd;
                pollfds[i].events = POLLIN;
                pollfds[i].revents = 0;
            }
            else
            {
                pollfds[i].fd = -1;
                pollfds[i].events = 0;
                pollfds[i].revents = 0;
            }
        }

        int ready = SocketPollMany(pollfds.data(), numConns, IO_POLL_TIMEOUT_MS);
        if (ready < 0)
        {
            log_error("SocketPollMany failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        for (size_t i = 0; i < numConns; i++)
        {
            if (pollfds[i].fd == (SocketFd)-1) continue;

            if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP))
            {
                readFromConnection(static_cast<int>(i), snapshot[i]);
                if (!this->isRunning()) return;
            }
        }
        // No sleep on ready==0: SocketPollMany already blocked up to
        // IO_POLL_TIMEOUT_MS, so the timeout path needs no extra back-off.
    }

    log_info("TcpVCIoThread stopped");
}

void TcpVCIoThread::readFromConnection(int connIndex, TcpConnectionSp conn)
{
    if (!conn || !conn->isConnected()) return;

    auto &buf = readBuffers[connIndex];

    char temp[4096];

    // Drain socket using direct recv (socket is already non-blocking; poll() already confirmed POLLIN).
    while (this->isRunning())
    {
        ssize_t n = RecvTcpDirect(conn->getSocketFd(), temp, sizeof(temp), 0);
        if (n > 0)
        {
            buf.data.insert(buf.data.end(), temp, temp + n);
        }
        else if (n == SOCKET_ERROR_CLOSED)
        {
            log_info("Connection closed");
            conn->disconnect();  // marks connected=false before VC-level callback counts alive conns
            if (disconnectCallback)
                disconnectCallback(conn);
            return;
        }
        else if (n == SOCKET_ERROR_WOULD_BLOCK || n == SOCKET_ERROR_TIMEOUT)
        {
            break;
        }
        else
        {
            log_error("Read error on connection");
            conn->disconnect();  // same: mark dead before callback
            if (disconnectCallback)
                disconnectCallback(conn);
            return;
        }
    }

    // Parse packets using a read-offset to avoid O(N) erase-from-front on every packet.
    while (buf.available() >= VC_MIN_DATA_PACKET_SIZE)
    {
        uint8_t packetType = static_cast<uint8_t>(buf.begin()[0]);
        switch (static_cast<VcPacketType>(packetType))
        {
        case VcPacketType::DATA:
        {
            if (buf.available() < sizeof(VCDataPacket))
                return;
            VCDataPacket *pkt = reinterpret_cast<VCDataPacket *>(buf.begin());
            if (pkt->dataLength > VC_MAX_DATA_PAYLOAD_SIZE)
                return;
            size_t totalSize = sizeof(VCDataPacket) + pkt->dataLength;
            if (buf.available() < totalSize)
                return;

            auto data = std::make_shared<std::vector<char>>(pkt->data, pkt->data + pkt->dataLength);
            if (dataCallback)
                dataCallback(pkt->header.messageId, data, connIndex);
            buf.consume(totalSize);
            break;
        }
        case VcPacketType::RESEND_REQUEST:
        {
            if (buf.available() < sizeof(VCResendRequest))
                return;
            VCResendRequest *req = reinterpret_cast<VCResendRequest *>(buf.begin());
            if (resendRequestCallback)
                resendRequestCallback(req->missingMessageId);
            buf.consume(sizeof(VCResendRequest));
            break;
        }
        case VcPacketType::MISSING_NOTIFY:
        {
            if (buf.available() < sizeof(VCHeader) + 1)
                return;
            VCMissingNotify *notify = reinterpret_cast<VCMissingNotify *>(buf.begin());

            // Reject malformed packet: count must not exceed the fixed-size array in the struct.
            // Without this check, notify->count > 64 causes an out-of-bounds read (UB / CVE risk).
            if (notify->count > VC_MAX_MISSING_IDS_PER_NOTIFY)
            {
                log_error(std::format("MISSING_NOTIFY count {} exceeds max {}, dropping connection",
                                      notify->count, VC_MAX_MISSING_IDS_PER_NOTIFY));
                buf.data.clear();
                buf.readOffset = 0;
                return;
            }

            size_t expectedSize = sizeof(VCHeader) + 1 + notify->count * sizeof(uint64_t);
            if (buf.available() < expectedSize)
                return;

            std::vector<uint64_t> missingIds(notify->missingIds, notify->missingIds + notify->count);
            if (missingNotifyCallback)
                missingNotifyCallback(missingIds);
            buf.consume(expectedSize);
            break;
        }
        default:
            log_error(std::format("Unknown packet type: {}", packetType));
            buf.data.clear();
            buf.readOffset = 0;
            return;
        }
    }
}
