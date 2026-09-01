#include "BeatIndicatorComponent.h"
#include "RiyaazLookAndFeel.h"

BeatIndicatorComponent::BeatIndicatorComponent (MetronomeAudioSource& sourceIn)
    : source (sourceIn)
{
    startTimerHz (30);
}

BeatIndicatorComponent::~BeatIndicatorComponent()
{
    stopTimer();
}

void BeatIndicatorComponent::setTaal (TaalType newType)
{
    displayPattern = TaalPattern (newType);
    repaint();
}

// Persistent (not flash-dependent) styling per beat type - a taal cycle's
// structure should read at a glance even before anything fires: Sam (the
// downbeat) and Khali (the silent vibhaag boundary) get a larger, coloured
// ring; Clap and Plain stay small dots, distinguished only by a slightly
// brighter border on Clap.
BeatIndicatorComponent::BeatStyle BeatIndicatorComponent::styleFor (BeatType type) const
{
    switch (type)
    {
        case BeatType::Sam:   return { 1.35f, RiyaazColours::gold,    2.0f, RiyaazColours::surface };
        case BeatType::Khali: return { 1.2f,  RiyaazColours::indigo,  2.0f, RiyaazColours::canvas };
        case BeatType::Clap:  return { 1.0f,  RiyaazColours::mutedText, 1.0f, RiyaazColours::surface };
        case BeatType::Plain: return { 1.0f,  RiyaazColours::border, 1.0f, RiyaazColours::surface };
    }

    jassertfalse; // unreachable - every BeatType enumerator is handled above
    return { 1.0f, RiyaazColours::border, 1.0f, RiyaazColours::surface };
}

void BeatIndicatorComponent::timerCallback()
{
    const int totalBeats = source.getTotalBeatsElapsed();
    const float previousIntensity = flashIntensity;
    bool beatChanged = false;

    if (totalBeats != lastSeenTotalBeats)
    {
        lastSeenTotalBeats = totalBeats;
        flashIntensity = 1.0f;
        beatChanged = true;
    }
    else
    {
        flashIntensity = juce::jmax (0.0f, flashIntensity - 0.12f); // decays to 0 over ~8 ticks (~0.27s at 30Hz)
    }

    // Only repaint when something the paint actually depends on moved. The
    // timer runs for the component's whole lifetime, but the metronome
    // defaults to stopped and this component is idle most of the time - once
    // flashIntensity has decayed to 0 with no beat firing, every further
    // repaint would be pixel-identical. This is the app's only always-on
    // repaint loop (PitchGraphComponent has no timer and repaints only on
    // addPoint()), so leaving it unguarded would put a permanent idle-CPU
    // and wakeup floor under an app that previously had none.
    //
    // getCurrentBeatIndex() needs no separate check: the beat index cannot
    // change without getTotalBeatsElapsed() also incrementing, since
    // triggerBeat() advances both together.
    if (beatChanged || flashIntensity != previousIntensity)
        repaint();
}

void BeatIndicatorComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const int beatCount = displayPattern.beatCount();
    const int currentBeat = source.getCurrentBeatIndex();

    const auto fillLit = [&] (juce::Rectangle<float> circle)
    {
        juce::ColourGradient gradient (RiyaazColours::goldLitInner, circle.getX() + circle.getWidth() * 0.35f,
                                        circle.getY() + circle.getHeight() * 0.3f,
                                        RiyaazColours::goldLitOuter, circle.getCentreX(), circle.getBottom(), true);
        gradient.addColour (0.65, RiyaazColours::gold);
        g.setGradientFill (gradient);
        g.fillEllipse (circle);
    };

    if (beatCount <= 1)
    {
        // PlainClick: no cycle to show progression through, so pulse a
        // single dot on every beat instead of highlighting a row position.
        const float diameter = juce::jmin (area.getWidth(), area.getHeight()) * (0.4f + 0.4f * flashIntensity);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());

        if (flashIntensity > 0.05f)
        {
            fillLit (circle);
        }
        else
        {
            g.setColour (RiyaazColours::surface);
            g.fillEllipse (circle);
            g.setColour (RiyaazColours::border);
            g.drawEllipse (circle, 1.0f);
        }
        return;
    }

    const float slotWidth = area.getWidth() / (float) beatCount;
    for (int i = 0; i < beatCount; ++i)
    {
        const BeatType type = displayPattern.classify (i);
        const bool isCurrent = (i == currentBeat);
        const auto style = styleFor (type);

        const float baseDiameter = juce::jmin (slotWidth, area.getHeight()) * 0.55f * style.relativeDiameterScale;
        const float diameter = isCurrent ? baseDiameter * (1.0f + 0.2f * flashIntensity) : baseDiameter;

        auto slot = area.withX (area.getX() + slotWidth * (float) i).withWidth (slotWidth);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (slot.getCentre());

        if (isCurrent && flashIntensity > 0.05f)
        {
            fillLit (circle);
        }
        else
        {
            g.setColour (style.fillColour);
            g.fillEllipse (circle);
        }

        g.setColour (style.borderColour);
        g.drawEllipse (circle, style.borderWidth);
    }
}
