#include "DataProcessorThread.h"

void DataProcessorThread::run()
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
