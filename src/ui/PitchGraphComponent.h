#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

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

    // Highlights a cents-from-Sa range as a band behind the trace - used by
    // Alankar practice mode to show the current target note's tolerance
    // range. nullopt clears it (no band drawn). centsRange is {low, high}.
    void setTargetBand (std::optional<std::pair<float, float>> centsRange);

    void paint (juce::Graphics& g) override;

private:
    PitchGraphPointBuffer buffer;
    std::optional<std::pair<float, float>> targetBand;
};
