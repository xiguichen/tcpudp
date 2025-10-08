#pragma once

#include <cstdint>

// 1 byte pack structures
#pragma pack(push, 1)

enum class VcPacketType : uint8_t {
  DATA = 0x00,
};

struct VCHeader
{
    VcPacketType type;
    uint64_t messageId;
};

struct VCDataPacket
{
    VCHeader header;
    uint16_t dataLength;
    uint8_t data[];
};

const uint32_t VC_MIN_DATA_PACKET_SIZE = sizeof(VCDataPacket);

 // Max size of the data payload
const uint16_t VC_MAX_DATA_PAYLOAD_SIZE = 1400;

#pragma pack(pop)
