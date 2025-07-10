#pragma once

#include "DataProcessorState.h"
#include <vector>

class DataProcessorContext
{
public:
    // Constructor
    DataProcessorContext();

    // Destructor
    ~DataProcessorContext();

    // Method to set the current state
    void setState(std::shared_ptr<DataProcessorState> state);

    // Method to process data
    void processData(const std::vector<char> &data);

    // Method to transition to the next state
    void transitionToNextState();

    // Method to get the current state name
    const char *getCurrentStateName() const;

  private:
    std::shared_ptr<DataProcessorState> currentState; // Pointer to the current state

};
