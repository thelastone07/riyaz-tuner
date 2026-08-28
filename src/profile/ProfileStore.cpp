// src/profile/ProfileStore.cpp
#include "ProfileStore.h"

ProfileStore::ProfileStore (juce::File storageFileIn) : storageFile (storageFileIn)
{
}

std::vector<UserProfile> ProfileStore::loadAll() const
{
    std::vector<UserProfile> result;

    if (! storageFile.existsAsFile())
        return result;

    const juce::var parsed = juce::JSON::parse (storageFile);
    auto* array = parsed.getArray();
    if (array == nullptr)
        return result; // missing/malformed - degrade to "no profiles" rather than crash

    for (const auto& item : *array)
    {
        if (! item.isObject())
            continue;

        const juce::var nameVar = item.getProperty ("name", juce::var());
        const juce::var saHzVar = item.getProperty ("saHz", juce::var());
        if (! nameVar.isString() || ! (saHzVar.isDouble() || saHzVar.isInt()))
            continue;

        result.push_back ({ nameVar.toString(), (float) (double) saHzVar });
    }

    return result;
}

void ProfileStore::save (const UserProfile& profile)
{
    auto profiles = loadAll();

    bool replaced = false;
    for (auto& existing : profiles)
    {
        if (existing.name == profile.name)
        {
            existing.savedSaHz = profile.savedSaHz;
            replaced = true;
            break;
        }
    }
    if (! replaced)
        profiles.push_back (profile);

    juce::Array<juce::var> jsonArray;
    for (const auto& p : profiles)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", p.name);
        obj->setProperty ("saHz", (double) p.savedSaHz);
        jsonArray.add (juce::var (obj));
    }

    storageFile.getParentDirectory().createDirectory();
    storageFile.replaceWithText (juce::JSON::toString (juce::var (jsonArray)));
}

juce::File getStandardProfileStoreFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Riyaaz")
        .getChildFile ("profiles.json");
}
