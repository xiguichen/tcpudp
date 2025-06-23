#include "SocketWriteThread.h"


void SocketWriteThread::run()
{

    while (this->isRunning())
    {
        // Get data to write
        auto data = this->getData();

        if (data && !data->empty())
        {
            // Write data to the socket
            this->write(*data);
        }
        else
        {
            // No data available, yield to other threads
            std::this_thread::yield();
        }
    }
}
