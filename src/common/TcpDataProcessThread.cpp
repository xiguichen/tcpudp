#include "TcpDataProcessThread.h"
#include "Socket.h"
#include "Log.h"
#include <format>

using namespace Logger;

std::shared_ptr<std::vector<char>> TcpDataProcessThread::read()
{

    // Read 4 bytes for the length of the data
    int length;
    auto result = RecvTcpDataWithSize(this->_socketFd, &length, sizeof(length), 0, sizeof(length));
    if(result < 0)
    {
        Log::getInstance().error("Failed to read data length from TCP socket");
        return nullptr;
    }
    if(result < sizeof(length))
    {
        Log::getInstance().error("Received less data than expected for length");
        return nullptr;
    }
    length = ntohl(length); // Convert from network byte order to host byte order

    // Read the actual data
    auto data = std::make_shared<std::vector<char>>(length);

    result = RecvTcpDataWithSize(this->_socketFd, data->data(), length, 0, length);

    if(result < 0)
    {
        Log::getInstance().error("Failed to read data from TCP socket");
        return nullptr;
    }

    if(result < length)
    {
        Log::getInstance().error("Received less data than expected");
        return nullptr;
    }

    Log::getInstance().info(std::format("Received {} bytes from TCP socket", length));

    return data;
}



