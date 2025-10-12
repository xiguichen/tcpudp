#pragma once
#include "VirtualChannel.h"
#include <map>
#include <string>

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

    void Add(const std::string &name, VirtualChannelSp vc);

    void Remove(const std::string &name);

  private:
    VcManager() = default;
    ~VcManager() = default;

    std::map<std::string, VirtualChannelSp> vcs;
};
