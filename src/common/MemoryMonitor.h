#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <atomic>
#include <chrono>
#include <thread>
#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include <iostream>
#include <format>
#include "Log.h"

// A class to monitor memory usage and provide alerts when thresholds are exceeded
class MemoryMonitor {
public:
    static MemoryMonitor& getInstance() {
        static MemoryMonitor instance;
        return instance;
    }
    
    // Start monitoring memory usage
    void start() {
        if (isRunning_) {
            return;
        }
        
        isRunning_ = true;
        monitorThread_ = std::thread(&MemoryMonitor::monitorLoop, this);
    }
    
    // Stop monitoring memory usage
    void stop() {
        if (!isRunning_) {
            return;
        }
        
        isRunning_ = false;
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }
    }
    
    // Track memory allocation
    void trackAllocation(size_t bytes) {
        currentMemoryUsage_.fetch_add(bytes, std::memory_order_relaxed);
        auto m = std::max(peakMemoryUsage_.load(std::memory_order_relaxed), 
        currentMemoryUsage_.load(std::memory_order_relaxed));
        peakMemoryUsage_.store(m, std::memory_order_relaxed);
    }
    
    // Track memory deallocation
    void trackDeallocation(size_t bytes) {
        currentMemoryUsage_.fetch_sub(bytes, std::memory_order_relaxed);
    }
    
    // Get current memory usage
    size_t getCurrentMemoryUsage() const {
        return currentMemoryUsage_.load(std::memory_order_relaxed);
    }
    
    // Get peak memory usage
    size_t getPeakMemoryUsage() const {
        return peakMemoryUsage_.load(std::memory_order_relaxed);
    }
    
    // Reset peak memory usage
    void resetPeakMemoryUsage() {
        peakMemoryUsage_.store(currentMemoryUsage_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    
    // Set memory usage threshold for alerts
    void setMemoryThreshold(size_t bytes) {
        memoryThreshold_ = bytes;
    }
    
    // Register a callback for memory alerts
    void registerAlertCallback(std::function<void(size_t)> callback) {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        alertCallbacks_.push_back(callback);
    }
    
    // Register the default alert handler
    void registerDefaultAlertHandler();
    
    // Format memory size for human-readable output
    static std::string formatMemorySize(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unitIndex = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024.0 && unitIndex < 4) {
            size /= 1024.0;
            unitIndex++;
        }
        
        return std::format("{:.2f} {}", size, units[unitIndex]);
    }
    
    // Log current memory usage
    void logMemoryUsage() {
        size_t current = getCurrentMemoryUsage();
        size_t peak = getPeakMemoryUsage();
        
        Logger::Log::getInstance().info(std::format(
            "Memory usage - Current: {}, Peak: {}", 
            formatMemorySize(current), 
            formatMemorySize(peak)
        ));
    }

private:
    MemoryMonitor() 
        : isRunning_(false),
          currentMemoryUsage_(0),
          peakMemoryUsage_(0),
          memoryThreshold_(100 * 1024 * 1024)  // Default 100MB threshold
    {}
    
    ~MemoryMonitor() {
        stop();
    }
    
    // Memory monitoring loop
    void monitorLoop() {
        while (isRunning_) {
            // Check memory usage every second
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            size_t currentUsage = currentMemoryUsage_.load(std::memory_order_relaxed);
            
            // Log memory usage every 10 seconds
            auto now = std::chrono::steady_clock::now();
            if (now - lastLogTime_ >= std::chrono::seconds(10)) {
                logMemoryUsage();
                lastLogTime_ = now;
            }
            
            // Check if memory usage exceeds threshold
            if (currentUsage > memoryThreshold_) {
                triggerAlerts(currentUsage);
            }
        }
    }
    
    // Trigger memory alert callbacks
    void triggerAlerts(size_t currentUsage) {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        for (const auto& callback : alertCallbacks_) {
            callback(currentUsage);
        }
    }
    
    std::atomic<bool> isRunning_;
    std::thread monitorThread_;
    
    std::atomic<size_t> currentMemoryUsage_;
    std::atomic<size_t> peakMemoryUsage_;
    std::atomic<size_t> memoryThreshold_;
    
    std::vector<std::function<void(size_t)>> alertCallbacks_;
    std::mutex callbacksMutex_;
    
    std::chrono::steady_clock::time_point lastLogTime_ = std::chrono::steady_clock::now();
};

// Smart buffer class that automatically tracks memory usage
template<typename T>
class TrackedBuffer {
public:
    TrackedBuffer(size_t size) : buffer_(size) {
        MemoryMonitor::getInstance().trackAllocation(size * sizeof(T));
    }
    
    ~TrackedBuffer() {
        MemoryMonitor::getInstance().trackDeallocation(buffer_.size() * sizeof(T));
    }
    
    // Prevent copying
    TrackedBuffer(const TrackedBuffer&) = delete;
    TrackedBuffer& operator=(const TrackedBuffer&) = delete;
    
    // Allow moving
    TrackedBuffer(TrackedBuffer&& other) noexcept : buffer_(std::move(other.buffer_)) {
        // No need to track allocation/deallocation for moves
    }
    
    TrackedBuffer& operator=(TrackedBuffer&& other) noexcept {
        if (this != &other) {
            // Track deallocation of current buffer
            MemoryMonitor::getInstance().trackDeallocation(buffer_.size() * sizeof(T));
            
            // Move the buffer
            buffer_ = std::move(other.buffer_);
            
            // No need to track allocation for the moved buffer
        }
        return *this;
    }
    
    // Resize the buffer
    void resize(size_t newSize) {
        size_t oldSize = buffer_.size();
        buffer_.resize(newSize);
        
        if (newSize > oldSize) {
            MemoryMonitor::getInstance().trackAllocation((newSize - oldSize) * sizeof(T));
        } else if (newSize < oldSize) {
            MemoryMonitor::getInstance().trackDeallocation((oldSize - newSize) * sizeof(T));
        }
    }
    
    // Access the underlying vector
    std::vector<T>& get() { return buffer_; }
    const std::vector<T>& get() const { return buffer_; }
    
    // Convenience operators
    T& operator[](size_t index) { return buffer_[index]; }
    const T& operator[](size_t index) const { return buffer_[index]; }
    
    // Common vector operations
    size_t size() const { return buffer_.size(); }
    bool empty() const { return buffer_.empty(); }
    void clear() { 
        MemoryMonitor::getInstance().trackDeallocation(buffer_.size() * sizeof(T));
        buffer_.clear(); 
    }
    
    // Iterator access
    typename std::vector<T>::iterator begin() { return buffer_.begin(); }
    typename std::vector<T>::iterator end() { return buffer_.end(); }
    typename std::vector<T>::const_iterator begin() const { return buffer_.begin(); }
    typename std::vector<T>::const_iterator end() const { return buffer_.end(); }

private:
    std::vector<T> buffer_;
};

// Convenience type alias for char buffers
using TrackedCharBuffer = TrackedBuffer<char>;

#endif // MEMORY_MONITOR_H