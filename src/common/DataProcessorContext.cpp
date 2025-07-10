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
void DataProcessorContext::setState(std::shared_ptr<DataProcessorState> state)
{
    currentState = std::move(state);
}

const char *DataProcessorContext::getCurrentStateName() const
{
    if (currentState)
    {
        return currentState->getStateName();
    }
    return "No State Set";
}

void DataProcessorContext::transitionToNextState()
{
    if (currentState)
    {
        currentState->transitionToNextState();
    }
    else
    {
        // Handle error: no state set
        throw std::runtime_error("No state set for DataProcessorContext");
    }
}
