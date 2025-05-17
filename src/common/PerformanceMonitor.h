#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>

class PerformanceMonitor {
public:
    static PerformanceMonitor& getInstance() {
        static PerformanceMonitor instance;
        return instance;
    }
    
    void recordPacketProcessed(size_t size, std::chrono::microseconds processingTime) {
        packetsProcessed.fetch_add(1, std::memory_order_relaxed);
        bytesProcessed.fetch_add(size, std::memory_order_relaxed);
        processingTimeUs.fetch_add(processingTime.count(), std::memory_order_relaxed);
    }
    
    void startMonitoring(int intervalSeconds = 10) {
        if (monitoringActive.exchange(true)) {
            return; // Already running
        }
        
        monitoringThread = std::thread([this, intervalSeconds]() {
            while (monitoringActive.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
                printPerformanceReport();
            }
        });
        
        monitoringThread.detach();
    }
    
    void stopMonitoring() {
        monitoringActive.store(false, std::memory_order_release);
    }
    
    void printPerformanceReport() {
        std::lock_guard<std::mutex> lock(reportMutex);
        
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastResetTime).count();
        
        if (duration == 0) return;
        
        uint64_t packets = packetsProcessed.exchange(0, std::memory_order_relaxed);
        uint64_t bytes = bytesProcessed.exchange(0, std::memory_order_relaxed);
        uint64_t timeUs = processingTimeUs.exchange(0, std::memory_order_relaxed);
        
        double packetsPerSecond = static_cast<double>(packets) / duration;
        double mbps = static_cast<double>(bytes * 8) / (duration * 1000000);
        double avgLatencyUs = packets > 0 ? static_cast<double>(timeUs) / packets : 0;
        
        std::cout << "=== Performance Report ===" << std::endl;
        std::cout << "  Duration: " << duration << " seconds" << std::endl;
        std::cout << "  Packets: " << packets << " (" << packetsPerSecond << " packets/sec)" << std::endl;
        std::cout << "  Throughput: " << mbps << " Mbps" << std::endl;
        std::cout << "  Avg Latency: " << avgLatencyUs << " us" << std::endl;
        std::cout << "=========================" << std::endl;
        
        lastResetTime = now;
    }
    
private:
    PerformanceMonitor() : lastResetTime(std::chrono::steady_clock::now()) {}
    ~PerformanceMonitor() {
        stopMonitoring();
    }
    
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
    
    std::atomic<uint64_t> packetsProcessed{0};
    std::atomic<uint64_t> bytesProcessed{0};
    std::atomic<uint64_t> processingTimeUs{0};
    std::chrono::time_point<std::chrono::steady_clock> lastResetTime;
    
    std::mutex reportMutex;
    std::thread monitoringThread;
    std::atomic<bool> monitoringActive{false};
};

#endif // PERFORMANCE_MONITOR_H