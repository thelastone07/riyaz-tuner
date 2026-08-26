#include "MetronomeAudioSource.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

class MetronomeAudioSourceTests : public juce::UnitTest
{
public:
    MetronomeAudioSourceTests() : juce::UnitTest ("MetronomeAudioSource", "Riyaaz") {}

    void runTest() override
    {
        beginTest ("Disabled source contributes nothing - a pre-filled buffer is preserved");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            // setEnabled() NOT called - defaults to disabled

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.37f); // known nonzero value

            juce::AudioSourceChannelInfo info (&buffer, 0, 512);
            source.addNextAudioBlock (info);

            // Proves additive-not-overwrite AND disabled-contributes-nothing
            // in one check: if addNextAudioBlock ever wrote via setSample()
            // instead of addSample(), or ran while disabled, this changes.
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.37f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 511), 0.37f, 0.0001f);

            source.releaseResources();
        }

        beginTest ("Enabling the source adds audible content on top of existing buffer contents");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.37f);

            juce::AudioSourceChannelInfo info (&buffer, 0, 512);
            source.addNextAudioBlock (info); // Sam triggers synchronously inside this first call

            // Sample 0 of a fresh trigger is sin(phase=0)=0 exactly, so it
            // alone can't prove anything was added - search the whole block.
            bool foundDifference = false;
            for (int i = 0; i < 512; ++i)
            {
                if (std::abs (buffer.getSample (0, i) - 0.37f) > 0.001f)
                {
                    foundDifference = true;
                    break;
                }
            }
            expect (foundDifference);

            source.releaseResources();
        }

        beginTest ("Changing taal resets the beat index to 0 at the next block");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 44100*60/300 = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // 18 blocks of 512 = 9216 samples > 8820 - guarantees at least
            // one beat boundary has passed, so the beat index is provably
            // no longer 0.
            for (int block = 0; block < 18; ++block)
                source.addNextAudioBlock (info);
            expect (source.getCurrentBeatIndex() != 0);

            // Switching taal must reset the clock regardless of which beat
            // we were mid-cycle on - critical because Jhaptaal only has 10
            // beats, so a Teentaal beat index above 9 would be out of
            // TaalPattern::classify()'s valid range for Jhaptaal if it were
            // not reset.
            source.setTaal (TaalType::Jhaptaal);
            source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 0);

            source.releaseResources();
        }

        beginTest ("setEnabled(true) resets the beat index to 0 (Stop-then-Start always restarts at Sam)");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            for (int block = 0; block < 18; ++block) // moves well past beat 0, as above
                source.addNextAudioBlock (info);
            expect (source.getCurrentBeatIndex() != 0);

            source.setEnabled (false);
            source.addNextAudioBlock (info); // disabled - contributes nothing, does not advance the clock
            source.setEnabled (true);        // re-enabling must reset to beat 0
            source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 0);

            source.releaseResources();
        }

        beginTest ("A BPM change takes effect on the NEXT beat boundary, not the one already in progress");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (80.0f); // samplesPerBeat = 44100*60/80 = 33075
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            source.addNextAudioBlock (info); // resetClock() fires here: beat 0 starts at 80 BPM (33075 samples/beat)
            source.setBpm (300.0f);          // speed up mid-beat-0; must NOT shorten the beat already in progress

            // 64 more blocks (65 total) = 33280 samples, just past the
            // 80-BPM beat-0 boundary (33075) - beat index must become 1,
            // and only THEN is the new, faster 300-BPM tempo (8820
            // samples/beat) read for beat 1.
            for (int block = 0; block < 64; ++block)
                source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 1);

            // 18 more blocks = 9216 samples, comfortably past 8820 (beat 1's
            // new, faster duration) but nowhere close to another 33075 -
            // only reachable if the BPM change actually took effect.
            for (int block = 0; block < 18; ++block)
                source.addNextAudioBlock (info);
            expectEquals (source.getCurrentBeatIndex(), 2);

            source.releaseResources();
        }

        beginTest ("PlainClick taal keeps the beat index at 0 across many blocks (single-beat cycle)");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::PlainClick);
            source.setBpm (300.0f); // fast, to cross many beat boundaries quickly
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            // samplesPerBeat = 8820 at 300 BPM; 40 blocks = 20480 samples,
            // more than two full beats.
            for (int block = 0; block < 40; ++block)
            {
                source.addNextAudioBlock (info);
                expectEquals (source.getCurrentBeatIndex(), 0); // beatCount()==1, so index % 1 is always 0
            }

            source.releaseResources();
        }

        beginTest ("getTotalBeatsElapsed() keeps incrementing even when the beat index wraps back to 0");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::PlainClick); // beatCount()==1, index never leaves 0
            source.setBpm (300.0f); // samplesPerBeat = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            const int startTotal = source.getTotalBeatsElapsed();

            // 40 blocks = 20480 samples = 2 full beats at 8820 samples/beat
            // (17640) plus change - at least 2 more beats must have fired.
            for (int block = 0; block < 40; ++block)
                source.addNextAudioBlock (info);

            expect (source.getTotalBeatsElapsed() >= startTotal + 2);

            source.releaseResources();
        }
    }
};

static MetronomeAudioSourceTests metronomeAudioSourceTestsInstance;
