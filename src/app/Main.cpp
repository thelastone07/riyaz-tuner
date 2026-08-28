// src/app/Main.cpp
#include "MainComponent.h"
#include "../profile/ProfilePickerComponent.h"
#include <juce_gui_extra/juce_gui_extra.h>

class RiyaazApplication : public juce::JUCEApplication
{
public:
    RiyaazApplication() = default;

    const juce::String getApplicationName() override { return "Riyaaz"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour (juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons),
              profileStore (getStandardProfileStoreFile())
        {
            setUsingNativeTitleBar (true);

            auto* picker = new ProfilePickerComponent (profileStore);
            picker->onResolved = [this] (juce::String profileName, std::optional<float> knownSaHz)
            {
                // Deferred via callAsync, not called synchronously:
                // onResolved fires from inside one of the picker's own
                // button click handlers (a member function call still
                // executing on that Component). Calling setContentOwned()
                // synchronously here would delete the picker (the previous
                // content) while it is still on the call stack of its own
                // callback - a self-destruction-during-callback hazard.
                // Deferring one message-loop turn lets the click handler
                // unwind completely first. A plain [this] capture is safe
                // here (unlike inside MainComponent, which uses
                // Component::SafePointer for its own callAsync calls)
                // because MainWindow owns the picker and outlives it
                // deterministically - nothing external can delete
                // MainWindow between the click and this callback firing
                // except full application shutdown, which tears down the
                // pending message together with everything else.
                juce::MessageManager::callAsync ([this, profileName, knownSaHz]
                {
                    setContentOwned (new MainComponent (profileName, knownSaHz), true);
                    centreWithSize (getWidth(), getHeight());
                });
            };
            setContentOwned (picker, true);

            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        ProfileStore profileStore;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RiyaazApplication)
