#include "DataProcessorThread.h"
#include "Log.h"
#include <format>

void DataProcessorThread::run()
{
    try
    {
        while (this->isRunning() && (dataReader.hasMoreData()))
        {
            auto data = dataReader.readData();
            if (data && data->size() > 0)
            {
                // Process the data using the IDataProcessor
                dataProcessor.processData(data->data(), data->size());
            }
        }
    }
    catch (const std::exception &e)
    {
        log_error(std::format("DataProcessorThread caught exception: {}", e.what()));
        this->setRunning(false);
    }
    catch (...)
    {
        log_error("DataProcessorThread caught unknown exception");
        this->setRunning(false);
    }
}
