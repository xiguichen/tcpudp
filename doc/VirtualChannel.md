A virtual channel is a logical communication path that allows data to be transmitted between two endpoints in a network or system. It is often used in the context of computer networks, telecommunication systems, and distributed computing environments.

In this implementation, we will define a `VirtualChannel` class that encaps
1. The virtual channel should handle data transmission between two endpoints.
2. It should support data retransmission in case of errors.
3. It should provide a mechanism to close the channel gracefully.


### Protocol

```plantuml


struct DataBlockFlags {
    Sync : bit
    Ack : bit
    Reserved : bit
    Reserved : bit
    Reserved : bit
    Reserved : bit
    Reserved : bit
    Reserved : bit
}

struct DataBlockHeader {
    size : size_t
    Counter: int
    Flags: DataBlockFlags
}

struct DataBlock {
    header : DataBlockHeader
    data : char*
}

DataBlockHeader *-- DataBlockFlags: contains
DataBlock *-- DataBlockHeader: contains


class UnAckedDataMap {
    +UnAckedDataMap()
    +~UnAckedDataMap()
    +addData(data : DataBlock)
    +getData() : DataBlock
    +isEmpty() : bool
    +removeData() : DataBlock
    ..private..
    -_dataBlocks : std::map<int, DataBlock>
}

class ReceivedDataMap {
    +ReceivedDataMap()
    +~ReceivedDataMap()
    +addData(data : DataBlock)
    +getData() : DataBlock
    +isEmpty() : bool
    +removeData() : DataBlock
    ..private..
    -_dataBlocks : std::map<int, DataBlock>
}

class VcDataQueue {
    +VcDataQueue()
    +~VcDataQueue()
    +enqueue(data : DataBlock)
    +dequeue() : DataBlock
    +isEmpty() : bool
    ..private..
    -_queue : std::queue<DataBlock>
}

```

Notes:
1. Sync flag is only set for DataBlock that need to Acknowledge.
2. Ack flag is set for DataBlock that has been acknowledged.
3. UnAckedDataMap is used to keep track of sent data blocks that have not yet been acknowledged.
4. Add data to ReceivedDataMap when a DataBlock is received.
5. VCDataQueue is used to manage the data blocks that are ready to use in the virtual channel.
