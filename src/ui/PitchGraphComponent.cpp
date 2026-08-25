#include "PitchGraphComponent.h"

PitchGraphPointBuffer::PitchGraphPointBuffer (uint64_t maxAgeMsIn)
    : maxAgeMs (maxAgeMsIn)
{
}

void PitchGraphPointBuffer::addPoint (uint64_t timestampMs, float centsFromSa)
{
    points.push_back ({ timestampMs, centsFromSa });

    while (! points.empty() && points.front().timestampMs + maxAgeMs < timestampMs)
        points.pop_front();
}

void PitchGraphPointBuffer::clear()
{
    points.clear();
}

PitchGraphComponent::PitchGraphComponent()
{
}

void PitchGraphComponent::addPoint (uint64_t timestampMs, float centsFromSa)
{
    buffer.addPoint (timestampMs, centsFromSa);
    repaint();
}

void PitchGraphComponent::clear()
{
    buffer.clear();
    repaint();
}

void PitchGraphComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto& points = buffer.getPoints();
    if (points.size() < 2)
        return;

    const auto bounds = getLocalBounds().toFloat();
    const uint64_t newestMs = points.back().timestampMs;
    const uint64_t oldestMs = points.front().timestampMs;
    const float timeSpanMs = (float) juce::jmax<uint64_t> (1, newestMs - oldestMs);

    // Cents axis: fixed +/-100 cent range (one swar-width either side of
    // center), clamped - enough to see meend/gamak motion within a swar
    // without the graph rescaling constantly, which is not what
    // "no scoring yet, just a scrolling graph" calls for.
    constexpr float kCentsRange = 100.0f;

    juce::Path path;
    bool started = false;

    for (const auto& p : points)
    {
        const float x = bounds.getX() + bounds.getWidth() * (float) (p.timestampMs - oldestMs) / timeSpanMs;
        const float clampedCents = juce::jlimit (-kCentsRange, kCentsRange, p.centsFromSa);
        const float y = bounds.getCentreY() - (clampedCents / kCentsRange) * (bounds.getHeight() * 0.5f);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    g.setColour (juce::Colours::limegreen);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    g.setColour (juce::Colours::darkgrey);
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());
}
