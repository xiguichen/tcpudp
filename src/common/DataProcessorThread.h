#pragma once
#include "IDataProcessor.h"
#include "IDataReader.h"
#include "StopableThread.h"

class DataProcessorThread : public StopableThread
{
  public:
    DataProcessorThread(IDataReader &dataReader, IDataProcessor &dataProcessor)
        : dataReader(dataReader), dataProcessor(dataProcessor)
    {
    }

    virtual ~DataProcessorThread() = default;

    // Thread run method
    void run() override;

  private:
    IDataReader &dataReader;
    IDataProcessor &dataProcessor;
};
