// src/practice/AlankarPracticeEngine.h
#pragma once
#include "AlankarPattern.h"
#include <utility>
#include <vector>

// NOTE ON WHAT THE "frames" NUMBERS BELOW ACTUALLY MEASURE. They are a COUNT
// OF CONFIDENT PITCH-WORKER UPDATES DELIVERED during a step - not a
// measurement of time. Two consequences, both deliberate for v1 and both
// worth knowing before these values are treated as durations:
//   - The delivery RATE is governed by pitch-worker and message-thread
//     scheduling (PitchWorker coalesces every currently-ready FIFO sample
//     into one process() call and its async delivery is latest-wins), not by
//     elapsed time. A step that coincided with a worker stall or a busy
//     message thread simply contributes fewer frames.
//   - Attribution is by DELIVERY time, not by the moment the pitch was
//     actually sung: onPitchReading() carries no timestamp, so a reading is
//     credited to whichever step is current when it arrives. Inference plus
//     the async hop means the tail of one step's audio is routinely credited
//     to the next step.
// So the percentages derived from them are an APPROXIMATION of time-in-tune,
// not an exact one - good enough to rank swars against each other within a
// run, not a precise duration measure. (Timestamped, capture-time attribution
// is a deliberate later change; it is an API break across all three layers.)
struct AlankarStepResult
{
    Swar swar = Swar::Sa;
    int octaveOffset = 0;
    int framesInTune = 0;
    int framesTotal = 0; // 0 if no confident pitch update was ever delivered during this step - happens at very high BPM relative to the pitch engine's ~64ms hop, or if the worker stalled across the whole step; reported as 0% for that step, not an error
};

struct AlankarSummary
{
    std::vector<AlankarStepResult> perStep;
    // sum(framesInTune) / sum(framesTotal) across all steps, 0 if no frames at
    // all. Frame-COUNT weighted, not time-weighted - see the note above: steps
    // that happened to receive more delivered updates carry more weight.
    float overallTimeInTunePercent = 0.0f;
    // Aggregated across all steps using that swar (any octave), sorted
    // worst-to-best. Inherits the same frame-count weighting and
    // delivery-time attribution as the overall figure above, so treat the
    // ordering as indicative rather than exact.
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent;
};

// Pure logic, externally driven - no audio/JUCE-Timer dependency. The
// caller (MainComponent) is responsible for calling onBeatElapsed() once
// per real metronome beat boundary, and onPitchReading() for every
// confident pitch frame while practice is active.
class AlankarPracticeEngine
{
public:
    // Exposed so callers (e.g. MainComponent's target-band display) use the
    // exact same tolerance value this class scores against, not a
    // duplicated magic number.
    static constexpr float kInTuneToleranceCents = 25.0f;

    explicit AlankarPracticeEngine (AlankarPatternId patternId);

    void onBeatElapsed();
    void onPitchReading (float centsFromSa);

    bool isFinished() const;
    int currentStepIndex() const;  // valid until isFinished()
    int totalSteps() const;
    float currentStepTargetCents() const; // valid until isFinished(); asserts in Debug and returns 0.0f in Release if called on a finished engine (see the .cpp for why that guard is not redundant)

    AlankarSummary getSummary() const; // valid at any point, including mid-practice for a live partial readout

private:
    static constexpr int kBeatsPerStep = 1;

    AlankarPattern pattern;
    int stepIndex = 0;
    int beatsIntoCurrentStep = 0;
    std::vector<AlankarStepResult> stepResults;
};
