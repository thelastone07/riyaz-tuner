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
    currentBeatIndex = 0;
}

void MetronomeAudioSource::addNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // resetPending must NEVER be consumed while disabled. setEnabled(true)
    // is two separate stores on the message thread - resetPending = true,
    // then enabled = true - and an audio callback can land between them. If
    // this method consumed resetPending before checking enabled (as it once
    // did), that interleaving would swallow the flag while still disabled,
    // and the enable that follows a block later would find needsReset false
    // and never call triggerBeat(0): Start is pressed, Sam is silent, the
    // indicator does not light, and the cycle audibly begins on beat 2.
    // Gating the consume on enabled removes the window entirely rather than
    // narrowing it.
    if (! enabled.load())
    {
        // A taal change still applies while stopped, so the pattern the UI
        // draws stays in step with the one we will play - but it leaves the
        // reset ARMED rather than performing it, since resetClock() is only
        // meaningful for a block we are actually about to render.
        if (taalChangePending.exchange (false))
        {
            pattern = TaalPattern (pendingTaalType.load());
            resetPending = true; // still owed a reset when we next actually run
        }

        return;
    }

    // Both flags are unconditionally consumed exactly once, regardless of
    // which (or both) were set - if only one branch's exchange() ran, the
    // other flag could stay pending and fire an extra, unwanted resetClock()
    // on some later call.
    const bool taalChanged = taalChangePending.exchange (false);
    const bool resetRequested = resetPending.exchange (false);

    if (taalChanged)
        pattern = TaalPattern (pendingTaalType.load());

    if (taalChanged || resetRequested)
    {
        resetClock();
        triggerBeat (0); // fire beat 0's click + UI update NOW - we are past the enabled gate, so this block really is about to render
    }

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
