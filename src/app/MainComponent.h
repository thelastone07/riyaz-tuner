// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
#include "../audio/tanpura/TanpuraAudioSource.h"
#include "../audio/worker/PitchWorker.h"
#include "../ui/PitchGraphComponent.h"
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

    CrepePitchEngine engine { juce::String ("models/crepe/small.onnx") };
    std::unique_ptr<PitchPipeline> pipeline;   // constructed in prepareToPlay(), once the real sample rate is known
    std::unique_ptr<PitchWorker> worker;       // ditto

    // Owned directly (not a unique_ptr like pipeline/worker): it has no
    // construction-time dependency on the sample rate - prepareToPlay() hands
    // it the rate later, and it is safe to render from before then (it just
    // outputs silence while disabled).
    TanpuraAudioSource tanpuraSource;

    juce::Label statusLabel;
    juce::Slider tanpuraVolumeSlider;
    juce::Label tanpuraVolumeLabel;
    PitchGraphComponent pitchGraph;

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
};
