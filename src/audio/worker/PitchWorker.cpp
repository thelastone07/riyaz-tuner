// src/audio/worker/PitchWorker.cpp
#include "PitchWorker.h"
#include <algorithm>

PitchWorker::PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples)
    : Thread ("PitchWorker"),
      pipeline (pipelineIn), listener (listenerIn),
      fifo (fifoCapacitySamples), fifoBuffer ((size_t) fifoCapacitySamples),
      drainScratch ((size_t) fifoCapacitySamples)
{
}

PitchWorker::~PitchWorker()
{
    stop();
}

void PitchWorker::pushAudio (const float* samples, int numSamples)
{
    auto writeHandle = fifo.write (numSamples);
    int written = 0;

    if (writeHandle.blockSize1 > 0)
    {
        std::copy (samples, samples + writeHandle.blockSize1,
                   fifoBuffer.begin() + writeHandle.startIndex1);
        written += writeHandle.blockSize1;
    }
    if (writeHandle.blockSize2 > 0)
    {
        std::copy (samples + written, samples + written + writeHandle.blockSize2,
                   fifoBuffer.begin() + writeHandle.startIndex2);
    }
    // If blockSize1 + blockSize2 < numSamples, the FIFO was full and the
    // remainder is dropped - by design, never blocks or grows here.

    notify();
}

void PitchWorker::start()
{
    startThread (Priority::normal);
}

void PitchWorker::stop()
{
    stopThread (1000);
}

void PitchWorker::run()
{
    while (! threadShouldExit())
    {
        wait (100.0); // wake on notify(), or periodically as a safety net

        while (fifo.getNumReady() > 0 && ! threadShouldExit())
        {
            auto readHandle = fifo.read (fifo.getNumReady());
            int n = 0;

            if (readHandle.blockSize1 > 0)
            {
                std::copy (fifoBuffer.begin() + readHandle.startIndex1,
                           fifoBuffer.begin() + readHandle.startIndex1 + readHandle.blockSize1,
                           drainScratch.begin());
                n += readHandle.blockSize1;
            }
            if (readHandle.blockSize2 > 0)
            {
                std::copy (fifoBuffer.begin() + readHandle.startIndex2,
                           fifoBuffer.begin() + readHandle.startIndex2 + readHandle.blockSize2,
                           drainScratch.begin() + n);
                n += readHandle.blockSize2;
            }

            auto update = pipeline.process (drainScratch.data(), (size_t) n);

            {
                const juce::ScopedLock sl (latestUpdateLock);
                latestUpdate = update;
            }
            triggerAsyncUpdate();
        }
    }
}

void PitchWorker::handleAsyncUpdate()
{
    PitchPipelineUpdate update { PitchPipelinePhase::Calibrating };
    {
        const juce::ScopedLock sl (latestUpdateLock);
        update = latestUpdate;
    }
    listener.pitchWorkerUpdate (update);
}
