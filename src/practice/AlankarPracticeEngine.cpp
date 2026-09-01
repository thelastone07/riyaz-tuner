// src/practice/AlankarPracticeEngine.cpp
#include "AlankarPracticeEngine.h"
#include "../audio/swarmap/SwarMapper.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <map>

AlankarPracticeEngine::AlankarPracticeEngine (AlankarPatternId patternId)
    : pattern (patternId)
{
    stepResults.reserve (pattern.fullSequence().size());
    for (const auto& step : pattern.fullSequence())
        stepResults.push_back ({ step.swar, step.octaveOffset, 0, 0 });
}

void AlankarPracticeEngine::onBeatElapsed (uint64_t latestKnownTimestampMs)
{
    if (isFinished())
        return;

    ++beatsIntoCurrentStep;
    if (beatsIntoCurrentStep >= kBeatsPerStep)
    {
        beatsIntoCurrentStep = 0;
        ++stepIndex;
        currentStepStartTimestampMs = latestKnownTimestampMs;
    }
}

void AlankarPracticeEngine::onPitchReading (float centsFromSa, uint64_t timestampMs)
{
    // Determine the target step. A reading captured before the current
    // step's recorded start is a late-arriving straggler from the PREVIOUS
    // step's tail (inference latency + async delivery routinely land it
    // after the next step has already begun) - redirect it there instead of
    // crediting the wrong step. This is also what lets a just-finished
    // engine still credit the last real step's tail readings (see below),
    // rather than the old behaviour of silently dropping them.
    int targetIndex = stepIndex;
    if (currentStepStartTimestampMs != 0 && timestampMs < currentStepStartTimestampMs)
        targetIndex = stepIndex - 1;

    if (targetIndex < 0 || targetIndex >= totalSteps())
        return; // nothing valid to attribute to - a reading before any step has started, or no eligible late step at the tail of a finished run

    // Weight = elapsed time since the previous reading, capped. The very
    // first reading ever (or a non-advancing/out-of-order timestamp)
    // contributes nothing - acceptable given it's at most one sample out of
    // many over a real run.
    uint64_t weight = 0;
    if (lastReadingTimestampMs != 0 && timestampMs > lastReadingTimestampMs)
        weight = std::min (timestampMs - lastReadingTimestampMs, kMaxReadingGapMs);
    lastReadingTimestampMs = timestampMs;

    const auto& step = pattern.fullSequence()[(size_t) targetIndex];
    const float targetCents = centsFromSaForSwar (step.swar, step.octaveOffset);

    auto& result = stepResults[(size_t) targetIndex];
    result.msTotal += weight;
    if (std::abs (centsFromSa - targetCents) <= kInTuneToleranceCents)
        result.msInTune += weight;
}

bool AlankarPracticeEngine::isFinished() const
{
    return stepIndex >= (int) pattern.fullSequence().size();
}

int AlankarPracticeEngine::currentStepIndex() const
{
    return stepIndex;
}

int AlankarPracticeEngine::totalSteps() const
{
    return (int) pattern.fullSequence().size();
}

float AlankarPracticeEngine::currentStepTargetCents() const
{
    jassert (! isFinished()); // contract: caller must check isFinished() first - see header doc comment

    // Defensive, not redundant with the assertion above: jassert compiles away
    // entirely in Release, and on a finished engine stepIndex == size(), so
    // the indexing below would be an out-of-range vector read (UB) rather than
    // a caught contract violation. A finished engine is a normal, long-lived
    // state - MainComponent deliberately keeps one alive after a practice run
    // so it can keep displaying the final summary - so any future call site is
    // realistically going to reach this method in that state. Fail with a
    // defined wrong number instead of a memory read past the end.
    if (isFinished())
        return 0.0f;

    const auto& step = pattern.fullSequence()[(size_t) stepIndex];
    return centsFromSaForSwar (step.swar, step.octaveOffset);
}

AlankarSummary AlankarPracticeEngine::getSummary() const
{
    AlankarSummary summary;
    summary.perStep = stepResults;

    uint64_t totalInTune = 0;
    uint64_t totalMs = 0;
    std::map<Swar, std::pair<uint64_t, uint64_t>> perSwarTotals; // swar -> (msInTune, msTotal)

    for (const auto& result : stepResults)
    {
        totalInTune += result.msInTune;
        totalMs += result.msTotal;

        auto& swarTotals = perSwarTotals[result.swar];
        swarTotals.first += result.msInTune;
        swarTotals.second += result.msTotal;
    }

    summary.overallTimeInTunePercent = totalMs > 0
        ? (100.0f * (float) totalInTune / (float) totalMs)
        : 0.0f;

    for (const auto& entry : perSwarTotals)
    {
        const float percent = entry.second.second > 0
            ? (100.0f * (float) entry.second.first / (float) entry.second.second)
            : 0.0f;
        summary.perSwarTimeInTunePercent.push_back ({ entry.first, percent });
    }

    std::sort (summary.perSwarTimeInTunePercent.begin(), summary.perSwarTimeInTunePercent.end(),
               [] (const auto& a, const auto& b) { return a.second < b.second; }); // worst-to-best

    return summary;
}
