// src/profile/SessionAnalytics.h
#pragma once
#include "SessionStore.h"
#include <vector>

enum class TrendDirection { NotEnoughData, Improving, Declining, Steady };

struct TrendResult
{
    TrendDirection direction = TrendDirection::NotEnoughData;
    float deltaPercent = 0.0f; // recentAvg - previousAvg; meaningful only when direction != NotEnoughData
    int recentCount = 0;       // how many sessions were actually averaged into "recent"
    int previousCount = 0;     // how many sessions were actually averaged into "previous"
};

// sessionsOldestFirst: a profile's completed Alankar sessions in
// chronological order (SessionStore::loadAllForProfile()'s own order).
// Compares the average overallTimeInTunePercent of the most recent
// min(windowSize, total - 1) sessions ("recent") against the
// min(windowSize, remaining) sessions immediately before that window
// ("previous") - the recent window is deliberately capped at total - 1,
// not total, so it always leaves at least one session for "previous" to
// compare against. NotEnoughData only when there are fewer than 2 sessions
// total - with 2 or more, a comparison (even a small, noisy one) is always
// produced, so a user doesn't have to reach windowSize sessions before
// seeing any trend at all. A delta within +-kSteadyThresholdPercent (see
// the .cpp) is reported as Steady rather than Improving/Declining, so
// noise-level session-to-session variation doesn't flip the label back and
// forth.
TrendResult computeTrend (const std::vector<AlankarSessionRecord>& sessionsOldestFirst, int windowSize = 5);
