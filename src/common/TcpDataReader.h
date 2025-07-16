#pragma once

#include "IDataReader.h"
#include "Socket.h"


class TcpDataReader: public IDataReader
{

    virtual std::shared_ptr<std::vector<char>> readData() override;
    // Checks if there is more data to read
    virtual bool hasMoreData() const override;

  private:

    SocketFd fd;
    bool _hasMoreData;
};
