#include "VcManager.h"
#include <vector>

void VcManager::Add(uint32_t clientId, VirtualChannelSp vc)
{
    // Thread-safe add/update
    std::lock_guard<std::mutex> lock(vcsMutex);
    vcs[clientId] = vc;
}

void VcManager::Remove(uint32_t clientId)
{
    // Thread-safe remove
    std::lock_guard<std::mutex> lock(vcsMutex);
    vcs.erase(clientId);
}

bool VcManager::Exists(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(vcsMutex);
    return vcs.find(clientId) != vcs.end();
}

VirtualChannelSp VcManager::Get(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(vcsMutex);
    auto it = vcs.find(clientId);
    if (it != vcs.end())
        return it->second;
    return nullptr;
}

void VcManager::CloseAll()
{
    std::vector<VirtualChannelSp> toClose;
    {
        std::lock_guard<std::mutex> lock(vcsMutex);
        toClose.reserve(vcs.size());
        for (auto &pair : vcs)
            toClose.push_back(pair.second);
        vcs.clear();
    }

    // Close outside the lock: virtual channel teardown joins threads and its
    // disconnect callback calls back into VcManager::Exists()/Remove().
    for (auto &vc : toClose)
    {
        if (vc)
            vc->close();
    }
}

