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

    log_info(std::format("Wait for queue data , Queue address: {:p}", static_cast<const void *>(this)));

    std::unique_lock<std::mutex> lock(queueMutex);
    // Block until queue is not empty
    queueCondVar.wait(lock, [this] { return !queue.empty() || cancelled; });
    if (cancelled)
    {
        log_info("Dequeue cancelled");
        return nullptr;
    }
    auto result = queue.front();
    queue.pop();

    lock.unlock();
    log_info(
        std::format("Dequeued data of size: {}, Queue address: {:p}", result->size(), static_cast<const void *>(this)));

    log_info(std::format("Queue size after dequeue: {}, Queue address: {:p}", queue.size(),
                         static_cast<const void *>(this)));
    return result;
}

void BlockingQueue::cancelWait()
{
    cancelled = true;
    queueCondVar.notify_all();
}
