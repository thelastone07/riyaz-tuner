#pragma once
#include "../audio/metronome/TaalPattern.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include <juce_gui_basics/juce_gui_basics.h>

// A row of lights, one per beat in the current taal cycle, colored/sized by
// BeatType and highlighted at the current beat - or, for PlainClick
// (beatCount()==1, no cycle to show progression through), a single dot that
// pulses on every beat instead.
class BeatIndicatorComponent : public juce::Component, private juce::Timer
{
public:
    explicit BeatIndicatorComponent (MetronomeAudioSource& sourceIn);
    ~BeatIndicatorComponent() override;

    // Re-derives this component's own display pattern from the caller's
    // taal selection - this component does not read taal state from the
    // audio thread, only the current beat index/total.
    void setTaal (TaalType newType);

    void paint (juce::Graphics& g) override;

private:
    // Persistent (not flash-dependent) look for a beat type: how much bigger
    // than the base dot it is, its border colour/width, and its unlit fill.
    struct BeatStyle
    {
        float relativeDiameterScale;
        juce::Colour borderColour;
        float borderWidth;
        juce::Colour fillColour;
    };

    void timerCallback() override;
    BeatStyle styleFor (BeatType type) const;

    MetronomeAudioSource& source;
    TaalPattern displayPattern { TaalType::PlainClick };

    int lastSeenTotalBeats = 0;
    float flashIntensity = 0.0f; // 0 (no flash) to 1 (just fired), decays each timer tick
};
