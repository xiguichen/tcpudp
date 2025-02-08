#include "udpserver.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

UdpServer::UdpServer(UdpServerConfig config)
    : _config(config), _socket_fd(-1) {}

UdpServer::~UdpServer() { stop(); }

void UdpServer::start() {
  std::cout << "Starting server" << std::endl;

  InitSocket();
  BindSocket();
  ReadThread();
}

void UdpServer::stop() {
  std::cout << "Stopping server" << std::endl;
  if (_socket_fd != -1) {
    close(_socket_fd);
    _socket_fd = -1;
  }
}

void UdpServer::InitSocket() {
  _socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (_socket_fd < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
}

void UdpServer::BindSocket() {
  struct sockaddr_in local_addr;
  memset(&local_addr, 0, sizeof(local_addr));

  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;
  local_addr.sin_port = htons(_config.localPort);

  if (bind(_socket_fd, (const struct sockaddr *)&local_addr,
           sizeof(local_addr)) < 0) {
    perror("bind failed");
    close(_socket_fd);
    exit(EXIT_FAILURE);
  }
}

void UdpServer::RevertData(char *data, int len) {
    // for each data element, do ~ operation to revert it
    for (int i = 0; i < len; i++) {
        data[i] = ~data[i];
    }
}

void UdpServer::RedirectTraffic(Socket source, Socket target) {
  char buffer[1024];
  struct sockaddr_in source_addr;
  struct sockaddr_in target_addr;
  socklen_t addr_len = sizeof(struct sockaddr_in);

  while (true) {
    int n = recvfrom(source.fd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)&source_addr, &addr_len);
    if (n < 0) {
      perror("recvfrom failed");
      break;
    }

    RevertData(buffer, n);

    if (sendto(target.fd, buffer, n, 0,
               (struct sockaddr *)&target_addr, addr_len) < 0) {
      perror("sendto failed");
      break;
    }
  }
}

void UdpServer::ReadThread()
{
    while(true)
    {
        char buffer[1024];
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        int n = recvfrom(_socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&source_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }
        UdpReceivedData data = {.source = {.fd = _socket_fd, .addr = source_addr}, .data = std::vector<char>(buffer, buffer + n)};
        _received_datas.push_back(data);
    }
}
