#pragma once

#include "StopableThread.h"
#include "TcpConnection.h"
#include <functional>
#include <memory>
#include <vector>

class TcpVCReadThread : public StopableThread
{

  public:

    TcpVCReadThread(TcpConnectionSp connection) : connection(connection) {}

    // Set the data callback
    void setDataCallback(
        std::function<void(const uint64_t messageId, std::shared_ptr<std::vector<char>> data)> callback);

    // Set the ack callback
    void setAckCallback(std::function<void(const uint64_t messageId)> callback);

  protected:
    virtual void run();

  private:
    TcpConnectionSp connection;
    std::vector<char> bufferVector;

    bool hasEnoughData(const char *buffer, size_t size);

    bool hasEnoughDataForData(const char *buffer, size_t size);

    int processBuffer(std::vector<char> &buffer);

    int processAckBuffer(std::vector<char> &buffer);
    int processDataBuffer(std::vector<char> &buffer);

    std::function<void(const uint64_t messageId, std::shared_ptr<std::vector<char>> data)> dataCallback;
    std::function<void(const uint64_t messageId)> ackCallback;
};

typedef std::shared_ptr<TcpVCReadThread> TcpVCReadThreadSp;
