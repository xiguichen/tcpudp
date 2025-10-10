#include "Server.h"
#include "Configuration.h"
#include "Socket.h"
#include <format>
#include <Log.h>

void Server::Listen()
{

    serverSocket = CreateSocket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        log_error("Failed to create socket");
        return;
    }

    auto address = Configuration::getInstance()->getSocketAddress();
    auto port = Configuration::getInstance()->getPortNumber();
    log_info(std::format("Server listening on {}:{}", address, port));

    // Bind the socket and start listening
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(address.c_str());
    serverAddr.sin_port = htons(port);
    if (SocketBind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        log_error("Failed to bind socket");
        SocketClose(serverSocket);
        return;
    }

    if (SocketListen(serverSocket, SOMAXCONN) < 0) {
        log_error("Failed to listen on socket");
        SocketClose(serverSocket);
        return;
    }

    log_info("Server is now listening for connections");

}

void Server::AcceptConnections()
{
    while(running) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        // Accept connection blocking
        SocketFd clientSocket = SocketAccept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            log_error("Failed to accept connection");
            continue;
        }

        // Log client connection
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        log_info(std::format("Accepted connection from {}:{}", clientIP, ntohs(clientAddr.sin_port)));

    }
}

