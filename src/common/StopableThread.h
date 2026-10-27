#pragma once
#include <mutex>
#include <atomic>
#include <thread>

class StopableThread
{
  public:
    StopableThread() = default;
    virtual ~StopableThread();

    // Start the thread
    void start();

    // Stop the thread
    void stop();

    // Join the thread (for cleanup in derived classes)
    void joinThread();

    // True when called from this object's own worker thread. Uses an atomic id
    // so callers can safely test before the thread is joined/reset.
    bool isCurrentThread() const { return _threadId.load(std::memory_order_acquire) == std::this_thread::get_id(); }

    // Set the running state of the thread (public to allow external signaling)
    virtual void setRunning(bool running);

    // Check if the thread is running
    virtual bool isRunning();

  protected:
    virtual void run() = 0;

  private:
    std::thread _thread; // Thread for execution
    std::atomic<std::thread::id> _threadId{}; // id while the worker runs; avoids racing on _thread
    std::atomic<bool> running{true}; // Flag to control the thread execution (atomic for thread safety)
    std::mutex stopMutex; // Mutex for thread safety
};

