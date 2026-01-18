#include "VcManager.h"

void VcManager::Add(uint32_t clientId, VirtualChannelSp vc)
{
    vcs[clientId] = vc;
}

void VcManager::Remove(uint32_t clientId)
{
    vcs.erase(clientId);
}

