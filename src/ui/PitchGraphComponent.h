#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <deque>

struct PitchGraphPoint
{
    uint64_t timestampMs;
    float centsFromSa;
};

class PitchGraphPointBuffer
{
public:
    explicit PitchGraphPointBuffer (uint64_t maxAgeMsIn = 8000);

    void addPoint (uint64_t timestampMs, float centsFromSa);
    void clear();
    const std::deque<PitchGraphPoint>& getPoints() const { return points; }

private:
    uint64_t maxAgeMs;
    std::deque<PitchGraphPoint> points;
};

class PitchGraphComponent : public juce::Component
{
public:
    PitchGraphComponent();

    void addPoint (uint64_t timestampMs, float centsFromSa);
    void clear();

    void paint (juce::Graphics& g) override;

private:
    PitchGraphPointBuffer buffer;
};
