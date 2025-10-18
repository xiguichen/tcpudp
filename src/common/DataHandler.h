#pragma once


#include <vector>

class DataHandler
{
    public:

        // Process incoming data
        virtual void processData(const std::vector<char> &data) = 0;

        // Handle errors
        virtual void handleError(const std::string &error) = 0;

        // Handle connection established event
        virtual void onConnectionEstablished() = 0;

        // Handle connection lost event
        virtual void onConnectionLost() = 0;

        // Virtual destructor
        virtual ~DataHandler() = default;

};
