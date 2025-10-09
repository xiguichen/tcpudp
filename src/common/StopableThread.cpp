#include "StopableThread.h"
#include "Log.h"

void StopableThread::stop()
{
    log_info("Stopping thread...");
    this->setRunning(false);

    if (_thread.joinable())
    {
        log_info("Waiting for thread to join...");
        _thread.join();
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

