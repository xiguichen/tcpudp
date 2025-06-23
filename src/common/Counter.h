#pragma once

#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>

template<typename T>
class Counter
{
public:
    Counter() : count(0) {}

    // Method to return the current count and increase it
    T getAndIncrement() {
        return count++;
    }

    // Reset the counter
    void reset() {
        count.store(0);
    }

private:
    std::atomic<T> count;
};


class CounterManager
{
public:
  static CounterManager &getInstance(int clientId);

  // Get the sent counter and increment it
  unsigned int getSentCounter();

  // Reset the sent counter
  void resetSentCounter();

  // Get the received counter and increment it
  unsigned int getReceivedCounter();

  // Reset the received counter
  void resetReceivedCounter();

private:
  // Private constructor to prevent instantiation
  CounterManager() = default;

  // Delete copy constructor and assignment operator to prevent copying
  CounterManager(const CounterManager &) = delete;
  CounterManager &operator=(const CounterManager &) = delete;

  static std::unordered_map<int, std::unique_ptr<CounterManager>> _instances;
  static std::mutex _mutex;
  Counter<unsigned int> _sentCounter;
  Counter<unsigned int> _receivedCounter;

};
