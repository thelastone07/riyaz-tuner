// src/ui/AnalyticsComponent.cpp
#include "AnalyticsComponent.h"

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
}

AnalyticsComponent::AnalyticsComponent()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Session History", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));

    addAndMakeVisible (trendLabel);
    trendLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (sessionListBox);
    sessionListBox.setRowHeight (22);
}

void AnalyticsComponent::setSessions (std::vector<AlankarSessionRecord> sessionsOldestFirstIn)
{
    sessionsOldestFirst = std::move (sessionsOldestFirstIn);

    trendLabel.setText (sessionsOldestFirst.empty()
                             ? juce::String ("No completed Alankar practice sessions yet - complete a practice run to start tracking your progress.")
                             : describeTrend (computeTrend (sessionsOldestFirst)),
                         juce::dontSendNotification);

    // ListBox pulls row content lazily through getNumRows()/paintListBoxItem()
    // above - this just tells it the row count may have changed.
    sessionListBox.updateContent();
    repaint();
}

void AnalyticsComponent::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds (area.removeFromTop (28));
    trendLabel.setBounds (area.removeFromTop (24));
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

    if (rowIsSelected)
        g.fillAll (juce::Colours::darkgrey);

    g.setColour (juce::Colours::white);
    const juce::String text = juce::Time (record.completedAtEpochMs).toString (true, true, false, true)
                               + "   " + record.patternName
                               + "   " + juce::String (record.startingBpm) + " BPM"
                               + "   " + juce::String (record.overallTimeInTunePercent, 1) + "% in tune";
    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft);
}
