#pragma once

#include <vector>

class DataProcessorState
{
public:
    virtual ~DataProcessorState() = default;

    // Method to process data
    virtual void processData(const std::vector<char>& data) = 0;

    // Method to handle state transition
    virtual void transitionToNextState() = 0;

    // Method to get the current state name
    virtual const char* getStateName() const = 0;
};
