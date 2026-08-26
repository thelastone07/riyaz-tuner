// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/tanpura/TanpuraAudioSource.h"
#include "../audio/metronome/MetronomeAudioSource.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
#include "../ui/BeatIndicatorComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

class MainComponent : public juce::AudioAppComponent, private PitchWorker::Listener
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
    void pitchWorkerUpdate (const PitchPipelineUpdate& update) override;
    static TaalType taalTypeForComboId (int comboId);

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

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
    bool metronomeRunning = false;
};
