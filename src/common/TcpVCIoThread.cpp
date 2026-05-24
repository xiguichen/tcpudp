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

void TcpVCIoThread::run()
{
    log_info("TcpVCIoThread started");

    size_t numConns = connections.size();
    std::vector<struct pollfd> pollfds(numConns);

    while (this->isRunning())
    {
        for (size_t i = 0; i < numConns; i++)
        {
            if (connections[i] && connections[i]->isConnected())
            {
                pollfds[i].fd = connections[i]->getSocketFd();
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
            if (pollfds[i].fd < 0) continue;

            if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP))
            {
                readFromConnection(static_cast<int>(i));
                if (!this->isRunning()) return;
            }
        }

        if (ready == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    log_info("TcpVCIoThread stopped");
}

void TcpVCIoThread::readFromConnection(int connIndex)
{
    if (connIndex < 0 || static_cast<size_t>(connIndex) >= connections.size())
        return;

    auto &conn = connections[connIndex];
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
