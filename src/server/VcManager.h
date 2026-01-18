#pragma once
#include "VirtualChannel.h"
#include <cstdint>
#include <map>

class VcManager
{
public:

    static VcManager &getInstance()
    {
        static VcManager instance;
        return instance;
    }

    VcManager(const VcManager &) = delete;
    VcManager &operator=(const VcManager &) = delete;

    void Add(uint32_t clientId, VirtualChannelSp vc);

    void Remove(uint32_t clientId);

  private:
    VcManager() = default;
    ~VcManager() = default;

    std::map<uint32_t, VirtualChannelSp> vcs;
};
