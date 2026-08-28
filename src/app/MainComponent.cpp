// src/app/MainComponent.cpp
#include "MainComponent.h"
#include <iterator>

namespace
{
    // Shared between the constructor (building the combo's items) and
    // alankarStartButton.onClick (mapping the selected combo item back to a
    // pattern) - kept in one place so the id<->index mapping can't drift
    // between the two call sites.
    constexpr AlankarPatternId kAlankarPatternIds[] = {
        AlankarPatternId::Alankar1, AlankarPatternId::Alankar2, AlankarPatternId::Alankar3,
        AlankarPatternId::Alankar4, AlankarPatternId::Alankar5
    };
}

void MainComponent::setAlankarPracticeControlsLocked (bool locked)
{
    // See the declaration in MainComponent.h for why each of these three is
    // here. Kept as one helper rather than three setEnabled() calls repeated
    // at each of the four call sites (practice start, practice finish,
    // leaving Alankar mode, pipeline re-entering Calibrating), so the locked
    // and unlocked sets can't drift apart.
    metronomeStartStopButton.setEnabled (! locked);
    metronomeTaalCombo.setEnabled (! locked);
    alankarPatternCombo.setEnabled (! locked);
}

void MainComponent::cancelAlankarPractice()
{
    // Only stop the metronome if a practice was actually started
    // (alankarEngine != nullptr) - otherwise this would also stop a metronome
    // the user started manually for free practice, independent of Alankar mode.
    if (alankarEngine != nullptr)
    {
        metronomeSource.setEnabled (false);
        metronomeRunning = false;
        metronomeStartStopButton.setButtonText ("Start metronome");
    }

    alankarEngine.reset();
    lastRenderedAlankarStepIndex = -1;
    pitchGraph.setTargetBand (std::nullopt);
    setAlankarPracticeControlsLocked (false); // no practice can be live once the engine is gone
    alankarResultsLabel.setText ("", juce::dontSendNotification);
}

TaalType MainComponent::taalTypeForComboId (int comboId)
{
    switch (comboId)
    {
        case 1: return TaalType::PlainClick;
        case 2: return TaalType::Teentaal;
        case 3: return TaalType::Jhaptaal;
        case 4: return TaalType::Ektaal;
        default: jassertfalse; return TaalType::PlainClick; // unreachable - the combo box only ever offers ids 1-4
    }
}

MainComponent::MainComponent()
{
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setText ("Starting...", juce::dontSendNotification);

    // Added (and made visible) BEFORE attachToComponent() below: an attached
    // Label mirrors its owner's visibility at attach time and adds itself to
    // the owner's parent, so attaching to a slider that is not yet visible /
    // not yet parented makes the label's own state depend on JUCE's later
    // visibility-change callback to recover.
    addAndMakeVisible (tanpuraVolumeSlider);
    tanpuraVolumeSlider.setRange (0.0, 1.0);
    tanpuraVolumeSlider.setValue (0.5, juce::dontSendNotification);
    tanpuraVolumeSlider.onValueChange = [this]
    {
        tanpuraSource.setGain ((float) tanpuraVolumeSlider.getValue());
    };

    addAndMakeVisible (tanpuraVolumeLabel);
    tanpuraVolumeLabel.setText ("Tanpura", juce::dontSendNotification);
    tanpuraVolumeLabel.attachToComponent (&tanpuraVolumeSlider, true);

    // Push the slider's initial value into the source here, on the message
    // thread, while the audio device is still closed - rather than from
    // prepareToPlay(), which is an audio-device callback and is NOT guaranteed
    // to run on the message thread (the same reason every statusLabel/
    // pitchGraph touch below goes through callAsync). Doing it here also stops
    // the two defaults from having to agree by coincidence. prepareToPlay()
    // never resets the source's gain, so it stays correct across device
    // restarts without being re-sent.
    tanpuraSource.setGain ((float) tanpuraVolumeSlider.getValue());

    addAndMakeVisible (metronomeTaalCombo);
    metronomeTaalCombo.addItem ("Plain click", 1);
    metronomeTaalCombo.addItem ("Teentaal (16)", 2);
    metronomeTaalCombo.addItem ("Jhaptaal (10)", 3);
    metronomeTaalCombo.addItem ("Ektaal (12)", 4);
    metronomeTaalCombo.setSelectedId (1, juce::dontSendNotification);
    metronomeTaalCombo.onChange = [this]
    {
        const auto type = taalTypeForComboId (metronomeTaalCombo.getSelectedId());
        metronomeSource.setTaal (type);
        beatIndicator.setTaal (type);
    };

    addAndMakeVisible (metronomeBpmSlider);
    metronomeBpmSlider.setRange (20.0, 300.0, 1.0);
    metronomeBpmSlider.setValue (80.0, juce::dontSendNotification);
    metronomeBpmSlider.onValueChange = [this]
    {
        metronomeSource.setBpm ((float) metronomeBpmSlider.getValue());
    };

    addAndMakeVisible (metronomeBpmLabel);
    metronomeBpmLabel.setText ("Metronome BPM", juce::dontSendNotification);
    metronomeBpmLabel.attachToComponent (&metronomeBpmSlider, true);

    addAndMakeVisible (metronomeStartStopButton);
    metronomeStartStopButton.setButtonText ("Start metronome");
    metronomeStartStopButton.onClick = [this]
    {
        metronomeRunning = ! metronomeRunning;
        metronomeSource.setEnabled (metronomeRunning);
        metronomeStartStopButton.setButtonText (metronomeRunning ? "Stop metronome" : "Start metronome");
    };

    // Push the initial BPM/taal into the source here, on the message
    // thread, while the audio device is still closed - same reasoning as
    // the tanpura gain push above (prepareToPlay() is an audio-device
    // callback, not guaranteed to run on the message thread). The
    // metronome starts disabled (metronomeRunning defaults to false), so
    // this has no audible effect until Start is pressed.
    metronomeSource.setBpm ((float) metronomeBpmSlider.getValue());
    metronomeSource.setTaal (taalTypeForComboId (metronomeTaalCombo.getSelectedId()));
    beatIndicator.setTaal (taalTypeForComboId (metronomeTaalCombo.getSelectedId()));

    addAndMakeVisible (beatIndicator);

    // --- TEMPORARY DIAGNOSTICS ---
    addAndMakeVisible (diagnosticsLabel);
    diagnosticsLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    diagnosticsLabel.setColour (juce::Label::textColourId, juce::Colours::yellow);
    diagnosticsLabel.setText ("diag: waiting for worker...", juce::dontSendNotification);
    // --- END TEMPORARY DIAGNOSTICS ---

    // NOTE: Alankar practice mode's beat-driven step advancement (see
    // timerCallback()) also depends on this timer running - do not remove
    // this call as part of any future diagnostics cleanup. 30Hz also matches
    // the poll rate BeatIndicatorComponent already uses elsewhere in this
    // codebase, so beat advancement is no coarser than the beat indicator's
    // own visual resolution.
    startTimerHz (30);

    addAndMakeVisible (modeCombo);
    modeCombo.addItem ("Free practice", 1);
    modeCombo.addItem ("Alankar practice", 2);
    modeCombo.setSelectedId (1, juce::dontSendNotification);
    modeCombo.onChange = [this]
    {
        const bool nowAlankarMode = (modeCombo.getSelectedId() == 2);
        alankarPatternCombo.setVisible (nowAlankarMode);
        alankarStartButton.setVisible (nowAlankarMode);
        alankarResultsLabel.setVisible (nowAlankarMode);

        if (! nowAlankarMode)
            cancelAlankarPractice(); // leaving Alankar mode - drop any practice (running or finished) and its pacing click
    };

    addAndMakeVisible (alankarPatternCombo);
    // Built from kAlankarPatternIds + AlankarPattern::name() rather than
    // hardcoded strings, so the naming convention only lives in one place
    // (AlankarPattern) instead of being duplicated here and in onClick below.
    for (size_t i = 0; i < std::size (kAlankarPatternIds); ++i)
        alankarPatternCombo.addItem (AlankarPattern (kAlankarPatternIds[i]).name(), (int) i + 1);
    alankarPatternCombo.setSelectedId (1, juce::dontSendNotification);
    alankarPatternCombo.setVisible (false); // hidden until Alankar mode is selected

    addAndMakeVisible (alankarStartButton);
    alankarStartButton.setButtonText ("Start Alankar Practice");
    alankarStartButton.setVisible (false);
    // Disabled until calibration succeeds (see the justBecameLive branch in
    // pitchWorkerUpdate() below) - starting practice before Sa is known
    // would advance steps with zero pitch data reaching them.
    alankarStartButton.setEnabled (false);
    alankarStartButton.onClick = [this]
    {
        const int selectedIndex = alankarPatternCombo.getSelectedId() - 1; // ids are 1-5, array is 0-4
        jassert (juce::isPositiveAndBelow (selectedIndex, (int) std::size (kAlankarPatternIds)));
        alankarEngine = std::make_unique<AlankarPracticeEngine> (kAlankarPatternIds[(size_t) selectedIndex]);
        lastSeenMetronomeBeats = metronomeSource.getTotalBeatsElapsed();
        // Back to "nothing rendered yet" so timerCallback()'s step-change guard
        // fires on its very first tick of this run and fills in the full
        // "step 1/N" label (a previous run may have left a real index here).
        lastRenderedAlankarStepIndex = -1;
        // Enabling the metronome from disabled (below) arms a reset that fires
        // triggerBeat(0) almost immediately - that means "step 0 begins now",
        // not "a beat elapsed". Absorb that one increment in timerCallback()'s
        // drain loop instead of forwarding it to the engine.
        alankarAwaitingFirstBeat = true;

        metronomeSource.setTaal (TaalType::PlainClick);
        metronomeRunning = true;
        metronomeStartStopButton.setButtonText ("Stop metronome");

        // BeatIndicatorComponent doesn't read taal state from the audio thread
        // itself - it must be told directly, same as metronomeTaalCombo.onChange
        // above does, or it keeps showing whatever taal was previously selected
        // (wrong light count, combo text mismatch) while the source is actually
        // on PlainClick.
        beatIndicator.setTaal (TaalType::PlainClick);
        metronomeTaalCombo.setSelectedId (1, juce::dontSendNotification);

        // setEnabled() last: it's what actually arms the reset described above,
        // so everything it depends on (taal, alankarAwaitingFirstBeat) must
        // already be in place before it fires.
        metronomeSource.setEnabled (true);

        // Practice is now live: lock the controls that would otherwise arm a
        // second, unabsorbed metronome reset (or silently disagree with the
        // running pattern). Unlocked again in timerCallback()'s finish branch,
        // in modeCombo's leaving-Alankar-mode branch, and when the pipeline
        // re-enters Calibrating.
        setAlankarPracticeControlsLocked (true);

        // Show a target band immediately, not just once the first voiced pitch
        // frame arrives (see pitchWorkerUpdate()) or the first timer tick (up
        // to 33ms away) - step 0 is always valid here since the engine was just
        // constructed and can't be finished yet. This one duplicated band set
        // per run is deliberate; it's what makes the band visible from the
        // instant Start is pressed.
        const float target = alankarEngine->currentStepTargetCents();
        pitchGraph.setTargetBand (std::make_pair (target - AlankarPracticeEngine::kInTuneToleranceCents,
                                                   target + AlankarPracticeEngine::kInTuneToleranceCents));

        alankarResultsLabel.setText ("Practicing...", juce::dontSendNotification);
    };

    addAndMakeVisible (alankarResultsLabel);
    alankarResultsLabel.setVisible (false);

    addAndMakeVisible (pitchGraph);

    setSize (600, 560);

    // Mono input (mic) plus STEREO output: the tanpura drone is rendered into
    // the output channels. See getNextAudioBlock() for why this channel count
    // change makes the order of operations there safety-critical.
    setAudioChannels (1, 2);

    // setAudioChannels() opens the device synchronously (and, on success, has
    // already called prepareToPlay() by the time it returns). If no device
    // opened - no microphone, input permission denied, driver unavailable -
    // nothing will ever call getNextAudioBlock(), so the app would otherwise
    // sit on "Calibrating..." (or "Starting...") forever with zero real audio
    // arriving and no explanation. Say so instead.
    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->statusLabel.setText ("No microphone found - check your audio input device and restart the app.",
                                                juce::dontSendNotification);
        });
    }
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    releaseResources(); // idempotent - stops+joins any existing worker and drops the old pipeline first, making this method safe to call more than once

    // Prepared BEFORE the pitch-path setup below, not after it: getNextAudioBlock()
    // now runs the tanpura source on EVERY block regardless of whether the pitch
    // path came up, so the source must always be holding this device's real
    // sample rate - including on the paths where engine.prepare() fails and this
    // function returns early. (It stays silent until calibration enables it, so
    // preparing it early has no audible effect.)
    tanpuraSource.prepareToPlay (samplesPerBlockExpected, sampleRate);

    // Same reasoning as tanpuraSource immediately above: getNextAudioBlock()
    // will call this unconditionally on every path, including the ones
    // where engine.prepare() fails below, so this must always be prepared.
    // It stays silent (disabled) until Start is pressed, so preparing it
    // early has no audible effect.
    metronomeSource.prepareToPlay (samplesPerBlockExpected, sampleRate);

    // engine.prepare() below restarts CrepePitchEngine's internal timestamp
    // counter at 0, so any points still held by the graph carry timestamps
    // from the PREVIOUS session and are strictly larger than everything that
    // follows. Drop them. prepareToPlay() is not guaranteed to run on the
    // message thread (it's an audio-device callback), so touch pitchGraph
    // through the same SafePointer/callAsync route the status updates use.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->pitchGraph.clear();
    });

    auto prepareStatus = engine.prepare (sampleRate);
    if (prepareStatus != PitchEngineStatus::Ok)
    {
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->statusLabel.setText ("Could not load the pitch model (models/crepe/small.onnx) - check it exists relative to the app's working directory.",
                                                juce::dontSendNotification);
        });
        return;
    }

    pipeline = std::make_unique<PitchPipeline> (engine, sampleRate);
    // static_cast to Listener& (rather than passing *this directly) is
    // required here, not stylistic: PitchWorker::Listener is a PRIVATE base
    // of MainComponent, and std::make_unique forwards its arguments through
    // its own template code, which sits outside MainComponent's access
    // scope. An implicit MainComponent&->Listener& conversion attempted
    // there fails to compile ("cannot cast to private base class") even
    // though this call site itself (a MainComponent member function) is
    // otherwise allowed to perform that exact conversion. Casting explicitly
    // here, in MainComponent's own scope, produces an already-Listener&
    // reference before make_unique ever sees it, sidestepping the problem
    // without loosening PitchWorker::Listener back to a public/protected
    // base.
    worker = std::make_unique<PitchWorker> (*pipeline, static_cast<PitchWorker::Listener&> (*this));
    worker->start();

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->statusLabel.setText ("Calibrating - sing a steady, comfortable note for a few seconds...",
                                            juce::dontSendNotification);
    });
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ORDER IS SAFETY-CRITICAL. With 1 input channel and 2 output channels,
    // JUCE's AudioSourcePlayer aliases channel 0 of this buffer onto output
    // channel 0's own memory, PRE-FILLED with a copy of the mic input (per
    // juce_AudioSourcePlayer.cpp's channel-compaction logic: when
    // numInputs <= numOutputs, the input channels are copied directly into the
    // corresponding output channels' memory before this callback runs).
    // Read the mic samples out FIRST - before anything below overwrites this
    // buffer - or the raw mic signal gets echoed straight back out to the
    // speakers, a real feedback risk with a mic and speakers in the same room,
    // not just a correctness bug.
    if (worker != nullptr && bufferToFill.buffer != nullptr && bufferToFill.buffer->getNumChannels() > 0)
    {
        const float* channelData = bufferToFill.buffer->getReadPointer (0, bufferToFill.startSample);
        worker->pushAudio (channelData, bufferToFill.numSamples);
    }

    // Deliberately NOT inside the guard above, and deliberately not an early
    // return when there is no worker: this call is what puts defined content
    // into the output buffer. TanpuraAudioSource::getNextAudioBlock() always
    // either clears the active region (while disabled) or writes every sample
    // of every channel (while enabled), so it also erases the aliased mic copy
    // described above. Bailing out before it - as the old worker-less early
    // return did, back when there were zero output channels - would now leave
    // that mic copy sitting in the output buffer and play it to the speakers.
    tanpuraSource.getNextAudioBlock (bufferToFill);

    // Additive, not overwriting - runs after the tanpura source above,
    // which has already either cleared the buffer (disabled) or written
    // every sample of every channel (enabled). Order matters: this must
    // come after that overwrite, or its contribution would itself be
    // overwritten.
    metronomeSource.addNextAudioBlock (bufferToFill);
}

void MainComponent::releaseResources()
{
    if (worker != nullptr)
        worker->stop();
    worker.reset();
    pipeline.reset();
    tanpuraSource.releaseResources();
    metronomeSource.releaseResources();
}

void MainComponent::pitchWorkerUpdate (const PitchPipelineUpdate& update)
{
    // Called via PitchWorker's AsyncUpdater, so this already runs on the
    // message thread - safe to touch Components directly here.

    // Detect the Calibrating->Live transition BEFORE overwriting lastUpdate.
    // CalibrationStatus::Success is reported on exactly one process() call
    // before the pipeline flips to Live, and PitchWorker's "latest wins"
    // coalescing can swallow that single update entirely - so "Calibrated!"
    // must be driven off the observed phase change, not off seeing Success.
    const bool justBecameLive = (lastUpdate.phase == PitchPipelinePhase::Calibrating
                                 && update.phase == PitchPipelinePhase::Live);
    lastUpdate = update;

    if (update.phase == PitchPipelinePhase::Calibrating)
    {
        juce::String text;
        switch (update.calibrationStatus)
        {
            case CalibrationStatus::InProgress: text = "Calibrating..."; break;
            case CalibrationStatus::Success:    text = "Calibrated!"; break;
            // PitchWorker restarts calibration automatically on these two, so
            // the wording must NOT imply the user has to trigger anything.
            case CalibrationStatus::Timeout:    text = "Didn't hear a steady note - trying again..."; break;
            case CalibrationStatus::Unstable:   text = "Pitch was too unstable - trying again..."; break;
        }
        statusLabel.setText (text, juce::dontSendNotification);

        // Being in Calibrating means Sa is not known (again): a fresh pipeline
        // starts here after a second prepareToPlay() (e.g. an audio device
        // change). setEnabled(true) in the justBecameLive branch below is
        // one-way, so without this the button would stay enabled from the
        // PREVIOUS calibration and pressing it would run a whole pattern with
        // zero pitch readings reaching the engine, reporting "0.0% in tune".
        // This restores the invariant its declaration states: enabled only
        // while a calibrated Sa exists.
        alankarStartButton.setEnabled (false);

        // Any practice in flight was being scored against a Sa that no longer
        // applies, and a finished one's summary is equally stale - drop it,
        // with the same cleanup leaving Alankar mode does (stop the pacing
        // click, clear the band and label, unlock the locked controls).
        // Guarded so this doesn't re-clear/repaint on every calibration update.
        if (alankarEngine != nullptr)
            cancelAlankarPractice();
    }
    else
    {
        // Every Live-phase update must produce SOME text - an unvoiced Live
        // frame used to fall through this function entirely, leaving a stale
        // "Calibrating..." on screen while the app was in fact listening.
        const juce::String saText = "Sa = " + juce::String (update.saHz, 1) + "Hz";

        if (justBecameLive)
        {
            // Fires regardless of whether THIS frame is voiced: it announces
            // the transition, not the current pitch.
            statusLabel.setText ("Calibrated!   " + saText, juce::dontSendNotification);

            // Start the drone on the Sa we just calibrated to. Both setters are
            // atomic stores consumed by the audio thread on its next block, so
            // calling them from the message thread here is fine. setSa() before
            // setEnabled() so the very first audible block is already in tune.
            tanpuraSource.setSa (update.saHz);
            tanpuraSource.setEnabled (true);
            alankarStartButton.setEnabled (true);
        }
        else if (update.swarLabel.has_value() && update.centsFromSa.has_value())
        {
            statusLabel.setText (saText + "   "
                                   + swarToString (update.swarLabel->swar) + " ("
                                   + registerToString (update.swarLabel->octaveRegister) + ")   "
                                   + juce::String (update.swarLabel->centsFromCenter, 1) + "c",
                                 juce::dontSendNotification);
        }
        else
        {
            statusLabel.setText (saText + " - listening...", juce::dontSendNotification);
        }

        // Plot whenever this frame carried a pitch, independently of which
        // text branch above ran (the transition frame can be voiced too).
        if (update.centsFromSa.has_value())
        {
            pitchGraph.addPoint (update.timestampMs, *update.centsFromSa);

            if (alankarEngine != nullptr && ! alankarEngine->isFinished())
            {
                alankarEngine->onPitchReading (*update.centsFromSa);
                const float target = alankarEngine->currentStepTargetCents();
                pitchGraph.setTargetBand (std::make_pair (target - AlankarPracticeEngine::kInTuneToleranceCents,
                                                           target + AlankarPracticeEngine::kInTuneToleranceCents));
            }
        }
    }
}

void MainComponent::timerCallback()
{
    // --- Alankar practice: beat-driven step advancement (PERMANENT - not part
    // of the diagnostics below). This block, and the startTimerHz() call that
    // drives it (see the constructor), must survive any future cleanup of the
    // TEMPORARY DIAGNOSTICS code further down this function.
    if (alankarEngine != nullptr && ! alankarEngine->isFinished())
    {
        const auto currentBeats = metronomeSource.getTotalBeatsElapsed();
        for (auto b = lastSeenMetronomeBeats; b < currentBeats; ++b)
        {
            if (alankarAwaitingFirstBeat)
                alankarAwaitingFirstBeat = false; // the reset's own initial triggerBeat(0) just means step 0 has begun, not that a beat has elapsed - absorb it, don't forward it
            else
                alankarEngine->onBeatElapsed();
        }
        lastSeenMetronomeBeats = currentBeats;

        if (alankarEngine->isFinished())
        {
            metronomeSource.setEnabled (false);
            metronomeRunning = false;
            metronomeStartStopButton.setButtonText ("Start metronome");
            pitchGraph.setTargetBand (std::nullopt);
            setAlankarPracticeControlsLocked (false); // practice is over - the metronome, taal and pattern are the user's again

            const auto summary = alankarEngine->getSummary();
            juce::String text = "Done! Overall: " + juce::String (summary.overallTimeInTunePercent, 1) + "% in tune.";
            if (! summary.perSwarTimeInTunePercent.empty())
            {
                text += "  Weakest: " + swarToString (summary.perSwarTimeInTunePercent.front().first)
                        + " (" + juce::String (summary.perSwarTimeInTunePercent.front().second, 1) + "%)";
            }
            alankarResultsLabel.setText (text, juce::dontSendNotification);
        }
        // Only when the step actually changed: this callback runs at 30Hz but
        // both the band and the label are functions of the current step alone,
        // so re-deriving them every tick re-does a vector copy, a std::map
        // build, a second vector, a sort and a repaint to produce exactly what
        // is already on screen. lastRenderedAlankarStepIndex starts at -1 (and
        // is reset to -1 when a practice starts), so step 0 always renders.
        else if (alankarEngine->currentStepIndex() != lastRenderedAlankarStepIndex)
        {
            lastRenderedAlankarStepIndex = alankarEngine->currentStepIndex();

            // Refresh the target band on every beat-driven step change, not
            // just on voiced pitch frames (see pitchWorkerUpdate()) - otherwise
            // the band would freeze on a stale step whenever the user pauses
            // singing while the engine keeps advancing via the beat clock.
            const float target = alankarEngine->currentStepTargetCents();
            pitchGraph.setTargetBand (std::make_pair (target - AlankarPracticeEngine::kInTuneToleranceCents,
                                                       target + AlankarPracticeEngine::kInTuneToleranceCents));

            const auto liveSummary = alankarEngine->getSummary();
            alankarResultsLabel.setText (
                "Practicing... step " + juce::String (alankarEngine->currentStepIndex() + 1) + "/"
                    + juce::String (alankarEngine->totalSteps())
                    + "   (" + juce::String (liveSummary.overallTimeInTunePercent, 1) + "% in tune so far)",
                juce::dontSendNotification);
        }
    }

    // --- TEMPORARY DIAGNOSTICS ---
    // Runs on the message thread (juce::Timer callbacks always do), polling the
    // worker's atomics independently of pitchWorkerUpdate() - so this keeps
    // updating even if the worker has stalled and pitchWorkerUpdate() has
    // stopped firing entirely, which is exactly the distinction under
    // investigation (audio still flowing in vs. the worker genuinely stuck).
    if (worker == nullptr)
    {
        diagnosticsLabel.setText ("diag: no worker (not calibrating yet / device not ready)", juce::dontSendNotification);
        return;
    }

    const auto pushed = worker->getTotalSamplesPushed();
    const auto dropped = worker->getTotalSamplesDropped();
    const auto drains = worker->getTotalDrainsProcessed();
    const auto lastMs = worker->getLastDrainDurationMs();
    const auto lastSamples = worker->getLastDrainSampleCount();
    const auto peak = worker->getLastPushedPeakAbs();
    const double dropPct = pushed > 0 ? (100.0 * (double) dropped / (double) pushed) : 0.0;

    diagnosticsLabel.setText (
        "diag: pushed=" + juce::String (pushed)
            + " dropped=" + juce::String (dropped) + " (" + juce::String (dropPct, 2) + "%)"
            + "  drains=" + juce::String (drains)
            + "  last=" + juce::String (lastMs, 1) + "ms/" + juce::String (lastSamples) + "smp"
            + "  MIC PEAK=" + juce::String (peak, 4),
        juce::dontSendNotification);
}
// --- END TEMPORARY DIAGNOSTICS ---

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    statusLabel.setBounds (area.removeFromTop (60));

    // Only the slider is positioned: tanpuraVolumeLabel is attached to it, so
    // JUCE re-places the label itself whenever the slider moves. The left inset
    // is what the attached label is drawn into - an attached label's width is
    // clamped to its owner's x, so a slider at x == 0 would leave no room and
    // the label would collapse to nothing.
    tanpuraVolumeSlider.setBounds (area.removeFromTop (30).withTrimmedLeft (80).withTrimmedRight (10));

    auto metronomeControlsRow = area.removeFromTop (30);
    metronomeTaalCombo.setBounds (metronomeControlsRow.removeFromLeft (150).reduced (2));
    metronomeStartStopButton.setBounds (metronomeControlsRow.removeFromRight (150).reduced (2));

    // Same attached-label reasoning as tanpuraVolumeSlider above, with a
    // wider left inset since "Metronome BPM" is a longer label than "Tanpura".
    metronomeBpmSlider.setBounds (area.removeFromTop (30).withTrimmedLeft (140).withTrimmedRight (10));

    beatIndicator.setBounds (area.removeFromTop (50));

    diagnosticsLabel.setBounds (area.removeFromTop (18)); // TEMPORARY DIAGNOSTICS

    auto alankarControlsRow = area.removeFromTop (30);
    modeCombo.setBounds (alankarControlsRow.removeFromLeft (150).reduced (2));
    alankarStartButton.setBounds (alankarControlsRow.removeFromRight (150).reduced (2));
    alankarPatternCombo.setBounds (alankarControlsRow.reduced (2));

    alankarResultsLabel.setBounds (area.removeFromTop (18));

    pitchGraph.setBounds (area);
}
