// src/practice/AlankarPattern.h
#pragma once
#include "../audio/swarmap/SwarMapper.h"
#include <juce_core/juce_core.h>
#include <vector>

// One of a small, fixed library of traditional Hindustani alankar practice
// patterns. Only the ascending half is stored per pattern - the descending
// half is derived by reversal at construction time, since it is always the
// exact reverse of ascending for these patterns (verified during design
// against two independent sources for all 5).
enum class AlankarPatternId { Alankar1, Alankar2, Alankar3, Alankar4, Alankar5 };

struct AlankarStep
{
    Swar swar;
    int octaveOffset; // 0 = Madhya (same octave as the calibrated Sa), +1 = Taar, -1 = Mandra
};

class AlankarPattern
{
public:
    explicit AlankarPattern (AlankarPatternId idIn);

    juce::String name() const;
    const std::vector<AlankarStep>& fullSequence() const;

private:
    AlankarPatternId id;
    std::vector<AlankarStep> steps;
};
