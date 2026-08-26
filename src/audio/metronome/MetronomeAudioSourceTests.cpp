#include "MetronomeAudioSource.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace
{
    // Mirrors MetronomeClickTests.cpp - 44100 alone previously hid two real
    // Tanpura bugs that were purely sample-rate-dependent, so the set
    // deliberately includes a rate above 48000.
    constexpr double kTestSampleRates[] = { 44100.0, 48000.0, 96000.0 };
}

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

        // This is the file's strongest rate-plumbing guard: it asserts exact
        // beat-index transitions two-sidedly, so it pins down WHEN a beat
        // boundary falls, not merely that beats happen. Since
        // MetronomeAudioSource's entire job is sample-rate-derived timing
        // (samplesPerBeat = sampleRate * 60 / bpm) - and sampleRate has an
        // in-class default of 44100.0 - a 44100-only version of this test
        // would still pass if prepareToPlay() ever stopped storing the rate,
        // while the shipped app ran at the wrong tempo and the wrong click
        // pitch on every 48kHz device. Every expected block count below is
        // DERIVED from the rate rather than hardcoded, so the rate has to be
        // genuinely plumbed through for these to hold.
        for (const double sampleRate : kTestSampleRates)
        {
            beginTest (juce::String ("A BPM change takes effect on the NEXT beat boundary, not the one already"
                                     " in progress, at ") + juce::String (sampleRate, 0) + "Hz");
            {
                MetronomeAudioSource source;
                source.prepareToPlay (512, sampleRate);
                source.setTaal (TaalType::Teentaal);
                source.setBpm (80.0f);
                source.setEnabled (true);

                // e.g. at 44100Hz: 33075 samples/beat at 80 BPM, 8820 at 300 BPM.
                const double samplesPerBeatOld = sampleRate * 60.0 / 80.0;
                const double samplesPerBeatNew = sampleRate * 60.0 / 300.0;

                // Blocks needed for the cumulative sample count to reach each
                // boundary. (blocksToCrossOld - 1) blocks is provably still
                // short of it, which is what makes the assertion two-sided.
                const int blocksToCrossOld = (int) std::ceil (samplesPerBeatOld / 512.0);
                const int blocksToCrossNew = (int) std::ceil (samplesPerBeatNew / 512.0);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::AudioSourceChannelInfo info (&buffer, 0, 512);

                source.addNextAudioBlock (info); // resetClock() fires here: beat 0 starts at 80 BPM
                source.setBpm (300.0f);          // speed up mid-beat-0; must NOT shorten the beat already in progress

                // Stop one block SHORT of the 80-BPM beat-0 boundary. If the
                // BPM change had (wrongly) shortened the beat in progress,
                // the index would already have advanced by now.
                for (int block = 0; block < blocksToCrossOld - 2; ++block)
                    source.addNextAudioBlock (info);
                expectEquals (source.getCurrentBeatIndex(), 0);

                // One more block crosses it - beat index becomes 1, and only
                // THEN is the new, faster 300-BPM tempo read for beat 1.
                source.addNextAudioBlock (info);
                expectEquals (source.getCurrentBeatIndex(), 1);

                // Enough blocks to clear beat 1's new, faster duration but
                // nowhere near another 80-BPM beat - only reachable if the
                // BPM change actually took effect.
                for (int block = 0; block < blocksToCrossNew; ++block)
                    source.addNextAudioBlock (info);
                expectEquals (source.getCurrentBeatIndex(), 2);

                source.releaseResources();
            }
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

        beginTest ("addNextAudioBlock() is additive - proven by comparing two identically-configured sources against different buffer prefills");
        {
            // MetronomeClick's output is 100% deterministic given the same trigger
            // sequence - it never reads the buffer. So two freshly-constructed,
            // identically-configured sources produce bit-identical click samples
            // regardless of what the buffer was pre-filled with. Under addSample
            // (correct), bufferA[i] - bufferB[i] == 0.37f for EVERY sample (a
            // constant offset, since both buffers get the same click added on top
            // of their own different pre-fill). Under setSample (a bug), both
            // buffers would end up holding the SAME raw click values regardless of
            // pre-fill, making the difference 0.0f instead of 0.37f - this test
            // fails immediately if that regression is ever introduced.

            MetronomeAudioSource sourceA;
            sourceA.prepareToPlay (512, 44100.0);
            sourceA.setTaal (TaalType::Teentaal);
            sourceA.setEnabled (true);

            MetronomeAudioSource sourceB;
            sourceB.prepareToPlay (512, 44100.0);
            sourceB.setTaal (TaalType::Teentaal);
            sourceB.setEnabled (true);

            juce::AudioBuffer<float> bufferA (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    bufferA.setSample (ch, i, 0.37f);

            juce::AudioBuffer<float> bufferB (2, 512);
            bufferB.clear(); // 0.0f

            juce::AudioSourceChannelInfo infoA (&bufferA, 0, 512);
            juce::AudioSourceChannelInfo infoB (&bufferB, 0, 512);

            sourceA.addNextAudioBlock (infoA);
            sourceB.addNextAudioBlock (infoB);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError (bufferA.getSample (ch, i) - bufferB.getSample (ch, i), 0.37f, 0.0001f);

            sourceA.releaseResources();
            sourceB.releaseResources();
        }

        beginTest ("The current beat's classification actually reaches MetronomeClick - Sam and Khali produce measurably different peak amplitude");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 8820 - fast enough to reach beat 8 (Khali) quickly
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            float samPeak = 0.0f;
            float khaliPeak = 0.0f;
            int lastSeenBeat = -1;

            // 8 beats * 8820 samples/beat = 70560 samples = 138 blocks of 512
            // (138*512=70656) - comfortably reaches beat 8 (Teentaal's Khali).
            for (int block = 0; block < 138; ++block)
            {
                buffer.clear();
                source.addNextAudioBlock (info);

                const int beat = source.getCurrentBeatIndex();
                if (beat != lastSeenBeat)
                {
                    lastSeenBeat = beat;
                    float peak = 0.0f;
                    for (int i = 0; i < 512; ++i)
                        peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));

                    if (beat == 0)
                        samPeak = juce::jmax (samPeak, peak);
                    else if (beat == 8)
                        khaliPeak = juce::jmax (khaliPeak, peak);
                }
            }

            // Thresholds track MetronomeClick's amplitude table, which leaves
            // ~3dB of headroom for the additive tanpura mix (Sam 0.70, Khali
            // 0.32). The point of this test is the RATIO - that Sam and Khali
            // are audibly different - so the absolute floors are deliberately
            // loose; only the 1.5x ratio below is load-bearing.
            expect (samPeak > 0.35f, "Sam (MetronomeClick amplitude 0.70) should be the loudest beat type");
            expect (khaliPeak > 0.05f, "Khali (MetronomeClick amplitude 0.32) should still be audible");
            expect (samPeak > khaliPeak * 1.5f, "Sam should be measurably louder than Khali - proves TaalPattern's classification is actually reaching MetronomeClick, not just always rendering the same beat type");

            source.releaseResources();
        }

        beginTest ("Calling setEnabled(true) again while already enabled does NOT reset the beat index (only a false-to-true transition resets)");
        {
            MetronomeAudioSource source;
            source.prepareToPlay (512, 44100.0);
            source.setTaal (TaalType::Teentaal);
            source.setBpm (300.0f); // samplesPerBeat = 8820
            source.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioSourceChannelInfo info (&buffer, 0, 512);

            for (int block = 0; block < 18; ++block) // moves well past beat 0 (18*512=9216 > 8820)
                source.addNextAudioBlock (info);
            expect (source.getCurrentBeatIndex() != 0);

            const int beatBeforeRedundantEnable = source.getCurrentBeatIndex();
            source.setEnabled (true); // already enabled - must be a no-op, not a reset
            source.addNextAudioBlock (info);

            expectEquals (source.getCurrentBeatIndex(), beatBeforeRedundantEnable);

            source.releaseResources();
        }
    }
};

static MetronomeAudioSourceTests metronomeAudioSourceTestsInstance;
