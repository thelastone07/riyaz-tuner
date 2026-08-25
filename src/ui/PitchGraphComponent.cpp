#include "PitchGraphComponent.h"

PitchGraphPointBuffer::PitchGraphPointBuffer (uint64_t maxAgeMsIn)
    : maxAgeMs (maxAgeMsIn)
{
}

void PitchGraphPointBuffer::addPoint (uint64_t timestampMs, float centsFromSa)
{
    // Self-healing guard against a non-monotonic timestamp. Timestamps come
    // from the pitch engine's own sample counter, which restarts at 0 whenever
    // the engine is re-prepared (a second prepareToPlay(), e.g. after an audio
    // device change). If old points with large timestamps were kept alongside
    // new small ones, the age-eviction test below could never fire again
    // (unbounded growth) and paint()'s unsigned newest-oldest subtraction would
    // underflow into garbage. Callers are expected to clear() explicitly on
    // re-prepare; this makes forgetting that non-fatal.
    if (! points.empty() && timestampMs < points.back().timestampMs)
        points.clear();

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

    const auto bounds = getLocalBounds().toFloat();

    // Cents axis: fixed +/-1200 cent range (a full octave either side of the
    // calibrated Sa), clamped and never auto-rescaling. This graph is a
    // MELODIC CONTOUR view - the shape of pitch movement across the octave a
    // phrase actually travels - not a fine intonation meter parked on one
    // swar. Horizontal gridlines mark every 100-cent swar boundary inside the
    // visible octave, drawn dimmer than the Sa line so they read as reference
    // rails behind the trace rather than as data.
    constexpr float kCentsRange = 1200.0f;

    const auto centsToY = [&bounds] (float cents)
    {
        return bounds.getCentreY() - (cents / kCentsRange) * (bounds.getHeight() * 0.5f);
    };

    // Swar gridlines at +/-100..+/-1100 cents (0 is the Sa line, drawn
    // brighter just below). Drawn before the trace so the trace sits on top.
    g.setColour (juce::Colour (0xff2b2b2b));
    for (int cents = -1100; cents <= 1100; cents += 100)
    {
        if (cents == 0)
            continue;

        g.drawHorizontalLine ((int) centsToY ((float) cents), bounds.getX(), bounds.getRight());
    }

    // The Sa line itself.
    g.setColour (juce::Colours::darkgrey);
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

    const auto& points = buffer.getPoints();
    if (points.size() < 2)
        return;

    const uint64_t newestMs = points.back().timestampMs;
    const uint64_t oldestMs = points.front().timestampMs;
    const float timeSpanMs = (float) juce::jmax<uint64_t> (1, newestMs - oldestMs);

    juce::Path path;
    bool started = false;

    for (const auto& p : points)
    {
        const float x = bounds.getX() + bounds.getWidth() * (float) (p.timestampMs - oldestMs) / timeSpanMs;
        const float clampedCents = juce::jlimit (-kCentsRange, kCentsRange, p.centsFromSa);
        const float y = centsToY (clampedCents);

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
}
