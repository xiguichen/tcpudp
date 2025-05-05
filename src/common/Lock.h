#pragma once

#include <mutex>
#include <map>

class SocketLock {

public:
    // Get the singleton instance
    static SocketLock& getInstance() {
        static SocketLock instance;
        return instance;
    }

    // Return unique lock object of a socket
    std::unique_lock<std::mutex> getLock(int socket) {
        return std::unique_lock<std::mutex>(getMutex(socket));
    }

    // Clear all mutexes
    void clearMutexes() {
        mutexes.clear();
    }

private:
    SocketLock() = default; // Private constructor

    // Return mutex object of a socket, use a map to store mutexes
    std::mutex& getMutex(int socket) {
        return mutexes[socket];
    }

    std::map<int, std::mutex> mutexes;



};
