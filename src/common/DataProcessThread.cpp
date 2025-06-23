#include "DataProcessThread.h"
#include "Log.h"

using namespace Logger;

void DataProcessThread::run()
{
    while (this->isRunning())
    {
        // Get data to read
        auto data = this->read();

        if(data == nullptr)
        {
            // Handle read error
            Log::getInstance().error("Failed to read data from socket");
            this->setRunning(true);
            break;
        }

        if (data && !data->empty())
        {
            // Process the read data
            this->processData(*data);
        }
        else
        {
            // No data available, yield to other threads
            std::this_thread::yield();
        }
    }
}


