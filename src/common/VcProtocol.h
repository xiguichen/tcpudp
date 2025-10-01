#pragma once

#include <cstdint>

// 1 byte pack structures
#pragma pack(push, 1)

enum class VcPacketType : uint8_t {
  DATA = 0x00,
  ACK = 0x01
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

struct VCAckPacket
{
    VCHeader header;
};

const uint32_t VC_MIN_DATA_PACKET_SIZE = sizeof(VCDataPacket);

 // 1 byte type + 8 bytes messageId
const uint32_t VC_MIN_ACK_PACKET_SIZE = sizeof(VCAckPacket);

 // Max size of the data payload
const uint16_t VC_MAX_DATA_PAYLOAD_SIZE = 1400;

#pragma pack(pop)
