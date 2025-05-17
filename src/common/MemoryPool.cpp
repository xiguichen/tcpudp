#include "MemoryPool.h"
#include "Log.h"
#include <format>

// Implementation of non-inline methods for MemoryPool

// This file is mostly empty because MemoryPool is implemented as a header-only library
// with most methods defined inline. However, we include this file to ensure proper
// linking and to provide a place for any future non-inline implementations.

// Log memory pool statistics
void logMemoryPoolStats() {
    auto& pool = MemoryPool::getInstance();
    auto stats = pool.getStats();
    
    Logger::Log::getInstance().info(std::format(
        "Memory Pool Stats - Total Allocated: {}, Buffers Created: {}, Recycled: {}",
        stats.totalAllocatedMemory,
        stats.totalBuffersCreated,
        stats.buffersRecycled
    ));
    
    Logger::Log::getInstance().info(std::format(
        "Buffer Availability - Small: {}, Medium: {}, Large: {}",
        stats.smallBuffersAvailable,
        stats.mediumBuffersAvailable,
        stats.largeBuffersAvailable
    ));
}