// src/profile/SessionStore.cpp
#include "SessionStore.h"

namespace
{
    // All records regardless of profile - used both by loadAllForProfile()'s
    // filtering and by append()'s read-modify-write. Same "degrade to empty
    // rather than crash" contract as ProfileStore::loadAll().
    std::vector<AlankarSessionRecord> loadAllRecords (const juce::File& storageFile)
    {
        std::vector<AlankarSessionRecord> result;

        if (! storageFile.existsAsFile())
            return result;

        const juce::var parsed = juce::JSON::parse (storageFile);
        auto* array = parsed.getArray();
        if (array == nullptr)
            return result; // missing/malformed - degrade to "no sessions" rather than crash

        for (const auto& item : *array)
        {
            if (! item.isObject())
                continue;

            const juce::var profileNameVar = item.getProperty ("profileName", juce::var());
            const juce::var patternNameVar = item.getProperty ("patternName", juce::var());
            const juce::var startingBpmVar = item.getProperty ("startingBpm", juce::var());
            const juce::var completedAtVar = item.getProperty ("completedAtEpochMs", juce::var());
            const juce::var overallPercentVar = item.getProperty ("overallTimeInTunePercent", juce::var());
            const juce::var perSwarVar = item.getProperty ("perSwar", juce::var());

            if (! profileNameVar.isString() || ! patternNameVar.isString()
                || ! (startingBpmVar.isInt() || startingBpmVar.isDouble())
                || ! (completedAtVar.isInt() || completedAtVar.isInt64())
                || ! (overallPercentVar.isDouble() || overallPercentVar.isInt())
                || perSwarVar.getArray() == nullptr)
                continue;

            AlankarSessionRecord record;
            record.profileName = profileNameVar.toString();
            record.patternName = patternNameVar.toString();
            record.startingBpm = (int) startingBpmVar;
            record.completedAtEpochMs = (juce::int64) completedAtVar;
            record.overallTimeInTunePercent = (float) (double) overallPercentVar;

            for (const auto& entry : *perSwarVar.getArray())
            {
                if (! entry.isObject())
                    continue;

                const juce::var swarVar = entry.getProperty ("swar", juce::var());
                const juce::var percentVar = entry.getProperty ("percent", juce::var());
                if (! swarVar.isString() || ! (percentVar.isDouble() || percentVar.isInt()))
                    continue;

                record.perSwarTimeInTunePercent.push_back ({ swarVar.toString(), (float) (double) percentVar });
            }

            result.push_back (record);
        }

        return result;
    }
}

SessionStore::SessionStore (juce::File storageFileIn) : storageFile (storageFileIn)
{
}

std::vector<AlankarSessionRecord> SessionStore::loadAllForProfile (const juce::String& profileName) const
{
    std::vector<AlankarSessionRecord> result;
    for (auto& record : loadAllRecords (storageFile))
        if (record.profileName == profileName)
            result.push_back (std::move (record));
    return result;
}

void SessionStore::append (const AlankarSessionRecord& record)
{
    auto records = loadAllRecords (storageFile);
    records.push_back (record);

    juce::Array<juce::var> jsonArray;
    for (const auto& r : records)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("profileName", r.profileName);
        obj->setProperty ("patternName", r.patternName);
        obj->setProperty ("startingBpm", r.startingBpm);
        obj->setProperty ("completedAtEpochMs", r.completedAtEpochMs);
        obj->setProperty ("overallTimeInTunePercent", (double) r.overallTimeInTunePercent);

        juce::Array<juce::var> perSwarArray;
        for (const auto& entry : r.perSwarTimeInTunePercent)
        {
            auto* swarObj = new juce::DynamicObject();
            swarObj->setProperty ("swar", entry.first);
            swarObj->setProperty ("percent", (double) entry.second);
            perSwarArray.add (juce::var (swarObj));
        }
        obj->setProperty ("perSwar", perSwarArray);

        jsonArray.add (juce::var (obj));
    }

    storageFile.getParentDirectory().createDirectory();
    storageFile.replaceWithText (juce::JSON::toString (juce::var (jsonArray)));
}

juce::File getStandardSessionStoreFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Riyaaz")
        .getChildFile ("sessions.json");
}
