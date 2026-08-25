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

    // Real-time-safe: no allocation, no blocking. Drops the excess silently
    // if the FIFO doesn't have room for all of numSamples.
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
