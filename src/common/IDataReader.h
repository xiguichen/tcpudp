#pragma once

#include <memory>
#include <vector>


class IDataReader {
public:
    virtual ~IDataReader() = default;

    // Reads data from the source and returns it as a string
    virtual std::shared_ptr<std::vector<char>> readData() = 0;

    // Checks if there is more data to read
    virtual bool hasMoreData() const = 0;

};
