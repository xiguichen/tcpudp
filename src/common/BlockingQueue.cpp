#include "BlockingQueue.h"
#include "Log.h"
#include <format>
#include <memory>
#include <vector>

using namespace Logger;

void BlockingQueue::enqueue(const std::shared_ptr<std::vector<char>> &data)
{
    // Enqueue to the standard queue with locking
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(data);
        log_debug(std::format("Queue size after enqueue: {}, Queue address: {:p}", queue.size(),
                             static_cast<const void *>(this)));
    }

    log_debug(std::format("Enqueued data, Queue address: {:p}", static_cast<const void *>(this)));
    queueCondVar.notify_one();
}

std::shared_ptr<std::vector<char>> BlockingQueue::dequeue()
{

    log_debug(std::format("Wait for queue data , Queue address: {:p}", static_cast<const void *>(this)));

    std::unique_lock<std::mutex> lock(queueMutex);
    // Block until queue is not empty
    queueCondVar.wait(lock, [this] { return !queue.empty() || cancelled; });
    if (cancelled)
    {
        log_debug("Dequeue cancelled");
        return nullptr;
    }
    auto result = queue.front();
    queue.pop();
    log_debug(std::format("Queue size after dequeue: {}, Queue address: {:p}", queue.size(),
                         static_cast<const void *>(this)));
    lock.unlock();

    log_debug(
        std::format("Dequeued data of size: {}, Queue address: {:p}", result->size(), static_cast<const void *>(this)));
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
