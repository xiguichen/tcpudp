# Configuration Guide

## Overview

The TCP/UDP project provides flexible configuration options for both client and server components. Configuration can be set through JSON files, command-line arguments, and programmatic API.

## Table of Contents

1. [Configuration Files](#configuration-files)
2. [Command Line Options](#command-line-options)
3. [Programmatic Configuration](#programmatic-configuration)
4. [Environment Variables](#environment-variables)
5. [Configuration Validation](#configuration-validation)
6. [Advanced Configuration](#advanced-configuration)
7. [Production Configuration](#production-configuration)
8. [Troubleshooting](#troubleshooting)

## Configuration Files

### Client Configuration

Default location: `src/client/config.json`

```json
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 7001,
  "peerAddress": "20.2.117.216",
  "clientId": 1,
  "connectTimeout": 5000,
  "reconnectDelay": 1000,
  "maxReconnectAttempts": 5,
  "recvBufferSize": 65536,
  "sendBufferSize": 65536,
  "virtualChannelConnections": 8,
  "sendQueueThreshold": 500,
  "logLevel": "INFO",
  "enablePerfCounters": true,
  "reorderTimeout": 4000
}
```

#### Client Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `localHostUdpPort` | int | 5002 | Local UDP port for client |
| `peerTcpPort` | int | 7001 | Server TCP port for initial connection |
| `peerAddress` | string | "localhost" | Server IP address or hostname |
| `clientId` | int | 1 | Client ID (server may override) |
| `connectTimeout` | int | 5000 | TCP connection timeout (ms) |
| `reconnectDelay` | int | 1000 | Delay between reconnection attempts (ms) |
| `maxReconnectAttempts` | int | 5 | Maximum reconnection attempts |
| `recvBufferSize` | int | 65536 | TCP receive buffer size (bytes) |
| `sendBufferSize` | int | 65536 | TCP send buffer size (bytes) |
| `virtualChannelConnections` | int | 8 | Number of TCP connections for virtual channel |
| `sendQueueThreshold` | int | 500 | Drop threshold for send queues |
| `logLevel` | string | "INFO" | Log level (DEBUG, INFO, WARNING, ERROR) |
| `enablePerfCounters` | bool | true | Enable performance counters |
| `reorderTimeout` | int | 4000 | Maximum time for message reordering (ms) |

### Server Configuration

Programmatically configured in `ServerConfiguration.h/cpp`

```cpp
struct ServerConfiguration {
    int tcpPort = 7001;
    int maxConnections = 100;
    int backlog = 50;
    int recvBufferSize = 65536;
    int sendBufferSize = 65536;
    int idleTimeout = 30000;
    int threadPoolSize = 4;
    LogLevel logLevel = INFO;
    bool enablePerfCounters = true;
    std::string logFile = "";
};
```

#### Server Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `tcpPort` | int | 7001 | TCP port for server to listen on |
| `maxConnections` | int | 100 | Maximum concurrent client connections |
| `backlog` | int | 50 | Listen backlog size |
| `recvBufferSize` | int | 65536 | TCP receive buffer size (bytes) |
| `sendBufferSize` | int | 65536 | TCP send buffer size (bytes) |
| `idleTimeout` | int | 30000 | Idle connection timeout (ms) |
| `threadPoolSize` | int | 4 | Number of worker threads |
| `logLevel` | enum | INFO | Log level |
| `enablePerfCounters` | bool | true | Enable performance counters |
| `logFile` | string | "" | Log file path (empty for stdout) |

## Command Line Options

### Server Command Line Options

```bash
# Basic server startup
./server --port 7001 --max-connections 100

# With logging
./server --log-level DEBUG --log-file server.log

# With performance monitoring
./server --enable-perf-counters --log-level INFO

# Help
./server --help
```

#### Server Command Line Arguments

| Argument | Description | Default |
|----------|-------------|---------|
| `--port` | TCP port to listen on | 7001 |
| `--max-connections` | Maximum concurrent connections | 100 |
| `--backlog` | Listen backlog size | 50 |
| `--recv-buffer` | TCP receive buffer size | 65536 |
| `--send-buffer` | TCP send buffer size | 65536 |
| `--idle-timeout` | Idle timeout in seconds | 30 |
| `--log-level` | Log level (DEBUG/INFO/WARNING/ERROR) | INFO |
| `--log-file` | Log file path | stdout |
| `--enable-perf-counters` | Enable performance counters | true |
| `--threads` | Thread pool size | 4 |
| `--help` | Show help message | - |

### Client Command Line Options

```bash
# Connect to server
./client --peer-address 20.2.117.216 --peer-port 7001

# With configuration file
./client --config custom-config.json

# With logging
./client --log-level DEBUG --log-file client.log

# Help
./client --help
```

#### Client Command Line Arguments

| Argument | Description | Default |
|----------|-------------|---------|
| `--peer-address` | Server IP address or hostname | localhost |
| `--peer-port` | Server TCP port | 7001 |
| `--local-udp-port` | Local UDP port | 5002 |
| `--client-id` | Client ID | 1 |
| `--config` | Configuration file path | src/client/config.json |
| `--connect-timeout` | Connection timeout in seconds | 5 |
| `--reconnect-delay` | Reconnect delay in seconds | 1 |
| `--max-reconnect` | Maximum reconnection attempts | 5 |
| `--log-level` | Log level (DEBUG/INFO/WARNING/ERROR) | INFO |
| `--log-file` | Log file path | stdout |
| `--help` | Show help message | - |

## Programmatic Configuration

### Client Configuration

```cpp
#include "Client.h"
#include "ClientConfiguration.h"

int main() {
    Client client;
    
    // Create configuration
    ClientConfiguration config;
    config.peerAddress = "20.2.117.216";
    config.peerTcpPort = 7001;
    config.localHostUdpPort = 5002;
    config.clientId = 1;
    config.connectTimeout = 5000;
    config.recvBufferSize = 65536;
    config.sendBufferSize = 65536;
    config.virtualChannelConnections = 8;
    config.sendQueueThreshold = 500;
    config.logLevel = DEBUG;
    
    // Set configuration
    client.setConfiguration(config);
    
    // Connect to server
    if (!client.connect(config.peerAddress, config.peerTcpPort)) {
        log_error("Connection failed");
        return 1;
    }
    
    // Send data
    client.sendUDPData("Hello, Server!", 13);
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return 0;
}
```

### Server Configuration

```cpp
#include "Server.h"
#include "ServerConfiguration.h"

int main() {
    Server server;
    
    // Create configuration
    ServerConfiguration config;
    config.tcpPort = 7001;
    config.maxConnections = 100;
    config.backlog = 50;
    config.recvBufferSize = 65536;
    config.sendBufferSize = 65536;
    config.idleTimeout = 30000;
    config.threadPoolSize = 4;
    config.logLevel = INFO;
    config.logFile = "server.log";
    
    // Set configuration
    server.setConfiguration(config);
    
    // Start server
    if (!server.start()) {
        log_error("Failed to start server");
        return 1;
    }
    
    log_info("Server started on port %d", config.tcpPort);
    
    // Wait for shutdown
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

## Environment Variables

### Configuration Override

Environment variables can override configuration values:

```bash
# Set server port
export SERVER_PORT=8001
export MAX_CONNECTIONS=200

# Set client configuration
export PEER_ADDRESS=192.168.1.100
export LOG_LEVEL=DEBUG
```

### Environment Variable Mappings

| Environment Variable | Configuration File | Default Value |
|---------------------|-------------------|--------------|
| `SERVER_PORT` | Server: tcpPort | 7001 |
| `MAX_CONNECTIONS` | Server: maxConnections | 100 |
| `BACKLOG` | Server: backlog | 50 |
| `RECV_BUFFER_SIZE` | Both: recvBufferSize | 65536 |
| `SEND_BUFFER_SIZE` | Both: sendBufferSize | 65536 |
| `LOG_LEVEL` | Both: logLevel | INFO |
| `LOG_FILE` | Both: logFile | "" |
| `ENABLE_PERF_COUNTERS` | Both: enablePerfCounters | true |
| `CLIENT_ID` | Client: clientId | 1 |
| `PEER_ADDRESS` | Client: peerAddress | localhost |
| `PEER_PORT` | Client: peerTcpPort | 7001 |
| `LOCAL_UDP_PORT` | Client: localHostUdpPort | 5002 |
| `RECONNECT_DELAY` | Client: reconnectDelay | 1000 |
| `MAX_RECONNECT_ATTEMPTS` | Client: maxReconnectAttempts | 5 |

## Configuration Validation

### Custom Configuration Loader

```cpp
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

class ConfigValidator {
public:
    static ClientConfiguration validateClientConfig(const std::string& filename) {
        ClientConfiguration config;
        nlohmann::json json;
        
        // Load JSON file
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + filename);
        }
        
        file >> json;
        
        // Validate required fields
        if (!json.contains("peerAddress") || !json["peerAddress"].is_string()) {
            throw std::runtime_error("peerAddress is required");
        }
        
        if (!json.contains("peerTcpPort") || !json["peerTcpPort"].is_number()) {
            throw std::runtime_error("peerTcpPort is required");
        }
        
        // Set with validation
        config.peerAddress = json["peerAddress"];
        config.peerTcpPort = json["peerTcpPort"];
        
        // Optional fields with validation
        if (json.contains("localHostUdpPort")) {
            if (!json["localHostUdpPort"].is_number() || 
                json["localHostUdpPort"] < 1 || json["localHostUdpPort"] > 65535) {
                throw std::runtime_error("localHostUdpPort must be between 1 and 65535");
            }
            config.localHostUdpPort = json["localHostUdpPort"];
        }
        
        if (json.contains("virtualChannelConnections")) {
            int vcConn = json["virtualChannelConnections"];
            if (vcConn < 1 || vcConn > 32) {
                throw std::runtime_error("virtualChannelConnections must be between 1 and 32");
            }
            config.virtualChannelConnections = vcConn;
        }
        
        // Validate log level
        if (json.contains("logLevel")) {
            std::string level = json["logLevel"];
            if (level == "DEBUG") config.logLevel = DEBUG;
            else if (level == "INFO") config.logLevel = INFO;
            else if (level == "WARNING") config.logLevel = WARNING;
            else if (level == "ERROR") config.logLevel = ERROR;
            else throw std::runtime_error("Invalid log level");
        }
        
        return config;
    }
    
    static ServerConfiguration validateServerConfig(const nlohmann::json& json) {
        ServerConfiguration config;
        
        // Validate and set port
        if (!json.contains("tcpPort") || !json["tcpPort"].is_number()) {
            throw std::runtime_error("tcpPort is required");
        }
        
        int port = json["tcpPort"];
        if (port < 1 || port > 65535) {
            throw std::runtime_error("tcpPort must be between 1 and 65535");
        }
        
        config.tcpPort = port;
        
        // Validate other fields
        if (json.contains("maxConnections")) {
            int maxConn = json["maxConnections"];
            if (maxConn < 1 || maxConn > 10000) {
                throw std::runtime_error("maxConnections must be between 1 and 10000");
            }
            config.maxConnections = maxConn;
        }
        
        if (json.contains("recvBufferSize")) {
            int bufSize = json["recvBufferSize"];
            if (bufSize < 1024 || bufSize > 1048576) {  // 1KB to 1MB
                throw std::runtime_error("recvBufferSize must be between 1024 and 1048576");
            }
            config.recvBufferSize = bufSize;
        }
        
        return config;
    }
};
```

### Configuration Usage Example

```cpp
try {
    // Load and validate configuration
    ClientConfiguration config = ConfigValidator::validateClientConfig("config.json");
    
    // Create client with validated config
    Client client;
    client.setConfiguration(config);
    
    // Connect
    if (!client.connect(config.peerAddress, config.peerTcpPort)) {
        log_error("Connection failed");
        return 1;
    }
    
    log_info("Connected successfully with validated configuration");
    
} catch (const std::exception& e) {
    log_error("Configuration error: %s", e.what());
    return 1;
}
```

## Advanced Configuration

### Multi-Server Configuration

```json
{
  "servers": [
    {
      "address": "server1.example.com",
      "port": 7001,
      "weight": 1
    },
    {
      "address": "server2.example.com", 
      "port": 7001,
      "weight": 2
    }
  ],
  "loadBalanceStrategy": "weighted_round_robin",
  "healthCheckInterval": 5000,
  "fallbackServer": "backup.example.com:7001"
}
```

### Batch Configuration

```cpp
class BatchConfig {
private:
    std::vector<ClientConfiguration> clients;
    std::vector<ServerConfiguration> servers;
    
public:
    void loadFromJSON(const std::string& filename) {
        nlohmann::json json;
        std::ifstream file(filename);
        file >> json;
        
        // Load client configurations
        if (json.contains("clients")) {
            for (const auto& clientJson : json["clients"]) {
                clients.push_back(ConfigValidator::validateClientConfig(clientJson));
            }
        }
        
        // Load server configurations
        if (json.contains("servers")) {
            for (const auto& serverJson : json["servers"]) {
                servers.push_back(ConfigValidator::validateServerConfig(serverJson));
            }
        }
    }
    
    void startAll() {
        // Start all servers
        for (auto& serverConfig : servers) {
            Server server;
            server.setConfiguration(serverConfig);
            server.start();
        }
        
        // Start all clients
        for (auto& clientConfig : clients) {
            Client client;
            client.setConfiguration(clientConfig);
            client.connect(clientConfig.peerAddress, clientConfig.peerTcpPort);
        }
    }
};
```

### Runtime Configuration Updates

```cpp
class ConfigurableServer {
private:
    ServerConfiguration config;
    std::mutex configMutex;
    std::thread configMonitor;
    
public:
    void startConfigMonitor() {
        configMonitor = std::thread([this]() {
            while (running) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                
                // Check for config file updates
                std::ifstream file("server_config.json");
                if (file.good()) {
                    nlohmann::json json;
                    file >> json;
                    
                    ServerConfiguration newConfig;
                    try {
                        newConfig = ConfigValidator::validateServerConfig(json);
                        
                        // Apply new configuration safely
                        {
                            std::lock_guard<std::mutex> lock(configMutex);
                            log_info("Updating configuration");
                            applyConfig(newConfig);
                            config = newConfig;
                        }
                        
                    } catch (const std::exception& e) {
                        log_error("Invalid config update: %s", e.what());
                    }
                }
            }
        });
    }
    
    void applyConfig(const ServerConfiguration& newConfig) {
        // Apply new configuration safely
        // Note: Some configuration changes may require restart
        if (newConfig.maxConnections != config.maxConnections) {
            // Restart server with new max connections
            restartWithMaxConnections(newConfig.maxConnections);
        }
        
        // Update log level
        if (newConfig.logLevel != config.logLevel) {
            log_setLogLevel(newConfig.logLevel);
        }
    }
};
```

## Production Configuration

### Production Server Configuration

```json
{
  "tcpPort": 7001,
  "maxConnections": 1000,
  "backlog": 1024,
  "recvBufferSize": 1048576,
  "sendBufferSize": 1048576,
  "idleTimeout": 300000,
  "threadPoolSize": 16,
  "logLevel": "INFO",
  "logFile": "/var/log/tcpudp/server.log",
  "enablePerfCounters": true,
  "enableMetrics": true,
  "metricsPort": 8080,
  "enableHealthCheck": true,
  "healthCheckInterval": 10000,
  "maxMessageSize": 1048576,
  "enableRateLimit": true,
  "rateLimitPerSecond": 10000,
  "enableCompression": true,
  "compressionThreshold": 1024,
  "tlsEnabled": false,
  "certFile": "",
  "keyFile": ""
}
```

### Production Client Configuration

```json
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 7001,
  "peerAddress": "load-balancer.example.com",
  "clientId": 1,
  "connectTimeout": 10000,
  "reconnectDelay": 2000,
  "maxReconnectAttempts": 10,
  "recvBufferSize": 1048576,
  "sendBufferSize": 1048576,
  "virtualChannelConnections": 16,
  "sendQueueThreshold": 1000,
  "logLevel": "WARNING",
  "logFile": "/var/log/tcpudp/client.log",
  "enablePerfCounters": true,
  "enableRetransmission": true,
  "maxRetransmissions": 3,
  "ackTimeout": 1000,
  "enableHeartbeat": true,
  "heartbeatInterval": 30000,
  "enableEncryption": true,
  "encryptionKey": "your-256-bit-key-here"
}
```

### Docker Configuration

```dockerfile
# Dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    libboost-all-dev \
    cmake \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
COPY . /app
WORKDIR /app

# Build
RUN mkdir -p Build && \
    cd Build && \
    cmake .. -G Ninja && \
    ninja

# Copy config
COPY config/ /app/config/

# Expose port
EXPOSE 7001

# Run server
CMD ["./Build/server/server", "--config", "/app/config/production.json"]
```

### Kubernetes Configuration

```yaml
# deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: tcpudp-server
spec:
  replicas: 3
  selector:
    matchLabels:
      app: tcpudp-server
  template:
    metadata:
      labels:
        app: tcpudp-server
    spec:
      containers:
      - name: server
        image:/tcpudp:latest
        ports:
        - containerPort: 7001
        env:
        - name: LOG_LEVEL
          value: "INFO"
        - name: MAX_CONNECTIONS
          value: "500"
        resources:
          requests:
            memory: "256Mi"
            cpu: "250m"
          limits:
            memory: "512Mi"
            cpu: "500m"
        livenessProbe:
          tcpSocket:
            port: 7001
          initialDelaySeconds: 30
          periodSeconds: 10
        readinessProbe:
          tcpSocket:
            port: 7001
          initialDelaySeconds: 5
          periodSeconds: 5
```

## Troubleshooting

### Common Configuration Issues

#### 1. Port Already in Use

**Error**: `bind: Address already in use`

**Solution**:
```bash
# Check what's using the port
netstat -tulnp | grep :7001
lsof -i :7001

# Kill the process
kill -9 <PID>

# Or use a different port
./server --port 7002
```

#### 2. Invalid Configuration File

**Error**: `Invalid configuration: missing required field`

**Solution**:
```bash
# Validate JSON syntax
python -m json.tool config.json

# Check for required fields
cat config.json | grep -E "(peerAddress|peerTcpPort)"
```

#### 3. Buffer Size Issues

**Error**: `Invalid buffer size: must be between 1024 and 1048576`

**Solution**:
```json
{
  "recvBufferSize": 65536,
  "sendBufferSize": 65536
}
```

#### 4. Connection Timeouts

**Error**: `Connection timeout after 5000ms`

**Solution**:
```json
{
  "connectTimeout": 10000,
  "reconnectDelay": 2000,
  "maxReconnectAttempts": 10
}
```

### Configuration Debug Mode

```cpp
// Enable debug configuration
void enableDebugConfig(ClientConfiguration& config) {
    config.logLevel = DEBUG;
    config.enablePerfCounters = true;
    config.sendQueueThreshold = 100;  // Lower threshold for monitoring
    
    // Add debug callbacks
    client.setDebugCallback([](const std::string& message) {
        log_debug("Debug: %s", message.c_str());
    });
}

// Usage
ClientConfiguration config = ConfigValidator::validateClientConfig("config.json");
enableDebugConfig(config);
```

### Configuration Versioning

```json
{
  "version": "1.0.0",
  "schema": "tcpudp-config-v1",
  "generated": "2024-01-01T00:00:00Z",
  
  "configuration": {
    "tcpPort": 7001,
    "maxConnections": 100
  }
}
```

### Migration Guide

#### Version 0.x to 1.0 Migration

```json
// Old format (v0.x)
{
  "port": 7001,
  "max_conn": 100
}

// New format (v1.0)
{
  "version": "1.0.0",
  "configuration": {
    "tcpPort": 7001,
    "maxConnections": 100
  }
}
```

**Migration Script**:

```python
import json

def migrate_config(old_file, new_file):
    with open(old_file) as f:
        old_config = json.load(f)
    
    # Map old keys to new keys
    mapping = {
        'port': 'tcpPort',
        'max_conn': 'maxConnections',
        'recv_buf': 'recvBufferSize',
        'send_buf': 'sendBufferSize'
    }
    
    new_config = {
        'version': '1.0.0',
        'schema': 'tcpudp-config-v1',
        'generated': '2024-01-01T00:00:00Z',
        'configuration': {}
    }
    
    for old_key, new_key in mapping.items():
        if old_key in old_config:
            new_config['configuration'][new_key] = old_config[old_key]
    
    with open(new_file, 'w') as f:
        json.dump(new_config, f, indent=2)
```