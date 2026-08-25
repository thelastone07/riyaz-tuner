// src/app/MainComponent.h
#pragma once
#include "../audio/pitchengine/CrepePitchEngine.h"
#include "../audio/pipeline/PitchPipeline.h"
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

    juce::Label statusLabel;
    PitchGraphComponent pitchGraph;

    PitchPipelineUpdate lastUpdate { PitchPipelinePhase::Calibrating };
};
