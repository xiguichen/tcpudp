#pragma once

#include <cstddef>
#include <cstdint>

struct DataHeader {
    uint16_t size;
    uint8_t id;
    uint8_t checksum;
};

const size_t HEADER_SIZE = sizeof(DataHeader);

uint8_t xor_checksum(const uint8_t *buffer, size_t length);

