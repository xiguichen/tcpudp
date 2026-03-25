#include "BlockingQueue.h"
#include "Log.h"
#include <format>
#include <memory>
#include <vector>

using namespace Logger;

void BlockingQueue::enqueue(const std::shared_ptr<std::vector<char>> &data)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(data);
    }

    queueCondVar.notify_one();
}

std::shared_ptr<std::vector<char>> BlockingQueue::dequeue()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCondVar.wait(lock, [this] { return !queue.empty() || cancelled; });
    if (cancelled)
    {
        return nullptr;
    }
    auto result = queue.front();
    queue.pop();
    lock.unlock();
    return result;
}

void BlockingQueue::cancelWait()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        cancelled = true;
    }
    queueCondVar.notify_all();
}

size_t BlockingQueue::size()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return queue.size();
}
