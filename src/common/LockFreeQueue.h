#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H
#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <condition_variable>

// A simple lock-free queue implementation using a fixed-size ring buffer
template<typename T>
class LockFreeQueue {
    std::atomic<bool> has_data_;
    std::condition_variable not_empty_;
    std::mutex notify_mutex_;

public:
    explicit LockFreeQueue(size_t capacity = 1024) 
        : buffer_(new Node[capacity + 1]), // +1 to distinguish between empty and full
          capacity_(capacity + 1),
          head_(0),
          tail_(0),
          has_data_(false) {
    }

    ~LockFreeQueue() {
        delete[] buffer_;
    }

    // Enqueue an item, returns true if successful, false if queue is full
    bool enqueue(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) % capacity_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer_[head].data = item;
        head_.store(next_head, std::memory_order_release);
        
        // Notify waiting consumers
        {
            std::lock_guard<std::mutex> lock(notify_mutex_);
            has_data_ = true;
            not_empty_.notify_one();
        }
        
        return true;
    }

    // Dequeue an item, returns true if successful, false if queue is empty
    bool dequeue(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }
        
        item = buffer_[tail].data;
        tail_.store((tail + 1) % capacity_, std::memory_order_release);
        
        // Update has_data_ flag when needed
        if (empty()) {
            std::lock_guard<std::mutex> lock(notify_mutex_);
            has_data_.store(false, std::memory_order_release);
        }
        
        return true;
    }

    // Try to dequeue with a timeout
    bool dequeue_with_timeout(T& item, int timeout_ms) {
        // First try a quick non-blocking dequeue
        if (dequeue(item)) {
            return true;
        }
        
        // If no data available, wait with timeout
        std::unique_lock<std::mutex> lock(notify_mutex_);
        
        // Check if queue is empty again (might have changed)
        if (!empty()) {
            // Queue now has data, try again after releasing the lock
            lock.unlock();
            return dequeue(item);
        }
        
        // Wait for data to become available with timeout
        bool has_data = not_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
            [this] { return !this->empty(); });
            
        // Unlock before returning or dequeueing
        lock.unlock();
        
        if (!has_data) {
            return false; // Timeout occurred
        }
        
        // Try to dequeue again after waiting
        return dequeue(item);
    }

    // Check if the queue is empty
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    // Get the approximate size (may not be accurate in concurrent scenarios)
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        
        if (head >= tail) {
            return head - tail;
        } else {
            return capacity_ - (tail - head);
        }
    }

private:
    struct Node {
        T data;
    };

    Node* buffer_;
    const size_t capacity_;
    
    // Head is where we enqueue
    std::atomic<size_t> head_;
    
    // Tail is where we dequeue
    std::atomic<size_t> tail_;
    
    // Prevent false sharing for notification variables
    
    // Prevent false sharing
    char padding_[64 - sizeof(std::atomic<size_t>)];
};


#endif // LOCK_FREE_QUEUE_H