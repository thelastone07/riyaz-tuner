// src/profile/SessionStore.h
#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

struct AlankarSessionRecord
{
    juce::String profileName;
    juce::String patternName;          // AlankarPattern::name(), e.g. "Alankar 1"
    int startingBpm = 0;                // BPM slider value at the moment Start was pressed
    juce::int64 completedAtEpochMs = 0; // juce::Time::getCurrentTime().toMilliseconds()
    float overallTimeInTunePercent = 0.0f;
    std::vector<std::pair<juce::String, float>> perSwarTimeInTunePercent; // swar name (swarToString), worst-to-best
};

// Reads/writes a JSON array of completed Alankar practice sessions to a
// fixed file, shared across all profiles on this machine (each record
// carries its own profileName). Append-only - unlike ProfileStore there is
// nothing to upsert; every completed run is a new, independent record. The
// target file is dependency-injected (a constructor parameter) so tests use
// a temp file, never the real user-data location.
class SessionStore
{
public:
    explicit SessionStore (juce::File storageFileIn);

    // Filtered to one profile's records, oldest-first (file order). Empty if
    // the file doesn't exist, is malformed, or the profile has no sessions
    // yet - same "degrade to empty, never crash" contract as ProfileStore.
    std::vector<AlankarSessionRecord> loadAllForProfile (const juce::String& profileName) const;

    // Always a full read-modify-write (load every record regardless of
    // profile, add the new one, write the whole array back) - the same
    // tradeoff ProfileStore::save() already accepts: tiny amount of data,
    // personal/local app, no backup/versioning needed for v1.
    void append (const AlankarSessionRecord& record);

private:
    juce::File storageFile;
};

// %APPDATA%\Riyaaz\sessions.json on Windows - alongside profiles.json.
juce::File getStandardSessionStoreFile();
