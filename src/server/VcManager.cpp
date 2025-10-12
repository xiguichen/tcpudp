#include "VcManager.h"

void VcManager::Add(const std::string &name, VirtualChannelSp vc)
{
    vcs[name] = vc;
}

void VcManager::Remove(const std::string &name)
{
    vcs.erase(name);
}

