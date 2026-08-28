#include "ProfileStore.h"
#include <juce_core/juce_core.h>

class ProfileStoreTests : public juce::UnitTest
{
public:
    ProfileStoreTests() : juce::UnitTest ("ProfileStore", "Riyaaz") {}

    void runTest() override
    {
        // A fresh temp directory for these tests, deleted at the end, so
        // they never touch the real profiles.json and don't interfere with
        // each other.
        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("riyaaz_profile_store_tests");
        tempDir.createDirectory();

        beginTest ("loadAll() returns empty for a file that doesn't exist yet");
        {
            const auto file = tempDir.getChildFile ("missing.json");
            file.deleteFile();
            ProfileStore store (file);
            expect (store.loadAll().empty());
        }

        beginTest ("save() then loadAll() round-trips a profile");
        {
            const auto file = tempDir.getChildFile ("roundtrip.json");
            file.deleteFile();
            ProfileStore store (file);
            store.save ({ "Alex", 220.0f });

            const auto profiles = store.loadAll();
            expectEquals ((int) profiles.size(), 1);
            expectEquals (profiles[0].name, juce::String ("Alex"));
            expectWithinAbsoluteError (profiles[0].savedSaHz, 220.0f, 0.001f);
        }

        beginTest ("save() with an existing name upserts rather than duplicating");
        {
            const auto file = tempDir.getChildFile ("upsert.json");
            file.deleteFile();
            ProfileStore store (file);
            store.save ({ "Alex", 220.0f });
            store.save ({ "Alex", 233.1f }); // same name, different Sa

            const auto profiles = store.loadAll();
            expectEquals ((int) profiles.size(), 1);
            expectWithinAbsoluteError (profiles[0].savedSaHz, 233.1f, 0.001f);
        }

        beginTest ("save() with a different name appends, keeping both");
        {
            const auto file = tempDir.getChildFile ("multiple.json");
            file.deleteFile();
            ProfileStore store (file);
            store.save ({ "Alex", 220.0f });
            store.save ({ "Sam", 196.3f });

            const auto profiles = store.loadAll();
            expectEquals ((int) profiles.size(), 2);
        }

        beginTest ("loadAll() returns empty for a malformed file, not a crash");
        {
            const auto file = tempDir.getChildFile ("malformed.json");
            file.replaceWithText ("{ this is not valid JSON at all !!!");
            ProfileStore store (file);
            expect (store.loadAll().empty());
        }

        beginTest ("a fresh ProfileStore instance sees a previous instance's save()");
        {
            const auto file = tempDir.getChildFile ("crosscheck.json");
            file.deleteFile();

            {
                ProfileStore firstInstance (file);
                firstInstance.save ({ "Alex", 220.0f });
            }

            ProfileStore secondInstance (file);
            const auto profiles = secondInstance.loadAll();
            expectEquals ((int) profiles.size(), 1);
            expectEquals (profiles[0].name, juce::String ("Alex"));
        }

        tempDir.deleteRecursively();
    }
};

static ProfileStoreTests profileStoreTestsInstance;
