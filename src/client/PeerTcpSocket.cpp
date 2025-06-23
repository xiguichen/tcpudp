#include "PeerTcpSocket.h"
#include <Log.h>
#include <Socket.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <Protocol.h>
#include <cstring>
#include <format>
#include <vector>
#include <Protocol.h>
#include <message.pb.h>

using namespace Logger;

PeerTcpSocket::PeerTcpSocket(const std::string &address, int port, uint32_t clientId) 
  : clientId(clientId), state(ConnectionState::DISCONNECTED) {
  // Create socket with non-blocking mode
  socketFd = CreateSocket(AF_INET, SOCK_STREAM, 0);
  if (socketFd < 0) {
    Log::getInstance().error("Failed to create TCP socket");
    throw std::runtime_error("Failed to create TCP socket");
  }
  
  
  this->connect(address, port);

  // Set socket to non-blocking mode
  if (SetSocketNonBlocking(socketFd) < 0) {
    Log::getInstance().error("Failed to set socket to non-blocking mode");
    SocketClose(socketFd);
    throw std::runtime_error("Failed to set socket to non-blocking mode");
  }

}

void PeerTcpSocket::connect(const std::string &address, int port) {
  if (state != ConnectionState::DISCONNECTED) {
    Log::getInstance().warning("Attempting to connect when socket is not in DISCONNECTED state");
    return;
  }

  state = ConnectionState::CONNECTING;
  peerAddress.sin_family = AF_INET;
  peerAddress.sin_port = htons(port);
  inet_pton(AF_INET, address.c_str(), &peerAddress.sin_addr);
  
  // Connect with timeout
  // int result = SocketConnectNonBlocking(socketFd, (struct sockaddr *)&peerAddress, sizeof(peerAddress), 5000); // 5 seconds timeout
  // if (result < 0) {
  //   Log::getInstance().error(std::format("Failed to connect to {}:{}", address, port));
  //   SocketLogLastError();
  //   close();
  //   throw std::runtime_error("Failed to connect to peer");
  // }
  int result = SocketConnect(socketFd, (struct sockaddr *)&peerAddress, sizeof(peerAddress));
  if(result)
  {
      Log::getInstance().info("something wrong happend during connect to remote server");
  }


  isConnected = true;
  state = ConnectionState::CONNECTED;
  Log::getInstance().info(std::format("Connected to peer at {}:{}", address, port));
}

void PeerTcpSocket::send(const std::vector<char> &data) {
  // Check if we're authenticated before sending data
  if (state != ConnectionState::AUTHENTICATED) {
    Log::getInstance().error("Cannot send data: Socket not authenticated");
    throw std::runtime_error("Cannot send data: Socket not authenticated");
  }

  // log the data size
  Log::getInstance().info(std::format("vector size: {}", data.size()));


  // Send data with timeout
  ssize_t result = SendTcpDataNonBlocking(socketFd, data.data(), data.size(), 0, 5000); // 5 seconds timeout
  if (result != static_cast<ssize_t>(data.size())) {
    Log::getInstance().error(std::format("Failed to send data, sent {} of {} bytes", 
                                        (result > 0 ? result : 0), data.size()));
    if (result < 0) {
      SocketLogLastError();
    }
    throw std::runtime_error("Failed to send data to peer");
  }
}

std::shared_ptr<std::vector<char>> PeerTcpSocket::receive() {
  // Check if we're connected
  if (state != ConnectionState::CONNECTED && state != ConnectionState::AUTHENTICATED) {
    Log::getInstance().error("Cannot receive data: Socket not connected");
    throw std::runtime_error("Cannot receive data: Socket not connected");
  }

  // Check if socket is readable with timeout
  if (!IsSocketReadable(socketFd, 1000)) { // 1 second timeout
    Log::getInstance().debug("Socket not readable within timeout");
    return {};
  }

  // Receive header with timeout
  UvtHeader header;
  Log::getInstance().info("recv header");
  ssize_t lengthBytesReceived = RecvTcpDataWithSizeNonBlocking(socketFd, &header, HEADER_SIZE, 0, HEADER_SIZE, 5000); // 5 seconds timeout
  
  if (lengthBytesReceived != HEADER_SIZE) {
    if (lengthBytesReceived == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().info("Timeout while receiving header");
    } else if (lengthBytesReceived == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().info("Would block while receiving header");
    } else if (lengthBytesReceived == SOCKET_ERROR_CLOSED) {
      Log::getInstance().error("Connection closed by peer");
    } else {
      Log::getInstance().error(std::format("Failed to receive header, error: {}", lengthBytesReceived));
      SocketLogLastError();
    }
    return {};
  }

  // Convert message length from network byte order to host byte order
  uint16_t messageLength = ntohs(header.size);
  Log::getInstance().info(std::format(
      "TCP -> Queue: Data ID {} , Data Length: {}, Data Checksum: {}",
      header.id, messageLength, header.checksum));

  // Prepare a buffer to receive the actual message
  auto buffer = std::make_shared<std::vector<char>>(messageLength);

  // Receive message body with timeout
  ssize_t bytesReceived = RecvTcpDataWithSizeNonBlocking(socketFd, buffer->data(), messageLength, 0, messageLength, 5000); // 5 seconds timeout
  
  if (bytesReceived != static_cast<ssize_t>(messageLength)) {
    if (bytesReceived == SOCKET_ERROR_TIMEOUT) {
      Log::getInstance().info("Timeout while receiving message body");
    } else if (bytesReceived == SOCKET_ERROR_WOULD_BLOCK) {
      Log::getInstance().info("Would block while receiving message body");
    } else if (bytesReceived == SOCKET_ERROR_CLOSED) {
      Log::getInstance().error("Connection closed by peer");
    } else {
      Log::getInstance().error(std::format("Failed to receive message body, received {} of {} bytes", 
                                          (bytesReceived > 0 ? bytesReceived : 0), messageLength));
      SocketLogLastError();
    }
    return {};
  }
  
  // Verify checksum
  uint8_t checksum = xor_checksum((uint8_t *)buffer->data(), messageLength);
  if (checksum != header.checksum) {
    Log::getInstance().error("Tcp -> Queue: Checksum verify failed");
    return {};
  }

  return buffer;
}

PeerTcpSocket::~PeerTcpSocket() { 
  close();
}

void PeerTcpSocket::close() {
  
  Log::getInstance().info("PeerTcpSocket close");
  if (socketFd >= 0) {
    SocketClose(socketFd);
    socketFd = -1;
    isConnected = false;
    state = ConnectionState::DISCONNECTED;
    connectionId = 0;
  }
}

void PeerTcpSocket::sendHandshake() { 
  // Check if we're in the correct state
  if (state != ConnectionState::CONNECTED) {
    Log::getInstance().error("Cannot send handshake: Socket not connected");
    throw std::runtime_error("Cannot send handshake: Socket not connected");
  }

  // Create and populate the MsgBind structure
  MsgBind msgBind;
  msgBind.clientId = this->clientId;

  // Sync sync;
  // sync.set_clientid(this->clientId);
  // sync.set_sequencenumber(0); // Initial sequence number
  //
  // // Create a buffer to hold the serialized MsgBind structure
  // auto buffer = std::make_shared<std::vector<char>>(sync.ByteSizeLong() + sizeof(int));
  //
  auto buffer = std::make_shared<std::vector<char>>(sizeof(MsgBind));
  
  std::memcpy(buffer->data(), &msgBind, sizeof(MsgBind));
  
  // Send handshake with timeout
  ssize_t result = SendTcpDataNonBlocking(socketFd, buffer->data(), sizeof(MsgBind), 0, 5000); // 5 seconds timeout
  if (result != sizeof(MsgBind)) {
    Log::getInstance().error(std::format("Failed to send handshake, sent {} bytes instead of {}", 
                                        result, sizeof(MsgBind)));
    if (result < 0) {
      SocketLogLastError();
    }
    throw std::runtime_error("Failed to send handshake");
  }
  
  Log::getInstance().info(std::format("Handshake sent successfully with client ID: {}", this->clientId));
}

void PeerTcpSocket::completeHandshake() {
  // Send the initial handshake
  sendHandshake();
  
  // Wait for the server's response
  Log::getInstance().info("Waiting for handshake response...");
  
  // Check if socket is readable with timeout (longer timeout for handshake)
  if (!IsSocketReadable(socketFd, 5000)) { // 5 second timeout
    Log::getInstance().error("Timeout waiting for handshake response");
    throw std::runtime_error("Timeout waiting for handshake response");
  }
  
  // Receive the MsgBindResponse with timeout
  std::vector<char> responseBuffer(sizeof(MsgBindResponse));
  ssize_t bytesReceived = RecvTcpDataWithSizeNonBlocking(socketFd, responseBuffer.data(), 
                                                        responseBuffer.size(), 0, 
                                                        sizeof(MsgBindResponse), 5000); // 5 seconds timeout
  
  if (bytesReceived != sizeof(MsgBindResponse)) {
    Log::getInstance().error(std::format("Failed to receive handshake response, received {} of {} bytes", 
                                        (bytesReceived > 0 ? bytesReceived : 0), sizeof(MsgBindResponse)));
    if (bytesReceived < 0) {
      SocketLogLastError();
    }
    throw std::runtime_error("Failed to receive handshake response");
  }
  
  // Extract the connection ID
  MsgBindResponse response;
  std::memcpy(&response, responseBuffer.data(), sizeof(MsgBindResponse));
  
  // Store the connection ID
  this->connectionId = response.connectionId;
  
  // Update the state to authenticated
  state = ConnectionState::AUTHENTICATED;
  
  Log::getInstance().info(std::format("Handshake completed successfully. Connection ID: {}", this->connectionId));
}
