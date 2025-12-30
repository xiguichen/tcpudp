#pragma once

// A class to monitor and report performance metrics
// It's used to track function execution time, includes min, max, and average times.
// During enter a function, call Enter(), and upon exit, call Exit().
class PerformanceCounter
{
public:
    PerformanceCounter();
    ~PerformanceCounter();

    // Call this method when entering a function
    void Enter();

    // Call this method when exiting a function
    void Exit();

    // Get the minimum execution time recorded
    double GetMinTime() const;

    // Get the maximum execution time recorded
    double GetMaxTime() const;

    // Get the average execution time recorded
    double GetAverageTime() const;

    // Get the total number of calls recorded
    int GetCallCount() const;

    // Report performance metrics to log
    void Report() const;

  private:
    double minTime;
    double maxTime;
    double totalTime;
    int callCount;
    double startTime;
};

static PerformanceCounter g_perfCounter;
