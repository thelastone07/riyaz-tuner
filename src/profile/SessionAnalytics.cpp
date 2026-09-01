// src/profile/SessionAnalytics.cpp
#include "SessionAnalytics.h"
#include <algorithm>

namespace
{
    // Deltas smaller than this are reported as Steady rather than
    // Improving/Declining - avoids a flip-flopping label from noise-level
    // session-to-session variation.
    constexpr float kSteadyThresholdPercent = 2.0f;

    float averageOverallPercent (const std::vector<AlankarSessionRecord>& sessions, size_t startIndex, size_t count)
    {
        if (count == 0)
            return 0.0f;

        float sum = 0.0f;
        for (size_t i = 0; i < count; ++i)
            sum += sessions[startIndex + i].overallTimeInTunePercent;
        return sum / (float) count;
    }
}

TrendResult computeTrend (const std::vector<AlankarSessionRecord>& sessionsOldestFirst, int windowSize)
{
    TrendResult result;

    const size_t total = sessionsOldestFirst.size();
    if (total < 2 || windowSize <= 0)
        return result; // NotEnoughData

    // Capped at total - 1 (not just total): reserves at least one session
    // for the "previous" side below, so any total >= 2 produces a real
    // comparison instead of NotEnoughData - a user on their 2nd-5th session
    // (i.e. below a naive windowSize-sized recent window) still sees a trend.
    const size_t recentCount = std::min ((size_t) windowSize, total - 1);
    const size_t remaining = total - recentCount; // always >= 1, given total >= 2 above
    const size_t previousCount = std::min ((size_t) windowSize, remaining);
    const size_t previousStart = remaining - previousCount;
    const size_t recentStart = total - recentCount;

    const float recentAvg = averageOverallPercent (sessionsOldestFirst, recentStart, recentCount);
    const float previousAvg = averageOverallPercent (sessionsOldestFirst, previousStart, previousCount);

    result.recentCount = (int) recentCount;
    result.previousCount = (int) previousCount;
    result.deltaPercent = recentAvg - previousAvg;

    if (result.deltaPercent > kSteadyThresholdPercent)
        result.direction = TrendDirection::Improving;
    else if (result.deltaPercent < -kSteadyThresholdPercent)
        result.direction = TrendDirection::Declining;
    else
        result.direction = TrendDirection::Steady;

    return result;
}
