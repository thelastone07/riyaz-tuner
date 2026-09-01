// src/ui/AnalyticsComponent.h
#pragma once
#include "../profile/SessionAnalytics.h"
#include "../profile/SessionStore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Shows a profile's completed Alankar practice history: a trend summary
// (improving/declining/steady, from computeTrend()) and a scrollable list
// of past sessions, most-recent-first. Purely a display of data handed to
// it via setSessions() - it reads nothing itself, so the caller
// (MainComponent) controls when the data is (re)loaded from SessionStore.
class AnalyticsComponent : public juce::Component, private juce::ListBoxModel
{
public:
    AnalyticsComponent();

    // sessionsOldestFirstIn: chronological order, as returned by
    // SessionStore::loadAllForProfile(). Empty is a valid, expected state
    // (a profile with no completed Alankar sessions yet).
    void setSessions (std::vector<AlankarSessionRecord> sessionsOldestFirstIn);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    juce::Label titleLabel;
    juce::Label trendLabel;
    juce::ListBox sessionListBox { "Session history", this };

    // Chronological (oldest-first), same order setSessions() was given -
    // paintListBoxItem() maps display row 0 (most recent) to the LAST
    // element, rather than re-sorting a second copy.
    std::vector<AlankarSessionRecord> sessionsOldestFirst;

    // How many of the most-recent rows (display rows 0..N-1) computeTrend()
    // actually averaged into the "recent" side of the trend - those rows are
    // highlighted in the list so the trend line and the rows backing it read
    // as one connected idea rather than two independent numbers.
    int recentWindowCount = 0;
};
