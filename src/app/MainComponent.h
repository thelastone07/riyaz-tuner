// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/tanpura/TanpuraAudioSource.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
#include "../ui/BeatIndicatorComponent.h"
#include "../practice/AlankarPattern.h"
#include "../practice/AlankarPracticeEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

// --- TEMPORARY DIAGNOSTICS (2026-08-28) --- see PitchWorker.h. Remove once
// root cause of the "graph doesn't react / gets stuck" regression is
// confirmed.
class MainComponent : public juce::AudioAppComponent, private PitchWorker::Listener, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override; // TEMPORARY DIAGNOSTICS
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

    CrepePitchEngine engine { juce::String ("models/crepe/small.onnx") };
    std::unique_ptr<PitchPipeline> pipeline;   // constructed in prepareToPlay(), once the real sample rate is known
    std::unique_ptr<PitchWorker> worker;       // ditto

    // Owned directly (not a unique_ptr like pipeline/worker): neither has a
    // construction-time dependency on the sample rate - prepareToPlay()
    // hands them the rate later, and both are safe to render from before
    // then (tanpuraSource outputs silence while disabled; metronomeSource
    // does too, since it also defaults to disabled).
    TanpuraAudioSource tanpuraSource;
    MetronomeAudioSource metronomeSource;

    juce::Label statusLabel;
    juce::Slider tanpuraVolumeSlider;
    juce::Label tanpuraVolumeLabel;
    juce::ComboBox metronomeTaalCombo;
    juce::Slider metronomeBpmSlider;
    juce::Label metronomeBpmLabel;
    juce::TextButton metronomeStartStopButton;
    BeatIndicatorComponent beatIndicator { metronomeSource };
    PitchGraphComponent pitchGraph;
    juce::Label diagnosticsLabel; // TEMPORARY DIAGNOSTICS

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    bool metronomeRunning = false;

    juce::ComboBox modeCombo;
    juce::ComboBox alankarPatternCombo;
    juce::TextButton alankarStartButton;
    juce::Label alankarResultsLabel;
    std::unique_ptr<AlankarPracticeEngine> alankarEngine;
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
