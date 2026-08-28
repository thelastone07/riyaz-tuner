// src/practice/AlankarPracticeEngine.h
#pragma once
#include "AlankarPattern.h"
#include <utility>
#include <vector>

struct AlankarStepResult
{
    Swar swar;
    int octaveOffset;
    int framesInTune;
    int framesTotal; // 0 if no confident pitch reading ever arrived during this step's window - happens at very high BPM relative to the pitch engine's ~64ms hop; reported as 0% for that step, not an error
};

struct AlankarSummary
{
    std::vector<AlankarStepResult> perStep;
    float overallTimeInTunePercent;                                // sum(framesInTune) / sum(framesTotal) across all steps, 0 if no frames at all
    std::vector<std::pair<Swar, float>> perSwarTimeInTunePercent;   // aggregated across all steps using that swar (any octave), sorted worst-to-best
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
    float currentStepTargetCents() const; // valid until isFinished()

    AlankarSummary getSummary() const; // valid at any point, including mid-practice for a live partial readout

private:
    static constexpr int kBeatsPerStep = 1;

    AlankarPattern pattern;
    int stepIndex = 0;
    int beatsIntoCurrentStep = 0;
    std::vector<AlankarStepResult> stepResults;
};
