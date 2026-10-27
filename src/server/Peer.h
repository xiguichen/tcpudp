#pragma once

// A peer is a remote entity of a virtual channel connection
// One peer is associated with one or more connection
#include <Socket.h>
#include <VcProtocol.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Peer
{
  public:
    // default constructor
    Peer() = default;

    Peer(uint32_t clientId);

    Peer &operator=(const Peer &other);

    // <summary>
    // Get the client ID of the peer
    // </summary>
    uint32_t GetClientId() const;

    // <summary>
    // Add a TCP socket to the peer
    // </summary>
    void AddSocket(SocketFd socket);

    // <summary>
    // Remove all TCP sockets from the peer
    // </summary>
    void RemoveAllSockets();

    // <summary>
    // Get the number of TCP sockets associated with the peer
    // </summary>
    size_t GetSocketCount();

    // <summary>
    // Set the UDP socket for the peer
    // </summary>
    void SetUdpSocket(SocketFd socket);

    // <summary>
    // Set the UDP address for the peer
    // </summary>
    void SetUdpAddress(const struct sockaddr_in &addr);

    // <summary>
    // Take ownership of the accumulated TCP sockets.
    // Used when they become owned by a virtual channel; the peer must not close
    // them afterwards. Returned by value so a concurrent AddSocket() cannot
    // invalidate the caller's snapshot.
    // </summary>
    std::vector<SocketFd> TakeSockets();

    // Timestamp of the last socket accumulation activity. Used to discard a
    // half-built peer whose client vanished mid-handshake and later reconnects
    // from scratch (otherwise stale FDs are spliced into the new VC).
    std::chrono::steady_clock::time_point GetLastActivity() const;

    // <summary>
    // Get the UDP socket associated with the peer
    // </summary>
    SocketFd GetUdpSocket() const;

  private:
    uint32_t clientId;
    std::vector<SocketFd> sockets;
    SocketFd udpSocket = -1;
    struct sockaddr_in udpAddress;
    mutable std::mutex socketMutex;
    std::chrono::steady_clock::time_point lastActivity{std::chrono::steady_clock::now()};
};

class PeerManager
{
  public:
    // <summary>
    // Get the peer by client ID
    // </summary>
    static Peer *GetPeerById(uint32_t clientId);

    // Lifetime-safe variant: the returned shared_ptr keeps the Peer alive even
    // if a disconnect callback removes it from the registry concurrently.
    static std::shared_ptr<Peer> GetPeerShared(uint32_t clientId);

    // <summary>
    // Add a new peer by client ID
    // </summary>
    static void AddPeer(uint32_t clientId);

    // <summary>
    // Remove a peer by client ID
    // </summary>
    static void RemovePeer(uint32_t clientId);

    // <summary>
    // Close all sockets and clear all peers
    // </summary>
    static void CloseAllSockets();

  private:
    // shared_ptr keeps every Peer at a stable address when the map rehashes
    // (a plain value map invalidated raw Peer* handed out by GetPeerById()
    // whenever an unrelated client connected/disconnected) and lets callers
    // take a strong reference for the duration of their work.
    static std::unordered_map<uint32_t, std::shared_ptr<Peer>> peers;
    static std::mutex peersMutex;
};
