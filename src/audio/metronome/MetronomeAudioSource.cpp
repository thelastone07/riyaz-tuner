#include "MetronomeAudioSource.h"

void MetronomeAudioSource::setBpm (float newBpm)
{
    bpm = juce::jlimit (20.0f, 300.0f, newBpm);
}

void MetronomeAudioSource::setTaal (TaalType newType)
{
    pendingTaalType = newType;
    taalChangePending = true;
}

void MetronomeAudioSource::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled && ! enabled.load())
        resetPending = true; // Stop-then-Start always restarts at beat 0 (Sam)
    enabled = shouldBeEnabled;
}

int MetronomeAudioSource::getCurrentBeatIndex() const
{
    return currentBeatIndexForUi.load();
}

int MetronomeAudioSource::getTotalBeatsElapsed() const
{
    return totalBeatsElapsedForUi.load();
}

void MetronomeAudioSource::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRateIn)
{
    sampleRate = sampleRateIn;
    resetPending = true; // force an initial clock reset on the audio thread using whatever taal/bpm is pending
}

void MetronomeAudioSource::releaseResources()
{
}

void MetronomeAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    addNextAudioBlock (bufferToFill);
}

void MetronomeAudioSource::triggerBeat (int beatIndex)
{
    currentBeatIndex = beatIndex;
    click.trigger (pattern.classify (beatIndex), sampleRate);
    currentBeatIndexForUi.store (beatIndex);
    totalBeatsElapsedForUi.fetch_add (1, std::memory_order_relaxed);
}

void MetronomeAudioSource::resetClock()
{
    samplesIntoBeat = 0.0;
    samplesPerBeat = sampleRate * 60.0 / (double) bpm.load();
    triggerBeat (0);
}

void MetronomeAudioSource::addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Both flags are unconditionally consumed exactly once, regardless of
    // which (or both) were set - if only one branch's exchange() ran, the
    // other flag could stay pending and fire an extra, unwanted resetClock()
    // on some later call.
    const bool taalChanged = taalChangePending.exchange (false);
    const bool resetRequested = resetPending.exchange (false);

    if (taalChanged)
        pattern = TaalPattern (pendingTaalType.load());

    if (taalChanged || resetRequested)
        resetClock();

    if (! enabled.load())
        return;

    auto* buffer = bufferToFill.buffer;
    const int numChannels = buffer->getNumChannels();

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const float s = click.renderNextSample();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer->addSample (ch, bufferToFill.startSample + i, s);

        samplesIntoBeat += 1.0;
        if (samplesIntoBeat >= samplesPerBeat)
        {
            samplesIntoBeat -= samplesPerBeat; // preserve remainder, avoids long-run tempo drift
            samplesPerBeat = sampleRate * 60.0 / (double) bpm.load(); // re-read bpm fresh at this new beat boundary
            triggerBeat ((currentBeatIndex + 1) % pattern.beatCount());
        }
    }
}
