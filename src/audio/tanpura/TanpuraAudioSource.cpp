#include "TanpuraAudioSource.h"

void TanpuraAudioSource::setSa (float saHz, TanpuraTuning tuning)
{
    pendingSaHz = saHz;
    pendingTuning = tuning;
    retunePending = true;
}

void TanpuraAudioSource::setGain (float newGain)
{
    gain = juce::jlimit (0.0f, 1.0f, newGain);
}

void TanpuraAudioSource::setEnabled (bool shouldBeEnabled)
{
    enabled = shouldBeEnabled;
}

void TanpuraAudioSource::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRateIn)
{
    sampleRate = sampleRateIn;
    retunePending = true; // force an initial prepare() on the audio thread using whatever Sa is pending
}

void TanpuraAudioSource::releaseResources()
{
}

void TanpuraAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (retunePending.exchange (false))
        synth.prepare (sampleRate, pendingSaHz.load(), pendingTuning.load());

    if (! enabled.load())
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const float currentGain = gain.load();
    auto* buffer = bufferToFill.buffer;
    const int numChannels = buffer->getNumChannels();

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const float sample = synth.renderNextSample() * currentGain;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer->setSample (ch, bufferToFill.startSample + i, sample);
    }
}
