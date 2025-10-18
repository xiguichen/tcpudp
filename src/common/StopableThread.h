#pragma once
#include <thread>
#include <mutex>

class StopableThread
{
  public:
    StopableThread() = default;
    virtual ~StopableThread();

    // Start the thread
    void start();

    // Stop the thread
    void stop();

  protected:
    virtual void run() = 0;

    // Check if the thread is running
    virtual bool isRunning();

    // Set the running state of the thread
    virtual void setRunning(bool running);

  private:
    std::thread _thread; // Thread for execution
    bool running = true; // Flag to control the thread execution
    std::mutex stopMutex; // Mutex for thread safety
};

