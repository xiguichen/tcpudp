#pragma once
#include <thread>
#include <mutex>
#include <atomic>

class StopableThread
{
  public:
    StopableThread() = default;
    virtual ~StopableThread();

    // Start the thread
    void start();

    // Stop the thread
    void stop();

    // Set the running state of the thread (public to allow external signaling)
    virtual void setRunning(bool running);

    // Check if the thread is running
    virtual bool isRunning();

  protected:
    virtual void run() = 0;

  private:
    std::thread _thread; // Thread for execution
    std::atomic<bool> running{true}; // Flag to control the thread execution (atomic for thread safety)
    std::mutex stopMutex; // Mutex for thread safety
};

