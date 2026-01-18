#include "Peer.h"
#include "Log.h"
#include <format>

std::unordered_map<uint32_t, Peer> PeerManager::peers;

uint32_t Peer::GetClientId() const
{
    return clientId;
}

Peer &Peer::operator=(const Peer &other)
{
    if (this != &other)
    {
        this->clientId = other.clientId;
        this->sockets = other.sockets;
        this->udpSocket = other.udpSocket;
        this->udpAddress = other.udpAddress;
    }
    return *this;
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
    log_info(std::format("Removing all sockets from peer with client ID {}", clientId));
    sockets.clear();
}

Peer *PeerManager::GetPeerById(uint32_t clientId)
{
    auto it = peers.find(clientId);
    if (it != peers.end())
    {
        return &it->second;
    }
    return nullptr;
}

void PeerManager::AddPeer(uint32_t clientId)
{
    if (peers.find(clientId) == peers.end())
    {
        Peer p = Peer(clientId);
        peers[clientId] = p;
    }
}

void PeerManager::RemovePeer(uint32_t clientId)
{
    peers.erase(clientId);
}

void PeerManager::CloseAllSockets()
{
    for (auto &pair : peers)
    {
        Peer &peer = pair.second;
        for (SocketFd socket : peer.GetSockets())
        {
            log_info(std::format("Closing socket {} for peer with client ID {}", socket, peer.GetClientId()));
            SocketClose(socket);
        }
        SocketClose(peer.GetUdpSocket());
    }
    peers.clear();
}

Peer::Peer(uint32_t clientId) : clientId(clientId)
{
}

