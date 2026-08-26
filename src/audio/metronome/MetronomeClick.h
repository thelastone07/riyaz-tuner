#pragma once
#include "TaalPattern.h" // for BeatType

// A single synthesized metronome tick: a damped sine burst. No delay line,
// no buffer of any kind - there is nothing to allocate or reserve, unlike
// KarplusStrongString.
class MetronomeClick
{
public:
    void trigger (BeatType type, double sampleRateIn);
    float renderNextSample();
    bool isSounding() const;

private:
    double phase = 0.0;
    double phaseIncrement = 0.0;
    float amplitude = 0.0f;
    float decayPerSample = 1.0f;
    int samplesRemaining = 0;
};
