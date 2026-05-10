### Test Scripts

Run all tests (unit + integration):
```bash
python test.py
```

### Unit Tests (Google Test)

Run directly:
```bash
Build/tests/CommonTest
```

Single test:
```bash
Build/tests/CommonTest --gtest_filter=SuiteName.TestName
```

### Integration Test

Launches the full client↔server pipeline and validates end-to-end data flow.

```bash
python test/integration_test.py
```

Flow:
```
test_script → UDP → client → TCP (VC) → server → UDP → echo_server → UDP
→ server → TCP (VC) → client → UDP → test_script
```

All ports are configurable via CLI. The test uses unique ports so it can run alongside other services:
- Server TCP: 12000
- UDP echo target: 12001
- Client local UDP: 12002

### Manual Test

Send UDP message to a port:

```bash
echo "Your message here" | nc -u 127.0.0.1 5002
```

### Architecture

@startuml

participant "test.py" as test_py
participant client
participant server
participant "echo_server.py" as server_py

test_py -> client : Send UDP data
client -> server: Forward over TCP (Virtual Channel)
server -> server_py: Forward over UDP
server_py -> server: Echo back
server -> client: Forward over TCP (Virtual Channel)
client -> test_py: Forward over UDP

@enduml
