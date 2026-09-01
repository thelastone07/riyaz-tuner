#include "SessionAnalytics.h"
#include <juce_core/juce_core.h>

namespace
{
    AlankarSessionRecord makeRecordWithPercent (float percent)
    {
        AlankarSessionRecord record;
        record.profileName = "Riyaaz";
        record.patternName = "Alankar 1";
        record.startingBpm = 60;
        record.completedAtEpochMs = 0;
        record.overallTimeInTunePercent = percent;
        return record;
    }
}

class SessionAnalyticsTests : public juce::UnitTest
{
public:
    SessionAnalyticsTests() : juce::UnitTest ("SessionAnalytics", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("fewer than 2 sessions -> NotEnoughData");
        {
            expect (computeTrend ({}).direction == TrendDirection::NotEnoughData);
            expect (computeTrend ({ makeRecordWithPercent (50.0f) }).direction == TrendDirection::NotEnoughData);
        }

        beginTest ("exactly 2 sessions, second well above the first -> Improving, with the correct delta");
        {
            std::vector<AlankarSessionRecord> sessions = { makeRecordWithPercent (60.0f), makeRecordWithPercent (80.0f) };
            const auto result = computeTrend (sessions);
            expect (result.direction == TrendDirection::Improving);
            expectWithinAbsoluteError (result.deltaPercent, 20.0f, 0.001f);
            expectEquals (result.recentCount, 1);
            expectEquals (result.previousCount, 1);
        }

        beginTest ("exactly 2 sessions, second well below the first -> Declining");
        {
            std::vector<AlankarSessionRecord> sessions = { makeRecordWithPercent (80.0f), makeRecordWithPercent (60.0f) };
            const auto result = computeTrend (sessions);
            expect (result.direction == TrendDirection::Declining);
            expectWithinAbsoluteError (result.deltaPercent, -20.0f, 0.001f);
        }

        beginTest ("a small delta within the steady threshold -> Steady, not Improving/Declining");
        {
            std::vector<AlankarSessionRecord> sessions = { makeRecordWithPercent (70.0f), makeRecordWithPercent (71.0f) };
            const auto result = computeTrend (sessions);
            expect (result.direction == TrendDirection::Steady);
        }

        beginTest ("more sessions than windowSize: only the last window vs. the window before it are compared");
        {
            // 5 sessions, windowSize=2: recent = last 2 (90, 90), previous = the
            // 2 immediately before that (50, 50) - the oldest session (10) is
            // outside both windows and must NOT affect the result.
            std::vector<AlankarSessionRecord> sessions = {
                makeRecordWithPercent (10.0f), // outside both windows
                makeRecordWithPercent (50.0f), makeRecordWithPercent (50.0f), // previous window
                makeRecordWithPercent (90.0f), makeRecordWithPercent (90.0f)  // recent window
            };
            const auto result = computeTrend (sessions, 2);
            expect (result.direction == TrendDirection::Improving);
            expectWithinAbsoluteError (result.deltaPercent, 40.0f, 0.001f);
            expectEquals (result.recentCount, 2);
            expectEquals (result.previousCount, 2);
        }

        beginTest ("total sessions <= windowSize: the recent window is capped at total-1, so a comparison is still produced, not NotEnoughData");
        {
            // 3 sessions, windowSize=5: recentCount = min(5, 3-1) = 2 (last
            // two: 20, 30 -> avg 25), previousCount = min(5, 1) = 1 (the one
            // remaining: 10). A user with only 3 sessions ever still sees a
            // trend, rather than having to wait until they reach windowSize.
            std::vector<AlankarSessionRecord> sessions = {
                makeRecordWithPercent (10.0f), makeRecordWithPercent (20.0f), makeRecordWithPercent (30.0f)
            };
            const auto result = computeTrend (sessions, 5);
            expect (result.direction == TrendDirection::Improving);
            expectEquals (result.recentCount, 2);
            expectEquals (result.previousCount, 1);
            expectWithinAbsoluteError (result.deltaPercent, 15.0f, 0.001f);
        }

        beginTest ("previous window shrinks (but stays >=1) when fewer than windowSize sessions remain before the recent window");
        {
            // 4 sessions, windowSize=3: recent = last 3, previous = only the 1
            // remaining before that - still a valid (if smaller) comparison.
            std::vector<AlankarSessionRecord> sessions = {
                makeRecordWithPercent (40.0f), // previous window (size 1)
                makeRecordWithPercent (90.0f), makeRecordWithPercent (90.0f), makeRecordWithPercent (90.0f) // recent window (size 3)
            };
            const auto result = computeTrend (sessions, 3);
            expect (result.direction == TrendDirection::Improving);
            expectEquals (result.recentCount, 3);
            expectEquals (result.previousCount, 1);
            expectWithinAbsoluteError (result.deltaPercent, 50.0f, 0.001f);
        }
    }
};

static SessionAnalyticsTests sessionAnalyticsTestsInstance;
