#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <future>

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
        // If already running, stop it first
        if (monitoringActive.load(std::memory_order_acquire)) {
            stopMonitoring();
        }
        
        // Set active flag and start thread
        monitoringActive.store(true, std::memory_order_release);
        monitoringThread = std::thread([this, intervalSeconds]() {
            while (monitoringActive.load(std::memory_order_acquire)) {
                // Use a shorter sleep with periodic checks
                for (int i = 0; i < intervalSeconds && monitoringActive.load(std::memory_order_acquire); i++) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                
                if (monitoringActive.load(std::memory_order_acquire)) {
                    printPerformanceReport();
                }
            }
            
            // Final report when stopping
            printPerformanceReport();
        });
        
        // Don't detach immediately - keep thread joinable
    }
    
    void stopMonitoring() {
        // Set the atomic flag to false
        monitoringActive.store(false, std::memory_order_release);
        
        // Join the thread if it's joinable
        if (monitoringThread.joinable()) {
            // Wait with timeout
            std::future<void> future = std::async(std::launch::async, [this]() {
                if (monitoringThread.joinable()) {
                    monitoringThread.join();
                }
            });
            
            // Wait for 1 second max
            if (future.wait_for(std::chrono::seconds(1)) == std::future_status::timeout) {
                // If thread doesn't respond, detach it
                monitoringThread.detach();
            }
        }
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