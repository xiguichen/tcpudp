#pragma once

#include <atomic>

template<typename T>
class Counter
{
public:
    Counter() : count(0) {}

    T getAndIncrement() {
        return count++;
    }

    T incrementAndGet() {
        return ++count;
    }

    void reset() {
        count.store(0);
    }

    T get() const {
        return count.load();
    }

private:
    std::atomic<T> count;
};
