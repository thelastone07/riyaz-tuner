#include "SessionStore.h"
#include <juce_core/juce_core.h>

namespace
{
    AlankarSessionRecord makeRecord (const juce::String& profileName, const juce::String& patternName = "Alankar 1")
    {
        AlankarSessionRecord record;
        record.profileName = profileName;
        record.patternName = patternName;
        record.startingBpm = 60;
        record.completedAtEpochMs = 1798675200000;
        record.overallTimeInTunePercent = 82.5f;
        record.perSwarTimeInTunePercent = { { "S", 71.0f }, { "R", 90.0f } };
        return record;
    }
}

class SessionStoreTests : public juce::UnitTest
{
public:
    SessionStoreTests() : juce::UnitTest ("SessionStore", "Riyaaz") {}

    void runTest() override
    {
        // A fresh temp directory for these tests, deleted at the end, so
        // they never touch the real sessions.json and don't interfere with
        // each other.
        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("riyaaz_session_store_tests");
        tempDir.createDirectory();

        beginTest ("loadAllForProfile() returns empty for a file that doesn't exist yet");
        {
            const auto file = tempDir.getChildFile ("missing.json");
            file.deleteFile();
            SessionStore store (file);
            expect (store.loadAllForProfile ("Riyaaz").empty());
        }

        beginTest ("append() then loadAllForProfile() round-trips a record");
        {
            const auto file = tempDir.getChildFile ("roundtrip.json");
            file.deleteFile();
            SessionStore store (file);
            store.append (makeRecord ("Riyaaz"));

            const auto records = store.loadAllForProfile ("Riyaaz");
            expectEquals ((int) records.size(), 1);
            expectEquals (records[0].profileName, juce::String ("Riyaaz"));
            expectEquals (records[0].patternName, juce::String ("Alankar 1"));
            expectEquals (records[0].startingBpm, 60);
            expectEquals (records[0].completedAtEpochMs, (juce::int64) 1798675200000);
            expectWithinAbsoluteError (records[0].overallTimeInTunePercent, 82.5f, 0.001f);
            expectEquals ((int) records[0].perSwarTimeInTunePercent.size(), 2);
            expectEquals (records[0].perSwarTimeInTunePercent[0].first, juce::String ("S"));
            expectWithinAbsoluteError (records[0].perSwarTimeInTunePercent[0].second, 71.0f, 0.001f);
        }

        beginTest ("multiple append() calls accumulate rather than overwrite");
        {
            const auto file = tempDir.getChildFile ("accumulate.json");
            file.deleteFile();
            SessionStore store (file);
            store.append (makeRecord ("Riyaaz", "Alankar 1"));
            store.append (makeRecord ("Riyaaz", "Alankar 2"));

            const auto records = store.loadAllForProfile ("Riyaaz");
            expectEquals ((int) records.size(), 2);
            expectEquals (records[0].patternName, juce::String ("Alankar 1"));
            expectEquals (records[1].patternName, juce::String ("Alankar 2"));
        }

        beginTest ("loadAllForProfile() filters to just the requested profile");
        {
            const auto file = tempDir.getChildFile ("multiprofile.json");
            file.deleteFile();
            SessionStore store (file);
            store.append (makeRecord ("Alex"));
            store.append (makeRecord ("Sam"));
            store.append (makeRecord ("Alex"));

            const auto alexRecords = store.loadAllForProfile ("Alex");
            expectEquals ((int) alexRecords.size(), 2);

            const auto samRecords = store.loadAllForProfile ("Sam");
            expectEquals ((int) samRecords.size(), 1);

            const auto noneRecords = store.loadAllForProfile ("NobodyHome");
            expect (noneRecords.empty());
        }

        beginTest ("loadAllForProfile() returns empty for a malformed file, not a crash");
        {
            const auto file = tempDir.getChildFile ("malformed.json");
            file.replaceWithText ("{ this is not valid JSON at all !!!");
            SessionStore store (file);
            expect (store.loadAllForProfile ("Riyaaz").empty());
        }

        beginTest ("a fresh SessionStore instance sees a previous instance's append()");
        {
            const auto file = tempDir.getChildFile ("crosscheck.json");
            file.deleteFile();

            {
                SessionStore firstInstance (file);
                firstInstance.append (makeRecord ("Riyaaz"));
            }

            SessionStore secondInstance (file);
            const auto records = secondInstance.loadAllForProfile ("Riyaaz");
            expectEquals ((int) records.size(), 1);
        }

        tempDir.deleteRecursively();
    }
};

static SessionStoreTests sessionStoreTestsInstance;
