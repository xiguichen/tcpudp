#include "Peer.h"
#include "Socket.h"
#include "VcProtocol.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

// Helper: create a lightweight UDP socket just to get a valid OS file descriptor.
// UDP sockets require no connect/listen and are the cheapest valid FD to obtain.
static SocketFd MakeTestSocket()
{
    return SocketCreate(AF_INET, SOCK_DGRAM, 0);
}

class PeerManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
#ifdef _WIN32
        WSAData wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    void TearDown() override
    {
        // Remove all peers created during a test so tests are independent.
        PeerManager::CloseAllSockets();
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

// ─── AddPeer / GetPeerById basics ────────────────────────────────────────────

TEST_F(PeerManagerTest, AddPeerCreatesNewPeer)
{
    PeerManager::AddPeer(1);
    Peer *peer = PeerManager::GetPeerById(1);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->GetClientId(), 1U);
}

TEST_F(PeerManagerTest, GetPeerByIdReturnsNullForUnknownId)
{
    Peer *peer = PeerManager::GetPeerById(9999);
    EXPECT_EQ(peer, nullptr);
}

TEST_F(PeerManagerTest, AddMultiplePeersAreIndependent)
{
    PeerManager::AddPeer(10);
    PeerManager::AddPeer(20);
    Peer *p10 = PeerManager::GetPeerById(10);
    Peer *p20 = PeerManager::GetPeerById(20);
    ASSERT_NE(p10, nullptr);
    ASSERT_NE(p20, nullptr);
    EXPECT_NE(p10, p20);
    EXPECT_EQ(p10->GetClientId(), 10U);
    EXPECT_EQ(p20->GetClientId(), 20U);
}

// ─── RemovePeer ──────────────────────────────────────────────────────────────

TEST_F(PeerManagerTest, RemovePeerDeletesPeer)
{
    PeerManager::AddPeer(2);
    PeerManager::RemovePeer(2);
    EXPECT_EQ(PeerManager::GetPeerById(2), nullptr);
}

TEST_F(PeerManagerTest, RemovePeerDoesNotAffectOtherPeers)
{
    PeerManager::AddPeer(30);
    PeerManager::AddPeer(31);
    PeerManager::RemovePeer(30);
    EXPECT_EQ(PeerManager::GetPeerById(30), nullptr);
    EXPECT_NE(PeerManager::GetPeerById(31), nullptr);
}

TEST_F(PeerManagerTest, RemoveNonExistentPeerIsNoOp)
{
    // Should not throw or crash
    EXPECT_NO_THROW(PeerManager::RemovePeer(9999));
}

// ─── AddPeer reconnect: stale socket flush ───────────────────────────────────

// AddPeer only flushes stale sockets when a virtual channel already exists
// for that client (true reconnect). During initial connection buildup no VC
// exists yet, so sockets accumulate across AddPeer calls.
TEST_F(PeerManagerTest, AddPeerOnReconnectFlushesSocketCount)
{
    PeerManager::AddPeer(3);
    Peer *peer = PeerManager::GetPeerById(3);
    ASSERT_NE(peer, nullptr);

    SocketFd fd1 = MakeTestSocket();
    SocketFd fd2 = MakeTestSocket();
    ASSERT_GT(fd1, 0);
    ASSERT_GT(fd2, 0);
    peer->AddSocket(fd1);
    peer->AddSocket(fd2);
    EXPECT_EQ(peer->GetSocketCount(), 2U);

    // Client restarts → AddPeer called again with the same ID.
    // Since no VC exists yet, sockets are preserved (still building up).
    PeerManager::AddPeer(3);

    peer = PeerManager::GetPeerById(3);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->GetSocketCount(), 2U);

    // Now remove the peer and re-add (simulating full teardown + reconnect)
    PeerManager::RemovePeer(3);
    PeerManager::AddPeer(3);
    peer = PeerManager::GetPeerById(3);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->GetSocketCount(), 0U);
}

// A peer that is already full but has no VC is a stale accumulation (for
// example, the previous VC was explicitly closed). A new initial connection
// must be able to start a fresh accumulation instead of being rejected forever.
TEST_F(PeerManagerTest, AddPeerFlushesFullStaleAccumulation)
{
    PeerManager::AddPeer(40);
    Peer *peer = PeerManager::GetPeerById(40);
    ASSERT_NE(peer, nullptr);

    for (int i = 0; i < VC_TCP_CONNECTIONS; ++i)
    {
        SocketFd fd = MakeTestSocket();
        ASSERT_NE(fd, static_cast<SocketFd>(-1));
        peer->AddSocket(fd);
    }
    ASSERT_EQ(peer->GetSocketCount(), static_cast<size_t>(VC_TCP_CONNECTIONS));

    PeerManager::AddPeer(40);

    peer = PeerManager::GetPeerById(40);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(peer->GetSocketCount(), 0U)
        << "AddPeer must flush a full peer that has no active VC";
}

// Once the accumulated sockets are handed to a VC, the peer must not keep
// owning them. Otherwise a later "full peer" flush would close fds that the VC
// already owns (possibly after the OS reused those fd numbers).
TEST_F(PeerManagerTest, TakeSocketsTransfersOwnership)
{
    PeerManager::AddPeer(42);
    Peer *peer = PeerManager::GetPeerById(42);
    ASSERT_NE(peer, nullptr);

    SocketFd fd1 = MakeTestSocket();
    SocketFd fd2 = MakeTestSocket();
    ASSERT_NE(fd1, static_cast<SocketFd>(-1));
    ASSERT_NE(fd2, static_cast<SocketFd>(-1));
    peer->AddSocket(fd1);
    peer->AddSocket(fd2);

    auto taken = peer->TakeSockets();
    ASSERT_EQ(taken.size(), 2U);
    EXPECT_EQ(taken[0], fd1);
    EXPECT_EQ(taken[1], fd2);
    EXPECT_EQ(peer->GetSocketCount(), 0U) << "the peer must not retain fds now owned by the VC";

    for (SocketFd fd : taken)
        SocketClose(fd);
}

// A retry that does not add a socket must not reset the stale-accumulation
// timer. Otherwise a client that keeps reconnecting prevents the timeout from
// ever expiring.
TEST_F(PeerManagerTest, AddPeerRetryDoesNotRefreshLastActivity)
{
    PeerManager::AddPeer(41);
    Peer *peer = PeerManager::GetPeerById(41);
    ASSERT_NE(peer, nullptr);

    SocketFd fd = MakeTestSocket();
    ASSERT_NE(fd, static_cast<SocketFd>(-1));
    peer->AddSocket(fd);

    auto before = peer->GetLastActivity();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    PeerManager::AddPeer(41);

    EXPECT_EQ(peer->GetLastActivity(), before)
        << "AddPeer must not refresh activity when it did not add a socket";
}

// Rejecting an excess socket is not successful accumulation activity.
TEST_F(PeerManagerTest, RejectedSocketDoesNotRefreshLastActivity)
{
    PeerManager::AddPeer(42);
    Peer *peer = PeerManager::GetPeerById(42);
    ASSERT_NE(peer, nullptr);

    for (int i = 0; i < VC_TCP_CONNECTIONS; ++i)
    {
        SocketFd fd = MakeTestSocket();
        ASSERT_NE(fd, static_cast<SocketFd>(-1));
        peer->AddSocket(fd);
    }

    auto before = peer->GetLastActivity();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    peer->AddSocket(MakeTestSocket());

    EXPECT_EQ(peer->GetLastActivity(), before)
        << "a rejected socket must not refresh the accumulation timer";
}

// AddPeer on a brand-new ID must not affect an existing peer's socket state
TEST_F(PeerManagerTest, AddNewPeerDoesNotAffectExistingPeer)
{
    PeerManager::AddPeer(4);
    Peer *p4 = PeerManager::GetPeerById(4);
    SocketFd fd = MakeTestSocket();
    ASSERT_GT(fd, 0);
    p4->AddSocket(fd);
    EXPECT_EQ(p4->GetSocketCount(), 1U);

    // Adding a completely different peer must leave peer 4 untouched
    PeerManager::AddPeer(5);
    EXPECT_EQ(PeerManager::GetPeerById(4)->GetSocketCount(), 1U);
}

// ─── AddSocket / GetSocketCount ──────────────────────────────────────────────

TEST_F(PeerManagerTest, AddSocketIncrementsCount)
{
    PeerManager::AddPeer(6);
    Peer *peer = PeerManager::GetPeerById(6);
    ASSERT_NE(peer, nullptr);

    SocketFd fd = MakeTestSocket();
    ASSERT_GT(fd, 0);
    peer->AddSocket(fd);
    EXPECT_EQ(peer->GetSocketCount(), 1U);
}

// The cap guard must prevent exceeding VC_TCP_CONNECTIONS sockets per peer
TEST_F(PeerManagerTest, AddSocketCapDoesNotExceedMaxConnections)
{
    PeerManager::AddPeer(7);
    Peer *peer = PeerManager::GetPeerById(7);
    ASSERT_NE(peer, nullptr);

    // Add exactly VC_TCP_CONNECTIONS sockets — all must be accepted
    std::vector<SocketFd> fds;
    for (int i = 0; i < VC_TCP_CONNECTIONS; ++i)
    {
        SocketFd fd = MakeTestSocket();
        ASSERT_GT(fd, 0);
        fds.push_back(fd);
        peer->AddSocket(fd);
    }
    EXPECT_EQ(peer->GetSocketCount(), static_cast<size_t>(VC_TCP_CONNECTIONS));

    // One extra socket — must be rejected (closed by AddSocket internally)
    SocketFd extraFd = MakeTestSocket();
    ASSERT_GT(extraFd, 0);
    peer->AddSocket(extraFd);

    // Count must not exceed VC_TCP_CONNECTIONS
    EXPECT_EQ(peer->GetSocketCount(), static_cast<size_t>(VC_TCP_CONNECTIONS));
}

// ─── RemoveAllSockets ────────────────────────────────────────────────────────

TEST_F(PeerManagerTest, RemoveAllSocketsClearsCount)
{
    PeerManager::AddPeer(8);
    Peer *peer = PeerManager::GetPeerById(8);
    ASSERT_NE(peer, nullptr);

    SocketFd fd1 = MakeTestSocket();
    SocketFd fd2 = MakeTestSocket();
    ASSERT_GT(fd1, 0);
    ASSERT_GT(fd2, 0);
    peer->AddSocket(fd1);
    peer->AddSocket(fd2);
    ASSERT_EQ(peer->GetSocketCount(), 2U);

    peer->RemoveAllSockets();
    EXPECT_EQ(peer->GetSocketCount(), 0U);
}

TEST_F(PeerManagerTest, RemoveAllSocketsOnEmptyPeerIsNoOp)
{
    PeerManager::AddPeer(9);
    Peer *peer = PeerManager::GetPeerById(9);
    ASSERT_NE(peer, nullptr);
    EXPECT_NO_THROW(peer->RemoveAllSockets());
    EXPECT_EQ(peer->GetSocketCount(), 0U);
}
