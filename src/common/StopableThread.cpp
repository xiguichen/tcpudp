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
    return running.load();
}
void StopableThread::setRunning(bool value)
{
    this->running.store(value);
}

StopableThread::~StopableThread()
{
    // Ensure the thread is stopped before destruction.
    // Note: running may already be false if the thread was signaled to stop
    // externally (e.g. via setRunning(false)) without being joined.
    if (_thread.joinable())
    {
        if (_thread.get_id() == std::this_thread::get_id())
        {
            // Destructor called from within the thread itself (e.g. the owning
            // object is destroyed from a callback executing on this thread).
            // Joining would deadlock; detach so the thread finishes naturally.
            log_debug("~StopableThread: detaching self to avoid join deadlock");
            _thread.detach();
        }
        else
        {
            this->setRunning(false);
            log_debug("~StopableThread: joining thread");
            try { _thread.join(); }
            catch (const std::exception &e)
            {
                log_error(std::format("~StopableThread join failed: {}", e.what()));
            }
        }
    }
}
