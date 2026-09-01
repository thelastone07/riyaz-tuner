// src/ui/AnalyticsComponent.cpp
#include "AnalyticsComponent.h"
#include "RiyaazLookAndFeel.h"

namespace
{
    juce::String describeTrend (const TrendResult& trend)
    {
        switch (trend.direction)
        {
            case TrendDirection::NotEnoughData:
                return "Not enough sessions yet to show a trend (need at least 2 completed practice runs).";

            case TrendDirection::Improving:
                return "Trend: Improving (+" + juce::String (trend.deltaPercent, 1) + " pts, last "
                       + juce::String (trend.recentCount) + " vs. previous " + juce::String (trend.previousCount) + " sessions)";

            case TrendDirection::Declining:
                return "Trend: Declining (" + juce::String (trend.deltaPercent, 1) + " pts, last "
                       + juce::String (trend.recentCount) + " vs. previous " + juce::String (trend.previousCount) + " sessions)";

            case TrendDirection::Steady:
                return "Trend: Steady (" + juce::String (trend.deltaPercent, 1) + " pts, last "
                       + juce::String (trend.recentCount) + " vs. previous " + juce::String (trend.previousCount) + " sessions)";
        }

        jassertfalse; // unreachable - every TrendDirection enumerator is handled above
        return {};
    }

    juce::Colour colourForTrend (TrendDirection direction)
    {
        switch (direction)
        {
            case TrendDirection::Improving: return RiyaazColours::gold;
            case TrendDirection::Declining: return RiyaazColours::terracotta;
            case TrendDirection::Steady:
            case TrendDirection::NotEnoughData: return RiyaazColours::mutedText;
        }

        jassertfalse;
        return RiyaazColours::mutedText;
    }
}

AnalyticsComponent::AnalyticsComponent()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Session History", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions ("Georgia", 18.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, RiyaazColours::primaryText);

    addAndMakeVisible (trendLabel);
    trendLabel.setJustificationType (juce::Justification::centredLeft);
    trendLabel.setFont (RiyaazLookAndFeel::smallMetaFont());

    addAndMakeVisible (sessionListBox);
    sessionListBox.setRowHeight (26);
}

void AnalyticsComponent::setSessions (std::vector<AlankarSessionRecord> sessionsOldestFirstIn)
{
    sessionsOldestFirst = std::move (sessionsOldestFirstIn);

    if (sessionsOldestFirst.empty())
    {
        trendLabel.setText ("No completed Alankar practice sessions yet - complete a practice run to start tracking your progress.",
                             juce::dontSendNotification);
        trendLabel.setColour (juce::Label::textColourId, RiyaazColours::mutedText);
        recentWindowCount = 0;
    }
    else
    {
        const auto trend = computeTrend (sessionsOldestFirst);
        trendLabel.setText (describeTrend (trend), juce::dontSendNotification);
        trendLabel.setColour (juce::Label::textColourId, colourForTrend (trend.direction));
        recentWindowCount = trend.recentCount;
    }

    // ListBox pulls row content lazily through getNumRows()/paintListBoxItem()
    // above - this just tells it the row count may have changed.
    sessionListBox.updateContent();
    repaint();
}

void AnalyticsComponent::paint (juce::Graphics& g)
{
    // A bordered panel matching the pitch graph's container treatment - the
    // two are mutually exclusive but occupy the same slot in MainComponent
    // (see resized() there), so they should read as the same kind of thing.
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (RiyaazColours::graphPanel);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (RiyaazColours::border);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void AnalyticsComponent::resized()
{
    auto area = getLocalBounds().reduced (18, 14);
    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    trendLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (8);
    sessionListBox.setBounds (area);
}

int AnalyticsComponent::getNumRows()
{
    return (int) sessionsOldestFirst.size();
}

void AnalyticsComponent::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, (int) sessionsOldestFirst.size()))
        return; // ListBox can ask for a stale row index mid-update; nothing valid to draw

    // Most-recent-first display over a chronological (oldest-first) vector:
    // row 0 is the last element.
    const auto& record = sessionsOldestFirst[sessionsOldestFirst.size() - 1 - (size_t) rowNumber];

    g.setColour (RiyaazColours::border);
    g.drawHorizontalLine (0, 0.0f, (float) width);

    if (rowIsSelected)
    {
        g.setColour (RiyaazColours::surfaceHover);
        g.fillRect (0, 1, width, height - 1);
    }

    // Rows inside the trend's "recent" window get gold-highlighted values,
    // tying the trend line above to the specific rows it was computed from.
    const bool inRecentWindow = rowNumber < recentWindowCount;

    g.setFont (RiyaazLookAndFeel::smallMetaFont());
    g.setColour (RiyaazColours::primaryText);
    g.drawText (juce::Time (record.completedAtEpochMs).toString (true, true, false, true),
                6, 0, (width * 4) / 10, height, juce::Justification::centredLeft);

    g.setColour (RiyaazColours::mutedText);
    g.drawText (record.patternName, (width * 4) / 10, 0, width / 4, height, juce::Justification::centredLeft);
    g.drawText (juce::String (record.startingBpm) + " BPM", (width * 13) / 20, 0, width / 5, height,
                juce::Justification::centredLeft);

    g.setColour (inRecentWindow ? RiyaazColours::gold : RiyaazColours::primaryText);
    g.drawText (juce::String (record.overallTimeInTunePercent, 1) + "% in tune",
                width - (width * 3) / 10 - 6, 0, (width * 3) / 10, height, juce::Justification::centredRight);
}
