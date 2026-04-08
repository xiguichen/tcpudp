#include "TcpVCWriteThread.h"
#include "Log.h"

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");

    while (this->isRunning())
    {
        auto data = writeQueue->dequeue();
        if (!data)
        {
            log_info("Thread exiting on null data (queue cancelled or stopped)");
            break;
        }
        if (!this->isRunning())
        {
            log_info("Thread exiting on stop signal");
            break;
        }
        if (this->connection && this->connection->isConnected()) {
            connection->send(data->data(), data->size());
        }
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
