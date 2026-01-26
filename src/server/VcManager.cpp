#include "VcManager.h"

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

