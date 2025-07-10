#pragma once

#include <DataProcessorState.h>
#include "ClientDataProcessorContext.h"
#include "CommunicateStates.h"

class StateBindSent: public DataProcessorState
{

public:
    StateBindSent(ClientDataProcessorContext* context)
        : _context(context), _nextState(CommunicateStates::STATE_BIND_SENT) {}
    ~StateBindSent() override = default;

    // Process data in the Bind Sent state
    void processData(const std::vector<char> &data) override;

    // Transition to the next state
    void transitionToNextState() override;

    // Get the current state name
    const char* getStateName() const override { return "ConnBindSentState"; }

private:
    ClientDataProcessorContext* _context = nullptr; // Pointer to the context for state management
    CommunicateStates _nextState; // Variable to hold the next state to transition to

};
