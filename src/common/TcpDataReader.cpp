#include "TcpDataReader.h"

std::shared_ptr<std::vector<char>> TcpDataReader::readData()
{
    try
    {
        // Buffer to read the 32-bit length
        uint32_t length = 0;
        ssize_t bytesRead =
            RecvTcpDataWithSize(fd, reinterpret_cast<char *>(&length), sizeof(length), 0, sizeof(length));

        // Check if reading the length was successful
        if (bytesRead != sizeof(length))
        {
            _hasMoreData = false; // Update status if error occurs
            return nullptr;
        }

        // Convert length to host byte order
        length = ntohl(length);

        // Allocate buffer for the incoming data
        auto data = std::make_shared<std::vector<char>>(length);
        bytesRead = RecvTcpDataWithSize(fd, data->data(), length, 0, length);

        // Check if reading the data was successful
        if (bytesRead != static_cast<ssize_t>(length))
        {
            _hasMoreData = false; // Update status if error occurs
            return nullptr;
        }

        return data; // Return the successfully read data
    }
    catch (...)
    {
        _hasMoreData = false; // Update status if any exception occurs
        return nullptr;
    }
    return nullptr;
}

bool TcpDataReader::hasMoreData() const
{
    return _hasMoreData;
}
