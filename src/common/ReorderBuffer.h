#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

/// Reorder window for the virtual channel's ordered message stream.
///
/// Slots are indexed by messageId, so the in-order path is O(1) and, unlike
/// std::map, does not allocate/free a tree node per packet. The window starts
/// at nextId() and grows on demand when a packet is further ahead than the
/// current capacity, preserving the unbounded-jitter semantics of the previous
/// map while the steady-state working set needs no allocation beyond the
/// payloads themselves.
class ReorderBuffer
{
  public:
    struct Item
    {
        uint64_t messageId{0};
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex{-1};
    };

    explicit ReorderBuffer(size_t initialCapacity = 4096)
        : capacity_(initialCapacity == 0 ? 1 : initialCapacity), slots_(capacity_)
    {
    }

    /// Returns false for ids that were already delivered or are already
    /// buffered (duplicates); true when the packet was stored.
    bool insert(uint64_t messageId, std::shared_ptr<std::vector<char>> data, int sourceConnIndex)
    {
        if (messageId < nextId_)
            return false;

        const uint64_t distance = messageId - nextId_;
        if (distance >= capacity_)
        {
            // Guard against a corrupt id forcing an enormous allocation. The
            // packet is dropped; the reorder timeout/resend path recovers.
            if (distance >= MAX_CAPACITY)
                return false;
            growTo(distance + 1);
        }

        Slot &slot = slots_[messageId % capacity_];
        if (slot.valid && slot.messageId == messageId)
            return false;

        if (slot.valid)
        {
            // Defensive: a slot left over from an older wrap that drain() or
            // skipTo() did not clear. Replace it instead of double-counting.
            --size_;
        }

        slot.messageId = messageId;
        slot.data = std::move(data);
        slot.sourceConnIndex = sourceConnIndex;
        slot.valid = true;
        ++size_;
        return true;
    }

    /// Appends every consecutively buffered item starting at nextId() to out
    /// and advances the cursor past them. Returns the number of items appended.
    size_t drain(std::vector<Item> &out)
    {
        size_t drained = 0;
        while (true)
        {
            Slot &slot = slots_[nextId_ % capacity_];
            if (!slot.valid || slot.messageId != nextId_)
                break;

            out.push_back(Item{nextId_, std::move(slot.data), slot.sourceConnIndex});
            resetSlot(slot);
            --size_;
            ++nextId_;
            ++drained;
        }
        return drained;
    }

    /// Drops buffered ids in [nextId(), messageId) and moves the cursor to
    /// messageId. Used by the reorder-timeout recovery path.
    void skipTo(uint64_t messageId)
    {
        if (messageId <= nextId_)
            return;

        // Only slots inside the current window can hold data; walking further
        // would just scan empty space.
        const uint64_t limit = nextId_ + capacity_;
        const uint64_t end = messageId < limit ? messageId : limit;
        for (uint64_t id = nextId_; id < end; ++id)
        {
            Slot &slot = slots_[id % capacity_];
            if (slot.valid && slot.messageId == id)
            {
                resetSlot(slot);
                --size_;
            }
        }
        nextId_ = messageId;
    }

    uint64_t nextId() const { return nextId_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear()
    {
        for (auto &slot : slots_)
            resetSlot(slot);
        size_ = 0;
        nextId_ = 0;
    }

    /// Visits buffered items in ascending messageId order.
    template <typename Fn> void forEach(Fn &&fn) const
    {
        for (uint64_t id = nextId_; id - nextId_ < capacity_; ++id)
        {
            const Slot &slot = slots_[id % capacity_];
            if (slot.valid && slot.messageId == id)
                fn(slot.messageId, slot.data, slot.sourceConnIndex);
        }
    }

  private:
    // Upper bound on the reorder window. Realistic gaps are bounded by the
    // resend cache reach (4096 ids); the larger cap only keeps a corrupt id
    // from turning into a multi-gigabyte allocation.
    static constexpr uint64_t MAX_CAPACITY = 1ULL << 20;

    struct Slot
    {
        uint64_t messageId{0};
        std::shared_ptr<std::vector<char>> data;
        int sourceConnIndex{-1};
        bool valid{false};
    };

    static void resetSlot(Slot &slot)
    {
        slot.valid = false;
        slot.messageId = 0;
        slot.data.reset();
        slot.sourceConnIndex = -1;
    }

    void growTo(uint64_t requiredCapacity)
    {
        size_t newCapacity = capacity_;
        while (newCapacity < requiredCapacity)
            newCapacity *= 2;

        std::vector<Slot> grown(newCapacity);
        for (uint64_t id = nextId_; id - nextId_ < capacity_; ++id)
        {
            Slot &slot = slots_[id % capacity_];
            if (!slot.valid || slot.messageId != id)
                continue;
            grown[id % newCapacity] = std::move(slot);
        }
        slots_ = std::move(grown);
        capacity_ = newCapacity;
    }

    size_t capacity_;
    std::vector<Slot> slots_;
    uint64_t nextId_{0};
    size_t size_{0};
};
