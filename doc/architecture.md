## Client Side Revert UDP data

### LocalHost read thread

@startuml
Client -> LocalHostPort1 : UDP data 1
LocalHostPort1 -> Queue1: UDP data 1
Client -> LocalHostPort1 : UDP data 2
LocalHostPort1 -> Queue1: UDP data 2
... More UDP data ...
Client -> LocalHostPort1 : UDP data n
LocalHostPort1 -> Queue1: UDP data n
@enduml

### LocalHost write thread

@startuml

Queue1 -> RemoteHostPort2: Reverted UDP data 1
Queue1 -> RemoteHostPort2: Reverted UDP data 2
... More UDP data ...
Queue1 -> RemoteHostPort2: Reverted UDP data n

@enduml

### RemoteHost read thread

@startuml

RemoteHostPort2 -> Queue2: UDP data 1
RemoteHostPort2 -> Queue2: UDP data 2
... More UDP data ...
RemoteHostPort2 -> Queue2: UDP data n

@enduml

### RemoteHost write thread

@startuml

Queue2 -> LocalHostPort1: Reverted UDP data 1
LocalHostPort1 -> Client: Reverted UDP data 1
Queue2 -> LocalHostPort1: Reverted UDP data 2
LocalHostPort1 -> Client: Reverted UDP data 2
... More UDP data ...
Queue2 -> LocalHostPort1: Reverted UDP data n
LocalHostPort1 -> Client: Reverted UDP data n

@enduml
