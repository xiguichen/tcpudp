#pragma once

// A peer is a remote entity of a virtual channel connection
// One peer is associated with one or more connection
#include <Socket.h>
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
    // Get the list of TCP sockets associated with the peer
    // </summary>
    const std::vector<SocketFd> &GetSockets();

    // <summary>
    // Get the UDP socket associated with the peer
    // </summary>
    SocketFd GetUdpSocket() const;

  private:
    uint32_t clientId;
    std::vector<SocketFd> sockets;
    SocketFd udpSocket = -1;
    struct sockaddr_in udpAddress;
    std::mutex socketMutex;
};

class PeerManager
{
  public:
    // <summary>
    // Get the peer by client ID
    // </summary>
    static Peer *GetPeerById(uint32_t clientId);

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
    static std::unordered_map<uint32_t, Peer> peers;
};
