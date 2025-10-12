#include "StopableThread.h"
#include "Log.h"
#include <format>

void StopableThread::stop()
{
    std::lock_guard<std::mutex> lock(stopMutex);

    log_info("Stopping thread...");
    this->setRunning(false);

    if (_thread.joinable())
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
