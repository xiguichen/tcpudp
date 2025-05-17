#include "MemoryMonitor.h"
#include "Log.h"
#include "MemoryPool.h"
#include <format>

// Implementation of non-inline methods for MemoryMonitor

// Helper function to log memory pool stats
static void logMemoryPoolStats() {
    auto& pool = MemoryPool::getInstance();
    auto stats = pool.getStats();
    
    Logger::Log::getInstance().info(std::format(
        "Memory Pool - Buffers: {} created, {} recycled, {} available",
        stats.totalBuffersCreated,
        stats.buffersRecycled,
        stats.smallBuffersAvailable + stats.mediumBuffersAvailable + stats.largeBuffersAvailable
    ));
}

// Register default alert handler
void MemoryMonitor::registerDefaultAlertHandler() {
    registerAlertCallback([](size_t usage) {
        Logger::Log::getInstance().warning(std::format(
            "Memory usage alert: Current usage is {} (exceeds threshold)",
            MemoryMonitor::formatMemorySize(usage)
        ));
        
        // Log detailed memory pool stats
        logMemoryPoolStats();
    });
}