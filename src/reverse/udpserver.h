#pragma once
#include "config.h"
#include <netinet/in.h>
#include <thread>
#include <vector>

typedef struct _socket {
  int fd;
  struct sockaddr_in addr;
} Socket;

typedef struct UdpReceivedData {
    Socket source;
    std::vector<char> data;
} UdpReceivedData;


class UdpServer {
public:
  UdpServer(UdpServerConfig config);
  ~UdpServer();
  void start();
  void stop();

private:
  void InitSocket();
  void BindSocket();
  void RevertData(char *data, int len);
  void RedirectTraffic(Socket source, Socket target);
  void ReadThread();

  UdpServerConfig _config;
  int _socket_fd;
  struct sockaddr_in _remote_addr;
  std::vector<std::thread> _worker_threads;
  std::vector<UdpReceivedData> _received_datas;
};
