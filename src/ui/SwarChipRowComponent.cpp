// src/ui/SwarChipRowComponent.cpp
#include "SwarChipRowComponent.h"
#include "RiyaazLookAndFeel.h"

void SwarChipRowComponent::setSequence (std::vector<Swar> swarsIn)
{
    swars = std::move (swarsIn);
    currentIndex = 0;
    repaint();
}

void SwarChipRowComponent::setCurrentIndex (int indexIn)
{
    currentIndex = indexIn;
    repaint();
}

void SwarChipRowComponent::paint (juce::Graphics& g)
{
    if (swars.empty())
        return;

    const int total = (int) swars.size();
    const int windowSize = juce::jmin (kMaxVisibleChips, total);
    // Centred on currentIndex, clamped so the window never runs past either
    // end of the sequence (e.g. currentIndex near 0 or near the last step).
    const int windowStart = juce::jlimit (0, total - windowSize, currentIndex - windowSize / 2);

    const auto bounds = getLocalBounds().toFloat();
    const float chipDiameter = juce::jmin (26.0f, bounds.getHeight());
    const float pitch = bounds.getWidth() / (float) windowSize;
    const juce::Font font (juce::FontOptions ("Georgia", 12.0f, juce::Font::bold));

    for (int i = 0; i < windowSize; ++i)
    {
        const int swarIndex = windowStart + i;
        const bool isCurrent = (swarIndex == currentIndex);

        const auto slot = juce::Rectangle<float> (pitch, bounds.getHeight()).withX (bounds.getX() + pitch * (float) i);
        const auto circle = juce::Rectangle<float> (chipDiameter, chipDiameter).withCentre (slot.getCentre());

        if (isCurrent)
        {
            juce::ColourGradient gradient (RiyaazColours::goldLitInner, circle.getX() + circle.getWidth() * 0.35f,
                                            circle.getY() + circle.getHeight() * 0.3f,
                                            RiyaazColours::goldLitOuter, circle.getCentreX(), circle.getBottom(), true);
            gradient.addColour (0.65, RiyaazColours::gold);
            g.setGradientFill (gradient);
            g.fillEllipse (circle);
            g.setColour (RiyaazColours::gold);
            g.drawEllipse (circle, 2.0f);
            g.setColour (RiyaazColours::canvas); // dark text reads on the bright gold fill
        }
        else
        {
            g.setColour (RiyaazColours::surface);
            g.fillEllipse (circle);
            g.setColour (RiyaazColours::border);
            g.drawEllipse (circle, 1.0f);
            g.setColour (RiyaazColours::mutedText);
        }

        g.setFont (font);
        g.drawText (swarToString (swars[(size_t) swarIndex]), circle.toNearestInt(),
                    juce::Justification::centred);
    }
}
