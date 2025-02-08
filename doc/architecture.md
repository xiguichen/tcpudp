
### Localhost read thread

@startuml
Client -> LocalhostPort1 : UDP data 1
LocalhostPort1 -> Queue1: UDP data 1
Client -> LocalhostPort1 : UDP data 2
LocalhostPort1 -> Queue1: UDP data 2
... More UDP data ...
Client -> LocalhostPort1 : UDP data n
LocalhostPort1 -> Queue1: UDP data n
@enduml

### Localhost write thread

@startuml

Queue1 -> RemotehostPort2: UDP data 1
Queue1 -> RemotehostPort2: UDP data 2
... More UDP data ...
Queue1 -> RemotehostPort2: UDP data n

@enduml

### Remotehost read thread

@startuml

RemotehostPort2 -> Queue2: UDP data 1
RemotehostPort2 -> Queue2: UDP data 2
... More UDP data ...
RemotehostPort2 -> Queue2: UDP data n

@enduml

### RemoteHost write thread

@startuml

Queue2 -> LocalhostPort1: UDP data 1
LocalhostPort1 -> Client: UDP data 1
Queue2 -> LocalhostPort1: UDP data 2
LocalhostPort1 -> Client: UDP data 2
... More UDP data ...
Queue2 -> LocalhostPort1: UDP data n
LocalhostPort1 -> Client: UDP data n

@enduml
