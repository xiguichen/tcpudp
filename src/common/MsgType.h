#pragma once

#include <cstdint>

enum class MsgType: uint8_t {
    MSG_SYNC = 1,
    MSG_ACK = 2,
    MSG_BIND = 3,
    MSG_DATA = 4,
};
