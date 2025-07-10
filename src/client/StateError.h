#pragma once
#include "ClientDataProcessorContext.h"
#include <DataProcessorState.h>


class StateError: public DataProcessorState
{
public:
    StateError(ClientDataProcessorContext* context)
        : _context(context) {}
    ~StateError() override = default;

    // Process data in the Error state
    void processData(const std::vector<char> &data) override {
        // Handle error state processing
        // This could involve logging the error or notifying the user
    }

    // Transition to the next state
    void transitionToNextState() override {
        // In an error state, we might not transition to another state
        // or we might reset to a safe state
    }

    // Get the current state name
    const char* getStateName() const override { return "ConnErrorState"; }

private:
    ClientDataProcessorContext* _context = nullptr; // Pointer to the context for state management

};
