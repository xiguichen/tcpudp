### Command for send udp message to a port

send udp message to port 8080

```bash
echo "Your message here" | nc -u 127.0.0.1 8080
```

### Architecture

@startuml

participant client.py as client_py
participant client 
participant server 
participant "server.py" as server_py

client_py -> client : Send Hello, UDP 5001
client -> server: Send Hello, TCP 6001
server -> server_py: Send Hello, TCP 7001
server_py -> server: Send Hello
server -> client: Send Hello
client -> client_py: Send Hello

@enduml
