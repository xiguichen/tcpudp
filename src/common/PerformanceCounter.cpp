#include "PerformanceCounter.h"
#include <chrono>
#include <limits>
#include <iostream>

PerformanceCounter::PerformanceCounter()
    : minTime(std::numeric_limits<double>::max()), maxTime(std::numeric_limits<double>::lowest()), totalTime(0.0),
      callCount(0), startTime(0.0)
{
}

PerformanceCounter::~PerformanceCounter() = default;

void PerformanceCounter::Enter()
{
    // Record the start time using a high-resolution clock
    startTime = std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

void PerformanceCounter::Exit()
{
    // Record the end time and calculate elapsed time
    double endTime =
        std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    double elapsedTime = endTime - startTime;

    // Update metrics
    if (elapsedTime < minTime)
    {
        minTime = elapsedTime;
    }
    if (elapsedTime > maxTime)
    {
        maxTime = elapsedTime;
    }

    if (elapsedTime > 0.02)
    {
        std::cout << std::format("Issue: PerformanceCounter warning: Elapsed time {:.6f}s exceeds threshold", elapsedTime) << std::endl;
    }

    totalTime += elapsedTime;
    ++callCount;
}

double PerformanceCounter::GetMinTime() const
{
    return callCount > 0 ? minTime : 0.0;
}

double PerformanceCounter::GetMaxTime() const
{
    return callCount > 0 ? maxTime : 0.0;
}

double PerformanceCounter::GetAverageTime() const
{
    return callCount > 0 ? totalTime / callCount : 0.0;
}

int PerformanceCounter::GetCallCount() const
{
    return callCount;
}

void PerformanceCounter::Report() const
{
    std::cout <<
        std::format("PerformanceCounter Report: Calls: {}, Min Time: {:.6f}s, Max Time: {:.6f}s, Avg Time: {:.6f}s",
                    GetCallCount(), GetMinTime(), GetMaxTime(), GetAverageTime()) << std::endl;
}
