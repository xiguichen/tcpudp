#pragma once

enum class CommunicateStates {
    STATE_SYNC_SENT = 0,
    STATE_BIND_SENT = 1,
    STATE_ERROR = 2,
    STATE_DONE = 3
};
