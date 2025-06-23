#include "StopableThread.h"

void StopableThread::stop()
{
    this->setRunning(false);

    if (_thread.joinable())
    {
        _thread.join();
    }
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

