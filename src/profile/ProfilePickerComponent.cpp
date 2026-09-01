// src/profile/ProfilePickerComponent.cpp
#include "ProfilePickerComponent.h"
#include "../ui/RiyaazLookAndFeel.h"

ProfilePickerComponent::ProfilePickerComponent (ProfileStore& storeIn) : store (storeIn)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Who's practicing?", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (RiyaazLookAndFeel::screenTitleFont());
    titleLabel.setColour (juce::Label::textColourId, RiyaazColours::primaryText);

    // Built once from the store's current contents - the list does not
    // refresh live, since nothing else can modify the store while this
    // component is showing (it's the only thing on screen before
    // MainComponent exists).
    for (auto& profile : store.loadAll())
    {
        ProfileRow row;
        row.profile = profile;

        row.label = std::make_unique<juce::Label>();
        row.label->setText (profile.name + "   Sa = " + juce::String (profile.savedSaHz, 1) + " Hz",
                             juce::dontSendNotification);
        row.label->setFont (RiyaazLookAndFeel::bodyFont());
        row.label->setColour (juce::Label::textColourId, RiyaazColours::primaryText);
        addAndMakeVisible (*row.label);

        row.recalibrateButton = std::make_unique<juce::TextButton> ("Recalibrate");
        row.recalibrateButton->onClick = [this, name = profile.name]
        {
            resolve (name, std::nullopt);
        };
        addAndMakeVisible (*row.recalibrateButton);

        row.useSavedButton = std::make_unique<juce::TextButton> ("Use saved Sa");
        // A "primary action" button - gold text even at rest, not just on
        // press. See RiyaazLookAndFeel::drawButtonText() for why this is a
        // per-button colour override rather than a second button style.
        row.useSavedButton->setColour (juce::TextButton::textColourOffId, RiyaazColours::gold);
        row.useSavedButton->onClick = [this, name = profile.name, saHz = profile.savedSaHz]
        {
            resolve (name, saHz);
        };
        addAndMakeVisible (*row.useSavedButton);

        profileRows.push_back (std::move (row));
    }

    addAndMakeVisible (newProfileLabel);
    newProfileLabel.setText ("NEW PROFILE", juce::dontSendNotification);

    addAndMakeVisible (newProfileNameEditor);
    newProfileNameEditor.setTextToShowWhenEmpty ("Enter a name", RiyaazColours::placeholderText);
    newProfileNameEditor.setFont (RiyaazLookAndFeel::bodyFont());
    newProfileNameEditor.setJustification (juce::Justification::centredLeft);

    addAndMakeVisible (createButton);
    createButton.setButtonText ("Create");
    createButton.setColour (juce::TextButton::textColourOffId, RiyaazColours::gold);
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
    errorLabel.setFont (RiyaazLookAndFeel::bodyFont());
    errorLabel.setColour (juce::Label::textColourId, RiyaazColours::terracotta);

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

    // A hairline divider under each existing-profile row, matching the
    // design system's row-separator treatment - drawn here rather than as
    // per-row Component borders since it's purely decorative and the rows
    // already know their own bounds from resized().
    g.setColour (RiyaazColours::border);
    for (auto& row : profileRows)
        g.drawHorizontalLine (row.label->getBounds().getY() - 1, 32.0f, (float) getWidth() - 32.0f);
    if (! profileRows.empty())
        g.drawHorizontalLine (profileRows.back().label->getBounds().getBottom() + 15, 32.0f, (float) getWidth() - 32.0f);
}

void ProfilePickerComponent::resized()
{
    auto area = getLocalBounds().reduced (32, 32);
    titleLabel.setBounds (area.removeFromTop (36));
    area.removeFromTop (16);

    for (auto& row : profileRows)
    {
        auto rowArea = area.removeFromTop (48);
        row.useSavedButton->setBounds (rowArea.removeFromRight (140).reduced (0, 6));
        rowArea.removeFromRight (12);
        row.recalibrateButton->setBounds (rowArea.removeFromRight (120).reduced (0, 6));
        row.label->setBounds (rowArea);
    }

    area.removeFromTop (16); // gap before the new-profile section

    newProfileLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (6);
    auto newProfileRow = area.removeFromTop (36);
    createButton.setBounds (newProfileRow.removeFromRight (100));
    newProfileRow.removeFromRight (12);
    newProfileNameEditor.setBounds (newProfileRow);

    errorLabel.setBounds (area.removeFromTop (30).withTrimmedTop (8));
}
