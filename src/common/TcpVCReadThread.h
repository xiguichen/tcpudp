#pragma once

#include "StopableThread.h"
#include "TcpConnection.h"
#include <functional>
#include <memory>
#include <vector>

class TcpVCReadThread : public StopableThread
{

  public:

    TcpVCReadThread(TcpConnectionSp connection, int connectionIndex) : connection(connection), connectionIndex(connectionIndex) {}

    virtual ~TcpVCReadThread();

    void setDataCallback(
        std::function<void(const uint64_t messageId, std::shared_ptr<std::vector<char>> data)> callback);

    void setDisconnectCallback(std::function<void(TcpConnectionSp connection)> callback);

    void setResendRequestCallback(std::function<void(uint64_t messageId)> callback)
    {
        resendRequestCallback = std::move(callback);
    }

    void setMissingNotifyCallback(std::function<void(const std::vector<uint64_t> &missingIds)> callback)
    {
        missingNotifyCallback = std::move(callback);
    }

  protected:
    virtual void run();

  private:
    TcpConnectionSp connection;
    int connectionIndex{-1};
    std::vector<char> bufferVector;

    inline bool hasEnoughData(const char *buffer, size_t size);

    bool hasEnoughDataForData(const char *buffer, size_t size);
    bool hasEnoughDataForResendRequest(const char *buffer, size_t size);
    bool hasEnoughDataForMissingNotify(const char *buffer, size_t size);

    int processBuffer(std::vector<char> &buffer);

    int processAckBuffer(std::vector<char> &buffer);
    int processDataBuffer(std::vector<char> &buffer);
    int processResendRequestBuffer(std::vector<char> &buffer);
    int processMissingNotifyBuffer(std::vector<char> &buffer);

    std::function<void(const uint64_t messageId, std::shared_ptr<std::vector<char>> data)> dataCallback;
    std::function<void(const uint64_t messageId)> ackCallback;
    std::function<void(TcpConnectionSp connection)> disconnectCallback;
    std::function<void(uint64_t messageId)> resendRequestCallback;
    std::function<void(const std::vector<uint64_t> &missingIds)> missingNotifyCallback;
};

typedef std::shared_ptr<TcpVCReadThread> TcpVCReadThreadSp;
