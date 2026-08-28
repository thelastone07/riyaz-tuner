// src/audio/worker/PitchWorker.h
#pragma once
#include "../pipeline/PitchPipeline.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <vector>

// Owns the single worker thread that pulls microphone audio off a lock-free
// FIFO and drives PitchPipeline::process().
//
// Timing invariant (important, and easy to get wrong): run() coalesces ALL
// currently-ready FIFO samples into ONE pipeline.process() call. That block is
// therefore NOT bounded by the audio callback's block size - if the worker
// thread falls behind real time, a single process() call can receive many
// callbacks' worth of audio at once (up to the whole FIFO capacity).
//
// So the old rule of thumb "TonicCalibrator needs multiple process() calls, so
// keep blocks under ~300ms" is NOT what actually holds any more. The real
// invariant this design depends on is simply: **the worker thread keeps up
// with real time**. As long as it drains at roughly the rate audio arrives,
// each drain is about one callback long, the calibrator still sees many
// process() calls across its window, and everything downstream behaves. If the
// worker ever stalls (a slow ONNX inference run, CPU contention, a debugger
// break), a subsequent drain can hand the calibrator one giant block, which
// collapses many expected engine frames into one and can skew calibration.
// See TODOS.md ("From RealtimeApp final review") for the known limitation and
// possible fixes (capping per-drain size / reading fixed CREPE-window chunks).
//
// Threading: pushAudio() is called from the audio thread; run() is the worker
// thread; handleAsyncUpdate() (and therefore Listener::pitchWorkerUpdate) runs
// on the message thread. A PitchWorker must be constructed and destroyed on
// the message thread - see ~PitchWorker().
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

    // --- TEMPORARY DIAGNOSTICS (2026-08-28) ---
    // Investigating a reported regression: "the graph doesn't react to voice
    // immediately, and after a while it's stuck" since Tanpura/Metronome
    // added real per-sample DSP work to the audio thread. PitchWorker.h's own
    // header comment (above) documents a known, previously-untriggered risk:
    // if this worker thread ever falls behind real time, a single
    // pipeline.process() call can coalesce many callbacks' worth of audio,
    // and the FIFO can start dropping fresh audio. These counters make that
    // directly observable instead of theoretical. Remove once root cause is
    // confirmed.
    juce::int64 getTotalSamplesPushed() const noexcept    { return totalSamplesPushed.load(); }
    juce::int64 getTotalSamplesDropped() const noexcept   { return totalSamplesDropped.load(); }
    juce::int64 getTotalDrainsProcessed() const noexcept  { return totalDrainsProcessed.load(); }
    double getLastDrainDurationMs() const noexcept        { return lastDrainDurationMs.load(); }
    int getLastDrainSampleCount() const noexcept          { return lastDrainSampleCount.load(); }
    float getLastPushedPeakAbs() const noexcept           { return lastPushedPeakAbs.load(); } // peak |sample| of the most recent pushAudio() call - proves whether real signal (not silence) is reaching the FIFO at all
    // --- END TEMPORARY DIAGNOSTICS ---

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

    // --- TEMPORARY DIAGNOSTICS (2026-08-28) --- see public getters above.
    std::atomic<juce::int64> totalSamplesPushed { 0 };
    std::atomic<juce::int64> totalSamplesDropped { 0 };
    std::atomic<juce::int64> totalDrainsProcessed { 0 };
    std::atomic<double> lastDrainDurationMs { 0.0 };
    std::atomic<int> lastDrainSampleCount { 0 };
    std::atomic<float> lastPushedPeakAbs { 0.0f };
    // --- END TEMPORARY DIAGNOSTICS ---
};
