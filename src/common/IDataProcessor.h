#pragma once

class IDataProcessor
{
    public:

        // Process incoming data
        virtual bool processData(char* data, size_t size) = 0;
};
