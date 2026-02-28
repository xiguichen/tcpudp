#include "Peer.h"
#include "Log.h"
#include <format>

std::unordered_map<uint32_t, Peer> PeerManager::peers;
std::mutex PeerManager::peersMutex;

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
    std::lock_guard<std::mutex> lock(socketMutex);
    if (sockets.size() >= VC_TCP_CONNECTIONS)
    {
        log_error(std::format("Peer {} already has {} sockets (max {}), closing excess socket {}",
                              clientId, sockets.size(), VC_TCP_CONNECTIONS, socket));
        SocketClose(socket);
        return;
    }
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
    for (SocketFd fd : sockets)
    {
        log_info(std::format("Closing socket {} for peer with client ID {}", fd, clientId));
        SocketClose(fd);
    }
    sockets.clear();
}

Peer *PeerManager::GetPeerById(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(peersMutex);
    auto it = peers.find(clientId);
    if (it != peers.end())
    {
        return &it->second;
    }
    return nullptr;
}

void PeerManager::AddPeer(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(peersMutex);
    auto it = peers.find(clientId);
    if (it == peers.end())
    {
        peers[clientId] = Peer(clientId);
    }
    else
    {
        // Peer already exists — client is reconnecting with stale state.
        // Close and flush any leftover sockets before accepting new ones.
        log_info(std::format("Peer {} already exists on reconnect, flushing stale sockets", clientId));
        it->second.RemoveAllSockets();
    }
}

void PeerManager::RemovePeer(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(peersMutex);
    peers.erase(clientId);
}

void PeerManager::CloseAllSockets()
{
    std::lock_guard<std::mutex> lock(peersMutex);
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

