#include "StopableThread.h"
#include <thread>
#include "Log.h"
#include <format>

void StopableThread::stop()
{
    std::lock_guard<std::mutex> lock(stopMutex);

    log_info("Stopping thread...");
    this->setRunning(false);

    // Avoid joining the current thread to prevent deadlocks.
    if (_thread.joinable())
    {
        // If called from a different thread, join to ensure clean termination
        if (_thread.get_id() != std::this_thread::get_id())
        {
            log_info("Waiting for thread to join...");
            try
            {
                _thread.join();
            }
            catch (const std::exception &e)
            {
                log_error(std::format("Exception while joining thread: {}", e.what()));
            }
        }
        else
        {
            // Called from the thread itself; do not join
            log_debug("Stop called from thread itself; skipping join to avoid deadlock");
        }
    }
    log_info("Thread stopped.");
}
void StopableThread::start()
{
    _thread = std::thread(&StopableThread::run, this);
}
bool StopableThread::isRunning()
{
    return running;
}
void StopableThread::setRunning(bool running)
{
    this->running = running;
}

StopableThread::~StopableThread()
{
    // Ensure the thread is stopped before destruction
    if (this->running)
    {
        stop();
    }
}
