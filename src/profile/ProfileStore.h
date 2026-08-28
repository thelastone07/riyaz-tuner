// src/profile/ProfileStore.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

struct UserProfile
{
    juce::String name;
    float savedSaHz;
};

// Reads/writes a JSON array of profiles to a fixed file. The target file is
// dependency-injected (a constructor parameter) so tests use a temp file,
// never the real user-data location.
class ProfileStore
{
public:
    explicit ProfileStore (juce::File storageFileIn);

    // Empty if the file doesn't exist (first launch ever) or is malformed
    // (defensive - a corrupted settings file should degrade to "no
    // profiles", not crash the app). A genuinely corrupted file is an
    // accepted, documented v1 risk: this store always does a full
    // read-modify-write, so a save() after a "silently empty" read of a
    // corrupted file would overwrite it with only the newly-created
    // profile, losing whatever was unreadable. Given the tiny amount of
    // data involved (a handful of name+Hz pairs) and the personal, local
    // nature of this app, no backup/versioning is built for v1.
    std::vector<UserProfile> loadAll() const;

    // Upserts by name: replaces an existing profile with the same name, or
    // appends if none matches. Callers that want to PREVENT an accidental
    // overwrite (the "New Profile" creation flow) must check loadAll() for
    // a name collision themselves before calling this - save() itself
    // always upserts unconditionally.
    void save (const UserProfile& profile);

private:
    juce::File storageFile;
};

// The standard, real storage location: %APPDATA%\Riyaaz\profiles.json on
// Windows. Both MainComponent and Main.cpp's MainWindow construct their own
// independent ProfileStore instances pointed at this same file (no shared
// mutable state needed - ProfileStore does no in-memory caching, every
// loadAll()/save() is a fresh, independent file read/write) - this function
// is the one place the actual path lives, so the two can't drift apart.
juce::File getStandardProfileStoreFile();
