#include "BeatIndicatorComponent.h"

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

juce::Colour BeatIndicatorComponent::colourFor (BeatType type) const
{
    switch (type)
    {
        case BeatType::Sam:   return juce::Colours::red;
        case BeatType::Clap:  return juce::Colours::dodgerblue;
        case BeatType::Khali: return juce::Colours::grey;
        case BeatType::Plain: return juce::Colours::lightgrey;
    }

    jassertfalse; // unreachable - every BeatType enumerator is handled above
    return juce::Colours::lightgrey;
}

void BeatIndicatorComponent::timerCallback()
{
    const int totalBeats = source.getTotalBeatsElapsed();
    if (totalBeats != lastSeenTotalBeats)
    {
        lastSeenTotalBeats = totalBeats;
        flashIntensity = 1.0f;
    }
    else
    {
        flashIntensity = juce::jmax (0.0f, flashIntensity - 0.12f); // decays to 0 over ~8 ticks (~0.27s at 30Hz)
    }

    repaint();
}

void BeatIndicatorComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const int beatCount = displayPattern.beatCount();
    const int currentBeat = source.getCurrentBeatIndex();

    if (beatCount <= 1)
    {
        // PlainClick: no cycle to show progression through, so pulse a
        // single dot on every beat instead of highlighting a row position.
        const float diameter = juce::jmin (area.getWidth(), area.getHeight()) * (0.4f + 0.4f * flashIntensity);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());
        g.setColour (colourFor (BeatType::Plain).brighter (flashIntensity));
        g.fillEllipse (circle);
        return;
    }

    const float slotWidth = area.getWidth() / (float) beatCount;
    for (int i = 0; i < beatCount; ++i)
    {
        const BeatType type = displayPattern.classify (i);
        const bool isCurrent = (i == currentBeat);

        const float baseDiameter = juce::jmin (slotWidth, area.getHeight()) * 0.6f;
        const float diameter = isCurrent ? baseDiameter * (1.0f + 0.3f * flashIntensity) : baseDiameter;

        auto slot = area.withX (area.getX() + slotWidth * (float) i).withWidth (slotWidth);
        auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (slot.getCentre());

        auto colour = colourFor (type);
        if (isCurrent)
            colour = colour.brighter (flashIntensity);

        g.setColour (colour);
        g.fillEllipse (circle);
    }
}
