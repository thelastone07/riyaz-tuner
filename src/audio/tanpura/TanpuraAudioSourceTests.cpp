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
    }
};

static TanpuraAudioSourceTests tanpuraAudioSourceTestsInstance;
