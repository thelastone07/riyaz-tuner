// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/tanpura/TanpuraAudioSource.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
#include "../ui/BeatIndicatorComponent.h"
#include "../ui/AnalyticsComponent.h"
#include "../ui/SwarChipRowComponent.h"
#include "../practice/AlankarPattern.h"
#include "../practice/AlankarPracticeEngine.h"
#include "../profile/ProfileStore.h"
#include "../profile/SessionStore.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>
#include <optional>

class MainComponent : public juce::AudioAppComponent, private PitchWorker::Listener, private juce::Timer
{
public:
    explicit MainComponent (const juce::String& profileNameIn, std::optional<float> knownSaHzIn);
    ~MainComponent() override;

    // Fired when the user presses the Home button, to go back to the profile
    // picker (recalibrate, switch profile, or use a different saved Sa). Not
    // handled here - MainComponent has no knowledge of MainWindow/the picker -
    // MainWindow wires this to swap its content back, the same way
    // ProfilePickerComponent::onResolved is wired the other direction.
    std::function<void()> onRequestHome;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override; // drives Alankar practice mode's beat-driven step advancement
    void pitchWorkerUpdate (const PitchPipelineUpdate& update) override;
    static TaalType taalTypeForComboId (int comboId);

    // Enable/disable, in one place, the controls that must not change while a
    // practice run is live. Any metronome clock reset other than the one
    // "Start Alankar Practice" itself arms injects an unabsorbed triggerBeat(0)
    // into timerCallback()'s drain loop, silently advancing the engine one step
    // out of alignment with the audible beat - so the two user-reachable ways
    // to arm one mid-practice (the metronome's own start/stop button, and a
    // taal change) are simply made unavailable while practice is live. The
    // pattern combo is locked for a different reason: the engine snapshots its
    // pattern at construction, so leaving it interactive lets the displayed
    // selection disagree with what is actually running.
    void setAlankarPracticeControlsLocked (bool locked);

    // Tear down any Alankar practice (running or finished) and put the UI back
    // to its no-practice state: stop the pacing click, drop the engine, clear
    // the target band and results label, unlock the controls above. Shared by
    // the two places that must do exactly this - leaving Alankar mode, and the
    // pitch pipeline re-entering Calibrating - so the two can't drift apart.
    void cancelAlankarPractice();

    // Re-derives swarChipRow's sequence from whichever pattern is currently
    // selected in alankarPatternCombo, with step 0 highlighted (i.e. "no
    // run active yet"). Called both when Alankar mode is first selected and
    // whenever the pattern combo's own selection changes - the two call
    // sites would otherwise have to stay in sync by hand.
    void refreshSwarChipRowFromSelectedPattern();

    // metronomeStartStopButton is now an icon-only square (see its
    // declaration below) - this is the one place its two glyphs are picked,
    // so the toggle handler and alankarStartButton.onClick (which also
    // forces the button into its "running" state) can't drift apart.
    void updateMetronomeStartStopButtonText();

    // Same reasoning as updateMetronomeStartStopButtonText() above, for
    // tanpuraToggleButton's own two glyphs.
    void updateTanpuraToggleButtonText();

    // Resolves the CREPE model path relative to the running executable (where
    // riyaaz_copy_crepe_model() in CMakeLists.txt places it) rather than the
    // process's current working directory, which a desktop shortcut has no
    // guarantee points at the repo root. Falls back to walking up from the
    // working directory, for the dev workflow of running the freshly-built
    // exe directly from a terminal at the repo root or a build/ subdirectory.
    static juce::String resolveCrepeModelPath();

    juce::String activeProfileName;
    std::optional<float> pendingKnownSaHz;
    ProfileStore profileStore { getStandardProfileStoreFile() };
    SessionStore sessionStore { getStandardSessionStoreFile() };

    CrepePitchEngine engine { resolveCrepeModelPath() };
    std::unique_ptr<PitchPipeline> pipeline;   // constructed in prepareToPlay(), once the real sample rate is known
    std::unique_ptr<PitchWorker> worker;       // ditto

    // Owned directly (not a unique_ptr like pipeline/worker): neither has a
    // construction-time dependency on the sample rate - prepareToPlay()
    // hands them the rate later, and both are safe to render from before
    // then (tanpuraSource outputs silence while disabled; metronomeSource
    // does too, since it also defaults to disabled).
    TanpuraAudioSource tanpuraSource;
    MetronomeAudioSource metronomeSource;

    // Top-left, alongside statusLabel (see resized()) - the only way back to
    // the profile picker (recalibrate Sa, or switch to/create another
    // profile) once MainComponent is up; there is otherwise no in-app path
    // back to it short of quitting and relaunching.
    juce::TextButton homeButton;
    juce::Label statusLabel;
    juce::Slider tanpuraVolumeSlider;
    juce::Label tanpuraVolumeLabel;
    // Manual on/off for the drone - tanpuraSource no longer auto-enables
    // itself the moment calibration succeeds (see pitchWorkerUpdate()), so
    // this is the only way to start it. Same icon-only square-button
    // convention as metronomeStartStopButton.
    juce::TextButton tanpuraToggleButton;
    bool tanpuraEnabled = false;
    juce::ComboBox metronomeTaalCombo;
    juce::Label metronomeTaalLabel;
    juce::Slider metronomeBpmSlider;
    juce::Label metronomeBpmLabel;
    // A small square icon button (glyph swaps between the two - see
    // updateMetronomeStartStopButtonText() in the .cpp) rather than a full-
    // width "Start metronome"/"Stop metronome" label, so it fits paired
    // alongside metronomeTaalCombo in the compact Tanpura/Taal/BPM row.
    juce::TextButton metronomeStartStopButton;
    BeatIndicatorComponent beatIndicator { metronomeSource };
    PitchGraphComponent pitchGraph;
    // Toggled by sessionHistoryButton, mutually exclusive with pitchGraph -
    // the two share the same bounds (see resized()) and only one is ever
    // visible at a time. Always available (not gated behind Alankar mode):
    // history is worth checking even from Free practice mode.
    juce::TextButton sessionHistoryButton;
    AnalyticsComponent analyticsView;

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    bool metronomeRunning = false;

    juce::ComboBox modeCombo;
    juce::ComboBox alankarPatternCombo;
    juce::TextButton alankarStartButton;
    // Visible only while a practice run is live (see alankarStartButton's
    // onClick and cancelAlankarPractice()) - toggles between pausing the
    // pacing click (freezing the current step in place, no beat advancement,
    // no pitch readings scored) and resuming it from exactly where it left
    // off. Implemented as a metronome disable/re-enable rather than a
    // separate mechanism - see its onClick in the .cpp for why that's safe.
    juce::TextButton alankarPauseButton;
    // Checked at any time, including before a run starts - read only when a
    // run finishes (see timerCallback()'s finish branch): if set, the same
    // pattern restarts immediately instead of stopping.
    juce::ToggleButton alankarLoopToggle;
    bool alankarPaused = false;
    // Shows the selected pattern's swar sequence with the current (or, before
    // a run starts, the 0th) step highlighted - visible only in Alankar mode,
    // alongside alankarPatternCombo/alankarStartButton/alankarResultsLabel.
    SwarChipRowComponent swarChipRow;
    juce::Label alankarResultsLabel;
    std::unique_ptr<AlankarPracticeEngine> alankarEngine;
    // Captured when a practice run starts (see alankarStartButton.onClick),
    // so the session record built on completion reflects what was actually
    // practiced even if the user changes the BPM slider or pattern combo
    // mid-run (both of which are locked while practice is live, but this
    // avoids the two ever being able to drift apart regardless).
    int alankarSessionStartingBpm = 0;
    juce::String alankarSessionPatternName;
    int lastSeenMetronomeBeats = 0; // matches MetronomeAudioSource::getTotalBeatsElapsed()'s int return type
    // Enabling the metronome from disabled arms a reset that fires triggerBeat(0)
    // almost immediately on the next audio block - meaning "step 0 (Sam) begins
    // now", not "one beat has elapsed". Set true right when practice starts so
    // timerCallback()'s drain loop can absorb that first increment instead of
    // misreading it as step 0 having finished.
    bool alankarAwaitingFirstBeat = false;
    // Which step index timerCallback() last rebuilt the target band and
    // results label for. The work behind those two (getSummary() copies a
    // vector, builds a std::map, builds a second vector and sorts it;
    // setTargetBand() repaints unconditionally) produces byte-identical output
    // until the step actually changes, so at 30Hz it was pure allocation and
    // repaint churn. -1 is an impossible step index, so the first check after
    // a practice starts always mismatches and renders. Same guard shape as
    // BeatIndicatorComponent's "only repaint when something changed".
    int lastRenderedAlankarStepIndex = -1;
};
