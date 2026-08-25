#include "TanpuraAudioSource.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

class TanpuraAudioSourceTests : public juce::UnitTest
{
public:
    TanpuraAudioSourceTests() : juce::UnitTest ("TanpuraAudioSource", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Disabled source fills the buffer with silence");
        {
            TanpuraAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setSa (220.0f);
            source.setGain (1.0f);
            // setEnabled() NOT called - defaults to disabled

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.5f); // pre-fill with garbage to prove it gets cleared

            juce::AudioSourceChannelInfo info (&buffer, 0, 512);
            source.getNextAudioBlock (info);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.0f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 511), 0.0f, 0.0001f);

            source.releaseResources();
        }

        beginTest ("Enabled source with nonzero gain produces non-silent stereo output");
        {
            TanpuraAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setSa (220.0f);
            source.setGain (1.0f);
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // Render several blocks - the first pluck may not happen at
            // sample 0 depending on internal state, so give it a few blocks
            // to guarantee at least one pluck has occurred.
            bool foundSound = false;
            for (int block = 0; block < 10 && ! foundSound; ++block)
            {
                source.getNextAudioBlock (info);
                for (int i = 0; i < 512; ++i)
                {
                    if (std::abs (buffer.getSample (0, i)) > 0.01f)
                    {
                        foundSound = true;
                        break;
                    }
                }
            }
            expect (foundSound);

            source.releaseResources();
        }

        beginTest ("setGain(0) produces silence even when enabled");
        {
            TanpuraAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setSa (220.0f);
            source.setGain (0.0f);
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            for (int block = 0; block < 10; ++block)
            {
                source.getNextAudioBlock (info);
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError (buffer.getSample (0, i), 0.0f, 0.0001f);
            }

            source.releaseResources();
        }

        beginTest ("retunePending.exchange(false) consumes flag exactly once, not every block");
        {
            // This test verifies that synth.prepare() is called exactly once after setSa(),
            // not on every subsequent block. If prepare() were called on every block,
            // samplesSinceLastPluck would reset to 0 every block, causing a fresh pluck
            // (high-energy noise burst) at the start of each block. With correct behavior,
            // natural decay should be visible over time since no new plucks occur for
            // ~53 blocks (1.2s * 44100Hz = 52920 samples / 512 samples per block).

            TanpuraAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setSa (220.0f);
            source.setGain (1.0f);
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // Render blocks and track peak amplitudes at various points.
            // Block 1 contains the initial pluck transient (highest energy).
            std::vector<float> blockPeaks;
            for (int block = 0; block < 15; ++block)
            {
                source.getNextAudioBlock (info);
                float blockPeak = 0.0f;
                for (int i = 0; i < 512; ++i)
                    blockPeak = std::max (blockPeak, std::abs (buffer.getSample (0, i)));
                blockPeaks.push_back (blockPeak);
            }

            // If prepare() is called only once (correct): block 1 peak is highest (fresh pluck),
            // then it generally trends downward as the string decays.
            // If prepare() is called every block (bug): most blocks would start with a fresh
            // pluck, so blockPeaks would be roughly similar for blocks 1-14.

            // Check that early peaks are generally higher than late peaks, confirming decay.
            // Compare the average of blocks 2-4 against blocks 10-14 (much later, plenty of decay).
            float earlAvg = (blockPeaks[1] + blockPeaks[2] + blockPeaks[3]) / 3.0f;
            float lateAvg = (blockPeaks[9] + blockPeaks[10] + blockPeaks[11] + blockPeaks[12] + blockPeaks[13]) / 5.0f;

            expect (blockPeaks[0] > 0.01f); // Block 1 has sound
            expect (lateAvg < 0.9f * earlAvg); // Late blocks show measurable decay compared to early blocks

            source.releaseResources();
        }
    }
};

static TanpuraAudioSourceTests tanpuraAudioSourceTestsInstance;
