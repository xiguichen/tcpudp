#include "BlockingQueue.h"
#include "Log.h"
#include <format>
#include <memory>
#include <vector>

using namespace Logger;

void BlockingQueue::enqueue(const std::shared_ptr<std::vector<char>> &data)
{
    std::function<void()> notifier;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(data);
        notifier = enqueueNotifier; // copy under lock so it can't be torn by setEnqueueNotifier
    }
    approxQueueSize.fetch_add(1, std::memory_order_relaxed);
    queueCondVar.notify_one();
    // Invoke the external notifier outside the queue lock to avoid lock-order
    // coupling between this queue's mutex and the consumer's wait mutex.
    if (notifier)
        notifier();
}

std::shared_ptr<std::vector<char>> BlockingQueue::dequeue()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCondVar.wait(lock, [this] { return !queue.empty() || cancelled; });
    if (cancelled)
    {
        return nullptr;
    }
    auto result = std::move(queue.front());
    queue.pop();
    lock.unlock();
    approxQueueSize.fetch_sub(1, std::memory_order_relaxed);
    return result;
}

std::shared_ptr<std::vector<char>> BlockingQueue::dequeueWithTimeout(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCondVar.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !queue.empty() || cancelled; });
    if (queue.empty() || cancelled)
        return nullptr;
    auto result = std::move(queue.front());
    queue.pop();
    lock.unlock();
    approxQueueSize.fetch_sub(1, std::memory_order_relaxed);
    return result;
}

std::shared_ptr<std::vector<char>> BlockingQueue::tryDequeue()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (queue.empty() || cancelled)
    {
        return nullptr;
    }
    auto result = std::move(queue.front());
    queue.pop();
    approxQueueSize.fetch_sub(1, std::memory_order_relaxed);
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

void BlockingQueue::setEnqueueNotifier(std::function<void()> notifier)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    enqueueNotifier = std::move(notifier);
}
