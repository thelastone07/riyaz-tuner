#include "PitchGraphComponent.h"
#include "RiyaazLookAndFeel.h"
#include "../audio/swarmap/SwarMapper.h"
#include <array>

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

void PitchGraphComponent::setTargetBand (std::optional<std::pair<float, float>> centsRange)
{
    targetBand = centsRange;
    repaint();
}

void PitchGraphComponent::paint (juce::Graphics& g)
{
    g.fillAll (RiyaazColours::graphPanel);

    const auto bounds = getLocalBounds().toFloat();
    const juce::Font labelFont (juce::FontOptions ("Georgia", 9.0f, juce::Font::plain));

    // Cents axis: fixed +/-1400 cent range, clamped and never auto-rescaling.
    // This graph is a MELODIC CONTOUR view - the shape of pitch movement
    // across the octave a phrase actually travels - not a fine intonation
    // meter parked on one swar. Horizontal gridlines mark every 100-cent swar
    // boundary inside the visible octave, drawn dimmer than the Sa line so
    // they read as reference rails behind the trace rather than as data.
    //
    // 1400 rather than a bare octave (1200): Taar Sa is EXACTLY +1200 cents
    // and is the peak note of every alankar pattern, so at 1200 its +/-25c
    // target band clamped to [1175, 1200] - a half-height sliver flush with
    // the top edge - and a perfectly in-tune trace point at +1205c clamped to
    // the same top rail, looking sharp while being scored in tune. 200 cents
    // of headroom either side leaves room for the band plus normal overshoot,
    // and improves the melodic-contour view for anything sung past Taar Sa.
    // The gridline loop below is deliberately unaffected: it stops at +/-1100
    // by its own literal bounds and does not derive them from this constant.
    constexpr float kCentsRange = 1400.0f;

    const auto centsToY = [&bounds] (float cents)
    {
        return bounds.getCentreY() - (cents / kCentsRange) * (bounds.getHeight() * 0.5f);
    };

    // Swar gridlines at +/-100..+/-1100 cents (0 is the Sa line, drawn gold
    // just below), each labelled with its swar name so the reference lines
    // read as a scale, not just abstract ticks. Drawn before the trace so
    // the trace sits on top. Dashed lines and labels are drawn as two
    // separate passes - drawDashedLine and drawText both need g's current
    // colour set to something different, so interleaving them per-line
    // would mean re-setting colour twice per iteration for no benefit.
    static const std::array<float, 2> kDashLengths { 3.0f, 4.0f };

    g.setColour (RiyaazColours::border);
    for (int cents = -1100; cents <= 1100; cents += 100)
    {
        if (cents == 0)
            continue;

        const float y = centsToY ((float) cents);
        g.drawDashedLine (juce::Line<float> (bounds.getX(), y, bounds.getRight(), y),
                           kDashLengths.data(), (int) kDashLengths.size(), 1.0f);
    }

    g.setColour (RiyaazColours::mutedText);
    g.setFont (labelFont);
    for (int cents = -1100; cents <= 1100; cents += 100)
    {
        if (cents == 0)
            continue;

        const int semitoneIndex = ((cents / 100) % 12 + 12) % 12;
        const float y = centsToY ((float) cents);
        g.drawText (swarToString ((Swar) semitoneIndex), bounds.getX() + 4.0f, y - 7.0f, 24.0f, 14.0f,
                    juce::Justification::centredLeft);
    }

    // The Sa line itself - gold, the design system's reference-line accent.
    g.setColour (RiyaazColours::gold.withAlpha (0.6f));
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());
    g.setColour (RiyaazColours::gold);
    g.setFont (labelFont);
    g.drawText (swarToString (Swar::Sa), bounds.getX() + 4.0f, bounds.getCentreY() - 7.0f, 24.0f, 14.0f,
                juce::Justification::centredLeft);

    // Alankar practice mode's live target-note band, drawn after the
    // gridlines/Sa line but before the trace, so the trace is always
    // visible on top of it. Drawn even if there aren't 2 points yet (i.e.
    // before the function's early return below), so the target is visible
    // from the very start of a step.
    if (targetBand.has_value())
    {
        const float bandLowCents = juce::jlimit (-kCentsRange, kCentsRange, targetBand->first);
        const float bandHighCents = juce::jlimit (-kCentsRange, kCentsRange, targetBand->second);
        const float yHigh = centsToY (bandHighCents); // higher cents -> smaller Y (visually higher)
        const float yLow = centsToY (bandLowCents);

        g.setColour (RiyaazColours::gold.withAlpha (0.16f));
        g.fillRect (bounds.getX(), yHigh, bounds.getWidth(), yLow - yHigh);
    }

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

    g.setColour (RiyaazColours::indigo);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}
