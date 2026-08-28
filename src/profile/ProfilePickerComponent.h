// src/profile/ProfilePickerComponent.h
#pragma once
#include "ProfileStore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ProfilePickerComponent : public juce::Component
{
public:
    // Fired exactly once, when a choice is made. chosenSaHz is nullopt for
    // "(re)calibrate" (both a brand new profile, and an existing profile's
    // "Recalibrate" action); it holds the saved Sa for "Use saved Sa".
    std::function<void (juce::String profileName, std::optional<float> chosenSaHz)> onResolved;

    explicit ProfilePickerComponent (ProfileStore& storeIn);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct ProfileRow
    {
        UserProfile profile;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::TextButton> recalibrateButton;
        std::unique_ptr<juce::TextButton> useSavedButton;
    };

    void resolve (juce::String profileName, std::optional<float> chosenSaHz);

    ProfileStore& store;
    juce::Label titleLabel;
    std::vector<ProfileRow> profileRows;

    juce::Label newProfileLabel;
    juce::TextEditor newProfileNameEditor;
    juce::TextButton createButton;
    juce::Label errorLabel;
};
