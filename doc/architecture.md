## Client Side (Behind NAT)

@startuml

participant Client 
participant LocalHostUdpPort1
queue Queue1
participant PeerTCPPort
queue Queue2

group LocolHostReadThread
    Client -> LocalHostUdpPort1 : UDP data 1
    LocalHostUdpPort1 -> Queue1: UDP data 1
    Client -> LocalHostUdpPort1 : UDP data 2
    LocalHostUdpPort1 -> Queue1: UDP data 2
    ... More UDP data ...
    Client -> LocalHostUdpPort1 : UDP data n
    LocalHostUdpPort1 -> Queue1: UDP data n
end

group LoclHostWriteThread
    Queue1 -> PeerTCPPort: TCP data 1
    Queue1 -> PeerTCPPort: TCP data 2
    ... More TCP data ...
    Queue1 -> PeerTCPPort: TCP data n
end

group PeerHostReadThread
    PeerTCPPort -> Queue2: TCP data 1
    PeerTCPPort -> Queue2: TCP data 2
    ... More TCP data ...
    PeerTCPPort -> Queue2: TCP data n
end


group PeerHostWriteThread
    Queue2 -> Client: UDP data 1
    Queue2 -> Client: UDP data 2
    ... More UDP data ...
    Queue2 -> Client: UDP data n
end

@enduml


## Server Side Revert UDP data

@startuml

participant ClientPeer1
participant ClientPeer2
participant LocalHostPort1
queue Queue1
queue Queue2
participant LocalHostPort2
collections SocketMap

LocalHostPort1 -> LocalHostPort1: Create socket socket 0 and bind socket 0 to port 1

group ClientPeerReadThread
    ClientPeer1 -> LocalHostPort1: Peer 1 UDP data 1
    LocalHostPort1 -> Queue1: Peer 1 UDP data 1, socket 1, Peer 1 address
    ClientPeer1 -> LocalHostPort1: Peer 1 UDP data 2
    LocalHostPort1 -> Queue1: Peer 1 UDP data 2, socket 1, Peer 1 address
    ... More UDP data ...
    ClientPeer1 -> LocalHostPort1: Peer 1 UDP data n
    LocalHostPort1 -> Queue1: Peer 1 UDP data n, socket 1, Peer 1 address

    ClientPeer2 -> LocalHostPort1: Peer 2 UDP data 1
    LocalHostPort1 -> Queue1: Peer 2 UDP data 1, socket 2, Peer 2 address
    LocalHostPort1 -> SocketMap: Peer 2 address, socket 0
    ClientPeer2 -> LocalHostPort1: Peer 2 UDP data 2
    LocalHostPort1 -> Queue1: Peer 2 UDP data 2, socket 2, Peer 2 address
    ... More UDP data ...
    ClientPeer2 -> LocalHostPort1: Peer 2 UDP data n
    LocalHostPort1 -> Queue1: Peer 2 UDP data n, socket 2, Peer 2 address

end

group ClientPeerWriteThread
    Queue1 -> LocalHostPort2: Reverted Peer 1 UDP data 1, socket1
    Queue1 -> LocalHostPort2: Reverted Peer 1 UDP data 2, socket1
    ... More UDP data ...
    Queue1 -> LocalHostPort2: Reverted Peer 1 UDP data n, socket1

    Queue1 -> LocalHostPort2: Reverted Peer 2 UDP data 1, socket2
    Queue1 -> LocalHostPort2: Reverted Peer 2 UDP data 2, socket2
    ... More UDP data ...
    Queue1 -> LocalHostPort2: Reverted Peer 2 UDP data n, socket2
end

group ServerReadThread
    LocalHostPort2 -> Queue2: socket 1, UDP data 1
    LocalHostPort2 -> Queue2: socket 1, UDP data 2
    ... More UDP data ...
    LocalHostPort2 -> Queue2: socket 1, UDP data n

    LocalHostPort2 -> Queue2: socket 2, UDP data 1
    LocalHostPort2 -> Queue2: socket 2, UDP data 2
    ... More UDP data ...
    LocalHostPort2 -> Queue2: socket 2, UDP data n
end

group ServerWriteThread
    Queue2 -> ClientPeer1: Reverted UDP data 1, socket 1
    Queue2 -> ClientPeer1: Reverted UDP data 2, socket 1
    ... More UDP data ...
    Queue2 -> ClientPeer1: Reverted UDP data n, socket 1

    Queue2 -> ClientPeer2: Reverted UDP data 1, socket 2
    Queue2 -> ClientPeer2: Reverted UDP data 2, socket 2
    ... More UDP data ...
    Queue2 -> ClientPeer2: Reverted UDP data n, socket 2
end



@enduml
