#pragma once

#include <DataProcessorState.h>
#include <DataProcessorContext.h>
#include "ClientDataProcessorContext.h"
#include "CommunicateStates.h"


class StateSyncSent: public DataProcessorState
{
public:
    StateSyncSent(ClientDataProcessorContext* context) 
        : _context(context), _nextState(CommunicateStates::STATE_SYNC_SENT) {}
    ~StateSyncSent() override = default;

    // Process data in the Sync Sent state
    void processData(const std::vector<char> &data) override;

    // Transition to the next state
    void transitionToNextState() override;

    // Get the current state name
    const char* getStateName() const override { return "ConnSyncSentState"; }

private:

    ClientDataProcessorContext* _context = nullptr; // Pointer to the context for state management
    
    CommunicateStates _nextState;

};
