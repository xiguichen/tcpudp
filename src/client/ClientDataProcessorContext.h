#pragma once

#include <DataProcessorContext.h>
#include <Counter.h>
#include <BlockingQueue.h>


class ClientDataProcessorContext: public DataProcessorContext
{
public:
    ClientDataProcessorContext() = default;
    ~ClientDataProcessorContext()  = default;

    Counter<int> syncCounter;  // Counter for Sync messages
    Counter<int> ackCounter;   // Counter for Ack messages

    BlockingQueue tcpSendQueue; // The queue for send tcp message

};
