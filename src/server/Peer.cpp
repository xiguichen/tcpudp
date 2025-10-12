#include "Peer.h"
#include "Log.h"
#include <format>

std::unordered_map<std::string, Peer> PeerManager::peers;
const std::string &Peer::GetIpAddress() const
{
    return ipAddress;
}

void Peer::AddSocket(SocketFd socket)
{
    sockets.push_back(socket);
}

size_t Peer::GetSocketCount()
{
    std::lock_guard<std::mutex> lock(socketMutex);
    return sockets.size();
}

void Peer::SetUdpSocket(SocketFd socket)
{
    udpSocket = socket;
}

void Peer::SetUdpAddress(const struct sockaddr_in &addr)
{
    udpAddress = addr;
}

const std::vector<SocketFd> &Peer::GetSockets()
{
    std::lock_guard<std::mutex> lock(socketMutex);
    return sockets;
}

SocketFd Peer::GetUdpSocket() const
{
    return udpSocket;
}

void Peer::RemoveAllSockets()
{
    std::lock_guard<std::mutex> lock(socketMutex);
    log_info(std::format("Removing all sockets from peer {}", ipAddress));
    sockets.clear();
}

Peer *PeerManager::GetPeerByIp(const std::string &ipAddress)
{
    auto it = peers.find(ipAddress);
    if (it != peers.end())
    {
        return &it->second;
    }
    return nullptr;
}
void PeerManager::AddPeer(const std::string &ipAddress)
{
    if (peers.find(ipAddress) == peers.end())
    {
        Peer p = Peer(ipAddress);
        peers[ipAddress] = p;
    }
}
void PeerManager::RemovePeer(const std::string &ipAddress)
{
    peers.erase(ipAddress);
}
void PeerManager::CloseAllSockets()
{
    for (auto &pair : peers)
    {
        Peer &peer = pair.second;
        for (SocketFd socket : peer.GetSockets())
        {
            SocketClose(socket);
        }
        SocketClose(peer.GetUdpSocket());
    }
    peers.clear();
}
Peer::Peer(const std::string &ipAddress) : ipAddress(ipAddress)
{
}

Peer &Peer::operator=(const Peer &other)
{
    if (this != &other)
    {
        std::lock_guard<std::mutex> lock(socketMutex);
        ipAddress = other.ipAddress;
        sockets = other.sockets;
        udpSocket = other.udpSocket;
        udpAddress = other.udpAddress;
    }
    return *this;
}

