// src/practice/AlankarPracticeEngine.h
#pragma once
#include "AlankarPattern.h"
#include <cstdint>
#include <utility>
#include <vector>

// Time-in-tune per step, measured against the pipeline's own capture
// timestamps (see PitchPipelineUpdate::timestampMs) rather than a count of
// delivered pitch-worker updates - so a step that happened to receive fewer
// updates (worker stall, busy message thread) isn't under-weighted, and a
// reading is credited to whichever step was actually being sung when it was
// captured, not whichever step happened to be current when it arrived.
struct AlankarStepResult
{
    Swar swar = Swar::Sa;
    int octaveOffset = 0;
    uint64_t msInTune = 0;
    uint64_t msTotal = 0; // 0 if no confident pitch update was ever attributed to this step - happens at very high BPM relative to the pitch engine's ~64ms hop, or if the worker stalled across the whole step; reported as 0% for that step, not an error
};

struct AlankarSummary
{
    std::vector<AlankarStepResult> perStep;
    // sum(msInTune) / sum(msTotal) across all steps, 0 if no time attributed at all.
    float overallTimeInTunePercent = 0.0f;
    // Aggregated across all steps using that swar (any octave), sorted worst-to-best.
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent;
};

// Pure logic, externally driven - no audio/JUCE-Timer dependency. The
// caller (MainComponent) is responsible for calling onBeatElapsed() once
// per real metronome beat boundary, and onPitchReading() for every
// confident pitch frame while practice is active - both carrying the pitch
// pipeline's own capture timestamp (PitchPipelineUpdate::timestampMs), not
// a delivery-time/wall-clock stamp, so the two stay in the same time domain.
class AlankarPracticeEngine
{
public:
    // Exposed so callers (e.g. MainComponent's target-band display) use the
    // exact same tolerance value this class scores against, not a
    // duplicated magic number.
    static constexpr float kInTuneToleranceCents = 25.0f;

    explicit AlankarPracticeEngine (AlankarPatternId patternId);

    // latestKnownTimestampMs: the most recent pipeline capture timestamp
    // known at the moment this beat boundary is observed (MainComponent
    // passes its last-seen PitchPipelineUpdate::timestampMs). Marks where
    // the new current step's readings begin, for onPitchReading()'s
    // straggler check below.
    void onBeatElapsed (uint64_t latestKnownTimestampMs);

    // timestampMs is the reading's own capture timestamp (from
    // PitchPipelineUpdate::timestampMs), not when it was delivered/processed.
    void onPitchReading (float centsFromSa, uint64_t timestampMs);

    bool isFinished() const;
    int currentStepIndex() const;  // valid until isFinished()
    int totalSteps() const;
    float currentStepTargetCents() const; // valid until isFinished(); asserts in Debug and returns 0.0f in Release if called on a finished engine (see the .cpp for why that guard is not redundant)

    AlankarSummary getSummary() const; // valid at any point, including mid-practice for a live partial readout

private:
    static constexpr int kBeatsPerStep = 1;
    // Caps the weight any single reading-to-reading gap can contribute, so a
    // stall or a pause in singing doesn't get counted as a long stretch of
    // (in)tune time. Comfortably longer than any realistic hop-to-hop gap
    // under normal load.
    static constexpr uint64_t kMaxReadingGapMs = 250;

    AlankarPattern pattern;
    int stepIndex = 0;
    int beatsIntoCurrentStep = 0;
    std::vector<AlankarStepResult> stepResults;

    // 0 = "no step boundary recorded yet" (still in step 0, or no beat has
    // ever advanced the engine). Otherwise the pipeline timestamp at which
    // the current step (or, for a finished engine, the step boundary that
    // finished it) began.
    uint64_t currentStepStartTimestampMs = 0;
    // 0 = "no reading processed yet".
    uint64_t lastReadingTimestampMs = 0;
};
