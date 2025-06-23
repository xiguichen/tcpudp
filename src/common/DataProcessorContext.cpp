#include "DataProcessorContext.h"

void DataProcessorContext::processData(const std::vector<char> &data)
{
    if (currentState)
    {
        currentState->processData(data);
    }
    else
    {
        // Handle error: no state set
        throw std::runtime_error("No state set for DataProcessorContext");
    }
}
