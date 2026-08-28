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

void AlankarPracticeEngine::onBeatElapsed()
{
    if (isFinished())
        return;

    ++beatsIntoCurrentStep;
    if (beatsIntoCurrentStep >= kBeatsPerStep)
    {
        beatsIntoCurrentStep = 0;
        ++stepIndex;
    }
}

void AlankarPracticeEngine::onPitchReading (float centsFromSa)
{
    if (isFinished())
        return;

    const auto& step = pattern.fullSequence()[(size_t) stepIndex];
    const float targetCents = centsFromSaForSwar (step.swar, step.octaveOffset);

    auto& result = stepResults[(size_t) stepIndex];
    ++result.framesTotal;
    if (std::abs (centsFromSa - targetCents) <= kInTuneToleranceCents)
        ++result.framesInTune;
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

    int totalInTune = 0;
    int totalFrames = 0;
    std::map<Swar, std::pair<int, int>> perSwarTotals; // swar -> (framesInTune, framesTotal)

    for (const auto& result : stepResults)
    {
        totalInTune += result.framesInTune;
        totalFrames += result.framesTotal;

        auto& swarTotals = perSwarTotals[result.swar];
        swarTotals.first += result.framesInTune;
        swarTotals.second += result.framesTotal;
    }

    summary.overallTimeInTunePercent = totalFrames > 0
        ? (100.0f * (float) totalInTune / (float) totalFrames)
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
