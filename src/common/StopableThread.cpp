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
        if (!isCurrentThread())
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
            catch (...)
            {
                log_error("Unknown exception while joining thread");
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
    _thread = std::thread([this]() {
        _threadId.store(std::this_thread::get_id(), std::memory_order_release);
        run();
        _threadId.store(std::thread::id{}, std::memory_order_release);
    });
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
        if (isCurrentThread())
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

    // Note: stop() must be called before destruction by the parent class
    // We only join if the thread is still joinable
    if (_thread.joinable())
    {
        // Thread has not been joined yet
        // This should not happen if close() was called properly
        log_error("StopableThread destroyed while thread is still joinable!");
        try
        {
            _thread.join();
        }
        catch (const std::exception &e)
        {
            log_error(std::format("Exception while joining thread in destructor: {}", e.what()));
        }
        catch (...)
        {
            log_error("Unknown exception while joining thread in destructor");
        }
    }
}

void StopableThread::joinThread()
{
    if (_thread.joinable())
    {
        // Never join ourselves: std::thread::join() on the current thread throws
        // std::system_error (EDEADLK) and can terminate. This happens when a
        // callback running on the thread triggers owner teardown.
        if (isCurrentThread())
        {
            log_debug("joinThread called from owning thread; detaching instead");
            _thread.detach();
            return;
        }
        _thread.join();
    }
}
