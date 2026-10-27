#include "Peer.h"
#include "Log.h"
#include "VcManager.h"
#include <format>

std::unordered_map<uint32_t, std::shared_ptr<Peer>> PeerManager::peers;
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
    // Only successful accumulation counts as activity. Refreshing this on a
    // rejected socket would keep a stale peer alive forever.
    lastActivity = std::chrono::steady_clock::now();
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

std::vector<SocketFd> Peer::TakeSockets()
{
    std::lock_guard<std::mutex> lock(socketMutex);
    auto taken = std::move(sockets);
    sockets.clear();
    return taken;
}

std::chrono::steady_clock::time_point Peer::GetLastActivity() const
{
    std::lock_guard<std::mutex> lock(socketMutex);
    return lastActivity;
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
        return it->second.get();
    }
    return nullptr;
}

std::shared_ptr<Peer> PeerManager::GetPeerShared(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(peersMutex);
    auto it = peers.find(clientId);
    return it != peers.end() ? it->second : nullptr;
}

void PeerManager::AddPeer(uint32_t clientId)
{
    std::lock_guard<std::mutex> lock(peersMutex);
    auto it = peers.find(clientId);
    if (it == peers.end())
    {
        peers[clientId] = std::make_shared<Peer>(clientId);
    }
    else
    {
        // Only flush stale sockets if the virtual channel exists.
        // If VC doesn't exist, we're still building up connections for a new VC
        // (e.g., 4 TCP connections for one virtual channel).
        // We should only treat it as a reconnect when the VC was actually removed.
        if (VcManager::getInstance().Exists(clientId))
        {
            log_info(std::format("Peer {} reconnecting, flushing stale sockets", clientId));
            it->second->RemoveAllSockets();
        }
        else if (it->second->GetSocketCount() >= VC_TCP_CONNECTIONS)
        {
            // A full accumulation with no VC means the previous channel's sockets
            // never became a live VC (e.g. an explicit close raced the accept
            // loop). Keeping them would reject every socket of the next channel.
            // The accept loop is single-threaded, so a normal in-progress
            // accumulation cannot be full here without having created its VC.
            log_info(std::format("Peer {} has a full accumulation with no VC, flushing", clientId));
            it->second->RemoveAllSockets();
        }
        else
        {
            // The client vanished halfway through building its 32-socket channel
            // (crash/network loss/restart). Those FDs are dead but would otherwise
            // be spliced into the next channel, leaving it short of live slots and
            // triggering the "initial connect while a VC exists" teardown loop.
            static constexpr auto STALE_ACCUMULATION_TIMEOUT = std::chrono::seconds(30);
            if (std::chrono::steady_clock::now() - it->second->GetLastActivity() >
                STALE_ACCUMULATION_TIMEOUT)
            {
                log_info(std::format("Peer {} has stale half-built sockets, flushing", clientId));
                it->second->RemoveAllSockets();
            }
        }
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
        Peer &peer = *pair.second;
        for (SocketFd socket : peer.TakeSockets())
        {
            log_info(std::format("Closing socket {} for peer with client ID {}", socket, peer.GetClientId()));
            SocketClose(socket);
        }
        SocketFd udp = peer.GetUdpSocket();
        if (udp != -1)
            SocketClose(udp);
    }
    peers.clear();
}

Peer::Peer(uint32_t clientId) : clientId(clientId)
{
}

