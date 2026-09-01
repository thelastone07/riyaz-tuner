// src/ui/SwarChipRowComponent.h
#pragma once
#include "../audio/swarmap/SwarMapper.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Shows an Alankar pattern's swar sequence as a row of circular chips, the
// current step highlighted - "where am I in this pattern" at a glance.
// Patterns run up to 40 steps (Alankar4/5's full ascending+descending
// sequence), far more than can render legibly at once in a 600px-wide
// window, so this shows a fixed-size WINDOW of chips centred on the
// current step (clamped to the pattern's bounds) rather than the whole
// sequence or a scrolling view - simplest thing that keeps "a few notes of
// context on either side" true at any pattern length or step position.
class SwarChipRowComponent : public juce::Component
{
public:
    // Rebuilds from a fresh sequence (e.g. a newly selected/started
    // pattern) and resets the current step to 0.
    void setSequence (std::vector<Swar> swarsIn);

    // Moves the highlighted step within the sequence set by setSequence() -
    // called on every beat-driven step change, without needing to re-supply
    // the (unchanged) sequence each time.
    void setCurrentIndex (int indexIn);

    void paint (juce::Graphics&) override;

private:
    static constexpr int kMaxVisibleChips = 13; // odd, so the window can centre exactly on the current step

    std::vector<Swar> swars;
    int currentIndex = 0;
};
