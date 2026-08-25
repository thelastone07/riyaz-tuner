// src/audio/worker/PitchWorker.h
#pragma once
#include "../pipeline/PitchPipeline.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

class PitchWorker : private juce::Thread, private juce::AsyncUpdater
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pitchWorkerUpdate (const PitchPipelineUpdate& update) = 0;
    };

    PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples = 16384);
    ~PitchWorker() override;

    // Real-time-safe: no heap allocation. Drops the excess silently if the
    // FIFO doesn't have room for all of numSamples. Not literally lock-free,
    // though: notify() calls WaitableEvent::signal(), which briefly takes a
    // std::lock_guard on the same mutex the worker thread's wait() holds
    // while checking its predicate. That's a bounded, very short critical
    // section (a single signal() call) - not the kind of blocking that risks
    // an audio dropout under normal conditions, but not literally zero
    // either. This is the standard JUCE idiom for waking a worker thread and
    // is a deliberate, accepted trade-off for this project, not an
    // oversight.
    void pushAudio (const float* samples, int numSamples);

    void start();
    void stop();

private:
    void run() override;
    void handleAsyncUpdate() override;

    PitchPipeline& pipeline;
    Listener& listener;

    juce::AbstractFifo fifo;
    std::vector<float> fifoBuffer;
    std::vector<float> drainScratch;

    juce::CriticalSection latestUpdateLock;
    PitchPipelineUpdate latestUpdate { PitchPipelinePhase::Calibrating };
};
