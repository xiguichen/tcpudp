#pragma once
#include <vector>

// Decides which VC slot an incoming replacement MsgBind should hot-swap into.
//
// On a per-slot reconnect the client sends both the old connectionId (which the
// server maps to a slot) AND the authoritative slotIndex it is reconnecting. The
// connectionId map can be stale: the client rotates the id locally after every
// reconnect but only ever transmits the PREVIOUS id, so the server's map lags one
// step. On macOS the dead socket is frequently still half-open, so getDeadSlots()
// is empty and the old fallback closed the fresh socket as a "stray", looping
// forever. Trusting the client-provided slotIndex (which the client always knows)
// breaks that loop without any protocol change.
//
// Parameters:
//   connIdSlot  : slot resolved from the connectionId map, or -1 if not found.
//   slotIndex   : slotIndex from the MsgBind (-1 if the client did not provide one).
//   numSlots    : total slots in the VC (valid slotIndex range is [0, numSlots)).
//   deadSlots   : slots the server has independently observed as dead.
// Returns the target slot, or -1 to reject the socket as a stray.
inline int DecideReplaceSlot(int connIdSlot, int slotIndex, int numSlots, const std::vector<int> &deadSlots)
{
    // 1. The connectionId map resolved a slot — trust it (fast path).
    if (connIdSlot >= 0)
        return connIdSlot;
    // 2. Map miss (stale after id rotation). The client authoritatively told us which
    //    slot it is reconnecting, so trust slotIndex even when deadSlots is empty —
    //    this is the macOS half-open case where the server hasn't observed the death.
    if (slotIndex >= 0 && slotIndex < numSlots)
        return slotIndex;
    // 3. No usable slotIndex — fall back to a slot the server has seen die.
    if (!deadSlots.empty())
        return deadSlots[0];
    // 4. Nothing resolvable — reject as a stray.
    return -1;
}
