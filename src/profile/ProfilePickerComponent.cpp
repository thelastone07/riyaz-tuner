// src/profile/ProfilePickerComponent.cpp
#include "ProfilePickerComponent.h"

ProfilePickerComponent::ProfilePickerComponent (ProfileStore& storeIn) : store (storeIn)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Who's practicing?", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f)));

    // Built once from the store's current contents - the list does not
    // refresh live, since nothing else can modify the store while this
    // component is showing (it's the only thing on screen before
    // MainComponent exists).
    for (auto& profile : store.loadAll())
    {
        ProfileRow row;
        row.profile = profile;

        row.label = std::make_unique<juce::Label>();
        row.label->setText (profile.name + "  (Sa = " + juce::String (profile.savedSaHz, 1) + "Hz)",
                             juce::dontSendNotification);
        addAndMakeVisible (*row.label);

        row.recalibrateButton = std::make_unique<juce::TextButton> ("Recalibrate");
        row.recalibrateButton->onClick = [this, name = profile.name]
        {
            resolve (name, std::nullopt);
        };
        addAndMakeVisible (*row.recalibrateButton);

        row.useSavedButton = std::make_unique<juce::TextButton> ("Use saved Sa");
        row.useSavedButton->onClick = [this, name = profile.name, saHz = profile.savedSaHz]
        {
            resolve (name, saHz);
        };
        addAndMakeVisible (*row.useSavedButton);

        profileRows.push_back (std::move (row));
    }

    addAndMakeVisible (newProfileLabel);
    newProfileLabel.setText ("New profile:", juce::dontSendNotification);

    addAndMakeVisible (newProfileNameEditor);
    newProfileNameEditor.setTextToShowWhenEmpty ("Enter a name", juce::Colours::grey);

    addAndMakeVisible (createButton);
    createButton.setButtonText ("Create");
    createButton.onClick = [this]
    {
        const auto name = newProfileNameEditor.getText().trim();
        if (name.isEmpty())
        {
            errorLabel.setText ("Enter a name first.", juce::dontSendNotification);
            return;
        }

        for (const auto& row : profileRows)
        {
            if (row.profile.name == name)
            {
                errorLabel.setText ("That name is already taken.", juce::dontSendNotification);
                return;
            }
        }

        resolve (name, std::nullopt); // a new profile always (re)calibrates - there is no saved Sa to reuse yet
    };

    addAndMakeVisible (errorLabel);
    errorLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);

    setSize (600, 560); // matches MainComponent's own window size, so the Task 5 swap doesn't visibly resize the window
}

void ProfilePickerComponent::resolve (juce::String profileName, std::optional<float> chosenSaHz)
{
    if (onResolved != nullptr)
        onResolved (profileName, chosenSaHz);
}

void ProfilePickerComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void ProfilePickerComponent::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds (area.removeFromTop (60));

    for (auto& row : profileRows)
    {
        auto rowArea = area.removeFromTop (40);
        row.label->setBounds (rowArea.removeFromLeft (300).reduced (4));
        row.recalibrateButton->setBounds (rowArea.removeFromLeft (140).reduced (4));
        row.useSavedButton->setBounds (rowArea.removeFromLeft (140).reduced (4));
    }

    area.removeFromTop (20); // gap before the new-profile section

    auto newProfileRow = area.removeFromTop (40);
    newProfileLabel.setBounds (newProfileRow.removeFromLeft (100).reduced (4));
    newProfileNameEditor.setBounds (newProfileRow.removeFromLeft (250).reduced (4));
    createButton.setBounds (newProfileRow.removeFromLeft (100).reduced (4));

    errorLabel.setBounds (area.removeFromTop (30).reduced (4));
}
