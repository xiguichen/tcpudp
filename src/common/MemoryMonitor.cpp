#include "MemoryMonitor.h"
#include "Log.h"
#include "MemoryPool.h"
#include <format>

// Implementation of MemoryMonitor methods

MemoryMonitor& MemoryMonitor::getInstance() {
    static MemoryMonitor instance;
    return instance;
}

void MemoryMonitor::start() {
    if (isRunning_) {
        return;
    }
    isRunning_ = true;
    monitorThread_ = std::thread(&MemoryMonitor::monitorLoop, this);
}

void MemoryMonitor::stop() {
    if (!isRunning_) {
        return;
    }
    isRunning_ = false;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void MemoryMonitor::trackAllocation(size_t bytes) {
    currentMemoryUsage_.fetch_add(bytes, std::memory_order_relaxed);
    auto m = std::max(peakMemoryUsage_.load(std::memory_order_relaxed),
                     currentMemoryUsage_.load(std::memory_order_relaxed));
    peakMemoryUsage_.store(m, std::memory_order_relaxed);
}

void MemoryMonitor::trackDeallocation(size_t bytes) {
    currentMemoryUsage_.fetch_sub(bytes, std::memory_order_relaxed);
}

size_t MemoryMonitor::getCurrentMemoryUsage() const {
    return currentMemoryUsage_.load(std::memory_order_relaxed);
}

size_t MemoryMonitor::getPeakMemoryUsage() const {
    return peakMemoryUsage_.load(std::memory_order_relaxed);
}

void MemoryMonitor::resetPeakMemoryUsage() {
    peakMemoryUsage_.store(currentMemoryUsage_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void MemoryMonitor::setMemoryThreshold(size_t bytes) {
    memoryThreshold_ = bytes;
}

void MemoryMonitor::registerAlertCallback(std::function<void(size_t)> callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    alertCallbacks_.push_back(callback);
}

std::string MemoryMonitor::formatMemorySize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    return std::format("{:.2f} {}", size, units[unitIndex]);
}

void MemoryMonitor::logMemoryUsage() {
    size_t current = getCurrentMemoryUsage();
    size_t peak = getPeakMemoryUsage();
    Logger::Log::getInstance().info(std::format(
        "Memory usage - Current: {}, Peak: {}",
        formatMemorySize(current),
        formatMemorySize(peak)
    ));
}

// Private methods
void MemoryMonitor::monitorLoop() {
    while (isRunning_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        size_t currentUsage = currentMemoryUsage_.load(std::memory_order_relaxed);
        auto now = std::chrono::steady_clock::now();
        if (now - lastLogTime_ >= std::chrono::seconds(10)) {
            logMemoryUsage();
            lastLogTime_ = now;
        }
        if (currentUsage > memoryThreshold_) {
            triggerAlerts(currentUsage);
        }
    }
}

void MemoryMonitor::triggerAlerts(size_t currentUsage) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    for (const auto& callback : alertCallbacks_) {
        callback(currentUsage);
    }
}

// Destructor
MemoryMonitor::~MemoryMonitor() {
    stop();
}

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