// src/audio/worker/PitchWorker.cpp
#include "PitchWorker.h"
#include <algorithm>

PitchWorker::PitchWorker (PitchPipeline& pipelineIn, Listener& listenerIn, int fifoCapacitySamples)
    : Thread ("PitchWorker"),
      pipeline (pipelineIn), listener (listenerIn),
      fifo (fifoCapacitySamples), fifoBuffer ((size_t) fifoCapacitySamples),
      drainScratch ((size_t) fifoCapacitySamples)
{
    jassert (fifoCapacitySamples > 0);
}

PitchWorker::~PitchWorker()
{
    // Real contract: a PitchWorker MUST be destroyed on the message thread -
    // the same thread that runs handleAsyncUpdate(). All current callers
    // (MainComponent's destructor / releaseResources(), and the unit tests)
    // already satisfy this; the assertion documents and enforces it.
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    stop();
    // stop() joins the worker thread, but a triggerAsyncUpdate() issued just
    // before that join may still be pending in the message queue.
    // cancelPendingUpdate() removes that NOT-YET-DISPATCHED update so it can
    // never fire against a partially-destroyed object.
    //
    // Note what this does NOT do: if the message thread has ALREADY entered
    // handleAsyncUpdate(), cancelPendingUpdate() is a no-op for that
    // in-flight call - JUCE only clears the pending flag / removes a queued
    // message, it cannot unwind a callback that is already running. So this
    // call is not, by itself, protection against tearing down from a
    // non-message thread; the jassert above states the guarantee that
    // actually makes destruction safe (destroy on the message thread, so an
    // in-progress handleAsyncUpdate() cannot be concurrent with this
    // destructor by construction).
    cancelPendingUpdate();
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
        // Check the FIFO before waiting so audio pushed between construction
        // and this thread's first loop iteration (or between the previous
        // drain and threadShouldExit() becoming true) is drained immediately
        // instead of sitting for up to the full 100ms wait() timeout. This
        // doesn't change steady-state behaviour: once drained, the loop still
        // falls through to wait() and blocks until notify() or the timeout.
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

            // Automatic calibration retry. TonicCalibrator::processFrame() is
            // idempotent once its window closes, so a Timeout/Unstable result
            // is returned forever afterwards, and PitchPipeline never leaves
            // the Calibrating phase except via Success - without this, one
            // failed calibration would brick the app permanently (and a failed
            // FIRST calibration is likely, since the window opens the instant
            // the app launches, before the user has sung anything).
            //
            // Restarting here is safe with no extra locking: this is the same
            // thread that just called pipeline.process(), and it is the only
            // thread that ever touches the pipeline. The failed status is
            // still stored/dispatched below so the UI can say "trying
            // again..." - the retry is invisible-but-announced, not silent.
            if (update.phase == PitchPipelinePhase::Calibrating
                && (update.calibrationStatus == CalibrationStatus::Timeout
                    || update.calibrationStatus == CalibrationStatus::Unstable))
            {
                pipeline.restartCalibration();
            }

            {
                const juce::ScopedLock sl (latestUpdateLock);
                latestUpdate = update;
            }
            triggerAsyncUpdate();
        }

        if (! threadShouldExit())
            wait (100.0); // wake on notify(), or periodically as a safety net
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
