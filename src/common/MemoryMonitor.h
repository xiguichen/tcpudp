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
#include <algorithm>
#include "Log.h"

// A class to monitor memory usage and provide alerts when thresholds are exceeded
class MemoryMonitor {
public:
    static MemoryMonitor& getInstance();
    void start();
    void stop();
    void trackAllocation(size_t bytes);
    void trackDeallocation(size_t bytes);
    size_t getCurrentMemoryUsage() const;
    size_t getPeakMemoryUsage() const;
    void resetPeakMemoryUsage();
    void setMemoryThreshold(size_t bytes);
    void registerAlertCallback(std::function<void(size_t)> callback);
    void registerDefaultAlertHandler();
    static std::string formatMemorySize(size_t bytes);
    void logMemoryUsage();

private:
    MemoryMonitor() 
        : isRunning_(false),
          currentMemoryUsage_(0),
          peakMemoryUsage_(0),
          memoryThreshold_(100 * 1024 * 1024)  // Default 100MB threshold
    {}
    
    ~MemoryMonitor();
    void monitorLoop();
    void triggerAlerts(size_t currentUsage);
    
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