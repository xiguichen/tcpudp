#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <memory>
#include <vector>
#include <utility>
#include <thread>

// A simple lock-free queue implementation using a fixed-size ring buffer
template<typename T>
class LockFreeQueue {
public:
    explicit LockFreeQueue(size_t capacity = 1024) 
        : buffer_(new Node[capacity + 1]), // +1 to distinguish between empty and full
          capacity_(capacity + 1),
          head_(0),
          tail_(0) {
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
        return true;
    }

    // Try to dequeue with a timeout
    bool dequeue_with_timeout(T& item, int timeout_ms) {
        auto start = std::chrono::steady_clock::now();
        
        while (true) {
            if (dequeue(item)) {
                return true;
            }
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            
            if (elapsed >= timeout_ms) {
                return false;
            }
            
            // Yield to avoid busy-waiting
            std::this_thread::yield();
        }
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
    
    // Prevent false sharing
    char padding_[64 - sizeof(std::atomic<size_t>)];
};

#endif // LOCK_FREE_QUEUE_H