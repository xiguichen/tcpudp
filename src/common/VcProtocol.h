#pragma once

#include <cstddef>
#include <cstdint>

// 1 byte pack structures
#pragma pack(push, 1)

enum class VcPacketType : uint8_t {
  DATA = 0x00,
  RESEND_REQUEST = 0x01,
  RESEND_RESPONSE = 0x02,
  MISSING_NOTIFY = 0x03,
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

struct VCResendRequest
{
    VCHeader header;
    uint64_t missingMessageId;
};

struct VCResendResponse
{
    VCHeader header;
    uint16_t dataLength;
    uint8_t data[];
};

static constexpr size_t VC_MAX_MISSING_IDS_PER_NOTIFY = 64;

struct VCMissingNotify
{
    VCHeader header;
    uint8_t count;
    uint64_t missingIds[VC_MAX_MISSING_IDS_PER_NOTIFY];
};

const uint32_t VC_MIN_DATA_PACKET_SIZE = sizeof(VCDataPacket);
const uint32_t VC_MIN_RESEND_REQUEST_SIZE = sizeof(VCResendRequest);
const uint32_t VC_MIN_RESEND_RESPONSE_SIZE = sizeof(VCResendResponse);
const uint32_t VC_MIN_MISSING_NOTIFY_SIZE = sizeof(VCMissingNotify);

 // Max size of the data payload
const uint16_t VC_MAX_DATA_PAYLOAD_SIZE = 2000;

// TCP Connections per virtual channel
const uint8_t VC_TCP_CONNECTIONS = 32;

// Index of the connection reserved exclusively for resend traffic (last slot).
constexpr uint8_t VC_RESEND_CONN_INDEX = VC_TCP_CONNECTIONS - 1;

// Drop incoming UDP packets when any send queue exceeds this depth.
// Prevents unbounded memory growth when the network can't keep up.
const size_t SEND_QUEUE_DROP_THRESHOLD = 500;

#pragma pack(pop)
