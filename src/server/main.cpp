#include "SocketManager.h"



int main(int argc, char* argv[]) {

    SocketManager socketManager;
    socketManager.createSocket();
    socketManager.bindToPort(8080);
    socketManager.listenForConnections();
    while (true) {
        socketManager.acceptConnection();
    }

}
