#pragma once
#include "TaalPattern.h"
#include "MetronomeClick.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

class MetronomeAudioSource : public juce::AudioSource
{
public:
    void setBpm (float newBpm);
    void setTaal (TaalType newType);
    void setEnabled (bool shouldBeEnabled);

    int getCurrentBeatIndex() const;   // for the UI: which light in the row is current
    int getTotalBeatsElapsed() const;  // for the UI: monotonically increasing, detects "a new beat fired" even when beatCount()==1

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;

    // Required by the AudioSource interface, never called by this app (see
    // MainComponent's getNextAudioBlock() for the real render path) -
    // implemented as clearActiveBufferRegion() followed by
    // addNextAudioBlock(bufferToFill), so if anything ever does call it
    // (e.g. a future refactor), it correctly satisfies AudioSource's
    // overwrite convention in terms of the additive primitive below, rather
    // than being a dead or incorrect stub.
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

    // The real render path - ADDS into the buffer, never overwrites. Called
    // after TanpuraAudioSource::getNextAudioBlock() in MainComponent, which
    // overwrites the buffer first.
    void addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill);

private:
    void resetClock();
    void triggerBeat (int beatIndex);

    MetronomeClick click;                          // audio-thread only
    TaalPattern pattern { TaalType::PlainClick };   // rebuilt on the audio thread when a taal change is consumed

    double sampleRate = 44100.0;
    double samplesPerBeat = 0.0;   // recomputed from bpm only at each beat boundary, not continuously mid-beat
    double samplesIntoBeat = 0.0;  // advances by 1.0 per sample; wraps by subtracting samplesPerBeat, not resetting to 0, to avoid long-run tempo drift
    int currentBeatIndex = 0;

    // UI-thread-writable, audio-thread-readable. taalChangePending and
    // resetPending need exchange() (consume-once) semantics; bpm/enabled are
    // simple values read fresh, no "consume once" requirement.
    std::atomic<float> bpm { 80.0f };
    std::atomic<TaalType> pendingTaalType { TaalType::PlainClick };
    std::atomic<bool> taalChangePending { false };
    std::atomic<bool> resetPending { false };
    std::atomic<bool> enabled { false };

    // Audio-thread-writable, UI-thread-readable (single writer / single
    // reader, no tear risk for a plain int).
    std::atomic<int> currentBeatIndexForUi { 0 };
    std::atomic<int> totalBeatsElapsedForUi { 0 };
};
