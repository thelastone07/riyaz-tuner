#pragma once
#include "TanpuraSynth.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

class TanpuraAudioSource : public juce::AudioSource
{
public:
    void setSa (float saHz, TanpuraTuning tuning = TanpuraTuning::PaSaSaSa);
    void setGain (float newGain);
    void setEnabled (bool shouldBeEnabled);

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    TanpuraSynth synth; // only ever touched on the audio thread (getNextAudioBlock and the retune it consumes)

    double sampleRate = 44100.0;

    // UI-thread-writable, audio-thread-readable. retunePending is the only
    // one that needs exchange() semantics - gain/enabled are simple values
    // read fresh every block, no "consume once" requirement.
    std::atomic<float> pendingSaHz { 220.0f };
    std::atomic<TanpuraTuning> pendingTuning { TanpuraTuning::PaSaSaSa };
    std::atomic<bool> retunePending { false };
    std::atomic<float> gain { 0.5f };
    std::atomic<bool> enabled { false };
};
