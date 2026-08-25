#pragma once
#include "KarplusStrongString.h"
#include <array>

enum class TanpuraTuning { PaSaSaSa, MaSaSaSa };

// Cycles a 4-string Karplus-Strong pattern tuned relative to the given Sa.
// PaSaSaSa: Pa (mandra, -500 cents), Sa (mandra, -1200 cents), Sa (madhya,
// 0 cents), Sa (madhya, 0 cents) - the standard tuning for Pa-emphasizing
// ragas. MaSaSaSa substitutes a Ma (mandra, -700 cents) for the Pa string,
// for Ma-emphasizing ragas. Cents follow this codebase's existing
// convention (see SwarMapper.h): Sa=0, Ma=500, Pa=700.
class TanpuraSynth
{
public:
    void prepare (double sampleRateIn, float saHz, TanpuraTuning tuning = TanpuraTuning::PaSaSaSa,
                 float secondsBetweenPlucks = 1.2f);
    float renderNextSample();

private:
    static constexpr int kNumStrings = 4;

    std::array<KarplusStrongString, kNumStrings> strings;
    std::array<float, kNumStrings> stringFrequencies {};
    double sampleRate = 0.0;
    double samplesPerPluck = 0.0;
    double samplesSinceLastPluck = 0.0;
    int nextStringIndex = 0;
    bool prepared = false;
};
