// src/app/MainComponent.cpp
#include "MainComponent.h"
#include "../ui/RiyaazLookAndFeel.h"
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
        updateMetronomeStartStopButtonText();
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

void MainComponent::updateMetronomeStartStopButtonText()
{
    // Icon-only square button (see its declaration in MainComponent.h) -
    // "stop" square when running, "play" triangle when not. Built via
    // CharPointer_UTF8 rather than a plain string literal so the glyph is
    // interpreted correctly regardless of the source file's own encoding.
    metronomeStartStopButton.setButtonText (metronomeRunning
        ? juce::String (juce::CharPointer_UTF8 ("\xE2\x96\xA0"))  // "■"
        : juce::String (juce::CharPointer_UTF8 ("\xE2\x96\xB6"))); // "▶"
}

void MainComponent::refreshSwarChipRowFromSelectedPattern()
{
    const int selectedIndex = alankarPatternCombo.getSelectedId() - 1; // ids are 1-5, array is 0-4
    if (! juce::isPositiveAndBelow (selectedIndex, (int) std::size (kAlankarPatternIds)))
        return; // defensive only - the combo is always populated with a valid selection before this can fire

    // pattern must outlive the reference below - fullSequence() returns a
    // reference into pattern's own internal vector, and a temporary
    // (AlankarPattern (...).fullSequence()) would be destroyed at the end of
    // this statement, leaving that reference dangling for the loop that
    // reads it.
    const AlankarPattern pattern (kAlankarPatternIds[(size_t) selectedIndex]);
    const auto& sequence = pattern.fullSequence();
    std::vector<Swar> swars;
    swars.reserve (sequence.size());
    for (const auto& step : sequence)
        swars.push_back (step.swar);
    swarChipRow.setSequence (std::move (swars));
}

MainComponent::MainComponent (const juce::String& profileNameIn, std::optional<float> knownSaHzIn)
    : activeProfileName (profileNameIn), pendingKnownSaHz (knownSaHzIn)
{
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setFont (RiyaazLookAndFeel::bodyFont());
    statusLabel.setColour (juce::Label::textColourId, RiyaazColours::primaryText);
    statusLabel.setText ("Starting...", juce::dontSendNotification);

    // The compact Tanpura / Taal / Metronome BPM row (see resized()) gives
    // each control its own caption ABOVE it, not to its left as this app's
    // very first layout did - so none of these three labels use
    // Label::attachToComponent() any more; resized() positions each one
    // explicitly, in lockstep with its control.
    addAndMakeVisible (tanpuraVolumeSlider);
    tanpuraVolumeSlider.setRange (0.0, 1.0);
    tanpuraVolumeSlider.setValue (0.5, juce::dontSendNotification);
    tanpuraVolumeSlider.onValueChange = [this]
    {
        tanpuraSource.setGain ((float) tanpuraVolumeSlider.getValue());
    };

    addAndMakeVisible (tanpuraVolumeLabel);
    tanpuraVolumeLabel.setText ("TANPURA", juce::dontSendNotification);

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

    addAndMakeVisible (metronomeTaalLabel);
    metronomeTaalLabel.setText ("TAAL", juce::dontSendNotification);

    addAndMakeVisible (metronomeBpmSlider);
    metronomeBpmSlider.setRange (20.0, 300.0, 1.0);
    metronomeBpmSlider.setValue (80.0, juce::dontSendNotification);
    metronomeBpmSlider.onValueChange = [this]
    {
        metronomeSource.setBpm ((float) metronomeBpmSlider.getValue());
    };

    addAndMakeVisible (metronomeBpmLabel);
    metronomeBpmLabel.setText ("METRONOME BPM", juce::dontSendNotification);

    addAndMakeVisible (metronomeStartStopButton);
    updateMetronomeStartStopButtonText();
    metronomeStartStopButton.onClick = [this]
    {
        metronomeRunning = ! metronomeRunning;
        metronomeSource.setEnabled (metronomeRunning);
        updateMetronomeStartStopButtonText();
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
        swarChipRow.setVisible (nowAlankarMode);
        alankarResultsLabel.setVisible (nowAlankarMode);

        if (nowAlankarMode)
            refreshSwarChipRowFromSelectedPattern(); // show the selected pattern (step 0) even before Start is pressed

        if (! nowAlankarMode)
            cancelAlankarPractice(); // leaving Alankar mode - drop any practice (running or finished) and its pacing click

        // resized() reserves layout space for the pattern/swar-chip rows
        // only in Alankar mode (see there) - a visibility change alone
        // doesn't re-run layout, so force it explicitly.
        resized();
    };

    addAndMakeVisible (alankarPatternCombo);
    // Built from kAlankarPatternIds + AlankarPattern::name() rather than
    // hardcoded strings, so the naming convention only lives in one place
    // (AlankarPattern) instead of being duplicated here and in onClick below.
    for (size_t i = 0; i < std::size (kAlankarPatternIds); ++i)
        alankarPatternCombo.addItem (AlankarPattern (kAlankarPatternIds[i]).name(), (int) i + 1);
    alankarPatternCombo.setSelectedId (1, juce::dontSendNotification);
    alankarPatternCombo.setVisible (false); // hidden until Alankar mode is selected
    alankarPatternCombo.onChange = [this] { refreshSwarChipRowFromSelectedPattern(); };

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
        alankarSessionStartingBpm = (int) metronomeBpmSlider.getValue();
        alankarSessionPatternName = AlankarPattern (kAlankarPatternIds[(size_t) selectedIndex]).name();
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
        updateMetronomeStartStopButtonText();

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

        // A fresh run always starts at step 0 - refreshSwarChipRowFromSelectedPattern()
        // rebuilds the sequence and resets the highlighted index to 0 together.
        refreshSwarChipRowFromSelectedPattern();

        alankarResultsLabel.setText ("Practicing...", juce::dontSendNotification);
    };

    addAndMakeVisible (swarChipRow);
    swarChipRow.setVisible (false); // hidden until Alankar mode is selected, same as alankarPatternCombo

    addAndMakeVisible (alankarResultsLabel);
    alankarResultsLabel.setFont (RiyaazLookAndFeel::smallMetaFont());
    alankarResultsLabel.setColour (juce::Label::textColourId, RiyaazColours::primaryText);
    alankarResultsLabel.setVisible (false);

    addAndMakeVisible (sessionHistoryButton);
    sessionHistoryButton.setButtonText ("Session History");
    sessionHistoryButton.onClick = [this]
    {
        const bool showingHistory = analyticsView.isVisible();
        if (showingHistory)
        {
            analyticsView.setVisible (false);
            pitchGraph.setVisible (true);
            sessionHistoryButton.setButtonText ("Session History");
            sessionHistoryButton.setToggleState (false, juce::dontSendNotification);
        }
        else
        {
            // Freshly loaded every time the view opens (SessionStore does no
            // in-memory caching - same pattern as ProfileStore), so a run
            // completed since the last time this was open is reflected.
            analyticsView.setSessions (sessionStore.loadAllForProfile (activeProfileName));
            analyticsView.setVisible (true);
            pitchGraph.setVisible (false);
            sessionHistoryButton.setButtonText ("Back to practice");
            sessionHistoryButton.setToggleState (true, juce::dontSendNotification);
        }
    };

    addAndMakeVisible (pitchGraph);
    addChildComponent (analyticsView); // hidden until sessionHistoryButton is pressed - addChildComponent does not show it

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
    // Reusing a saved profile's Sa skips calibration entirely - the very
    // first process() call (once the worker starts below) already reports
    // Live phase, at this Sa, with zero calibration frames consumed.
    if (pendingKnownSaHz.has_value())
        pipeline->startLiveWithKnownSa (*pendingKnownSaHz);

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
            alankarStartButton.setEnabled (true);

            if (pendingKnownSaHz.has_value())
            {
                // Reusing a saved Sa - deliberately quieter than a fresh
                // calibration: no tanpura auto-start, and nothing new to
                // save since the Sa didn't change.
            }
            else
            {
                // Start the drone on the Sa we just calibrated to. Both setters are
                // atomic stores consumed by the audio thread on its next block, so
                // calling them from the message thread here is fine. setSa() before
                // setEnabled() so the very first audible block is already in tune.
                tanpuraSource.setSa (update.saHz);
                tanpuraSource.setEnabled (true);

                // A real calibration just succeeded (new profile, or an
                // existing one's "Recalibrate") - persist the fresh Sa for
                // next time.
                profileStore.save ({ activeProfileName, update.saHz });
            }
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
                alankarEngine->onPitchReading (*update.centsFromSa, update.timestampMs);
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
                alankarEngine->onBeatElapsed (lastUpdate.timestampMs);
        }
        lastSeenMetronomeBeats = currentBeats;

        if (alankarEngine->isFinished())
        {
            metronomeSource.setEnabled (false);
            metronomeRunning = false;
            updateMetronomeStartStopButtonText();
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

            // Record this completed run. Only reached when the engine finishes
            // on its own (all steps beat-advanced through) - cancelAlankarPractice()
            // (leaving Alankar mode, or the pipeline re-entering Calibrating
            // mid-run) never calls into sessionStore, so a cancelled/interrupted
            // run records nothing, and this is the only call to
            // sessionStore.append() anywhere in the codebase.
            AlankarSessionRecord record;
            record.profileName = activeProfileName;
            record.patternName = alankarSessionPatternName;
            record.startingBpm = alankarSessionStartingBpm;
            record.completedAtEpochMs = juce::Time::getCurrentTime().toMilliseconds();
            record.overallTimeInTunePercent = summary.overallTimeInTunePercent;
            for (const auto& entry : summary.perSwarTimeInTunePercent)
                record.perSwarTimeInTunePercent.push_back ({ swarToString (entry.first), entry.second });
            sessionStore.append (record);
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

            swarChipRow.setCurrentIndex (alankarEngine->currentStepIndex());

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
    statusLabel.setBounds (area.removeFromTop (44));

    // Compact 3-column row: Tanpura | Taal (+ start/stop) | Metronome BPM,
    // each with its own caption above it - replaces the original layout's
    // three separate 30px-tall rows (one control per row, caption to its
    // left) with one ~50px row, freeing meaningfully more vertical space
    // for the graph/analytics panel below.
    auto controlsRow = area.removeFromTop (52);
    auto tanpuraColumn = controlsRow.removeFromLeft (controlsRow.getWidth() / 3).reduced (4, 0);
    auto taalColumn = controlsRow.removeFromLeft ((controlsRow.getWidth() * 4) / 7).reduced (4, 0);
    auto bpmColumn = controlsRow.reduced (4, 0);

    tanpuraVolumeLabel.setBounds (tanpuraColumn.removeFromTop (18));
    tanpuraColumn.removeFromTop (2);
    tanpuraVolumeSlider.setBounds (tanpuraColumn.removeFromTop (28));

    metronomeTaalLabel.setBounds (taalColumn.removeFromTop (18));
    taalColumn.removeFromTop (2);
    auto taalControlsRow = taalColumn.removeFromTop (28);
    metronomeStartStopButton.setBounds (taalControlsRow.removeFromRight (28));
    taalControlsRow.removeFromRight (8);
    metronomeTaalCombo.setBounds (taalControlsRow);

    metronomeBpmLabel.setBounds (bpmColumn.removeFromTop (18));
    bpmColumn.removeFromTop (2);
    metronomeBpmSlider.setBounds (bpmColumn.removeFromTop (28));

    beatIndicator.setBounds (area.removeFromTop (50));

    diagnosticsLabel.setBounds (area.removeFromTop (18)); // TEMPORARY DIAGNOSTICS

    auto modeRow = area.removeFromTop (32);
    modeCombo.setBounds (modeRow.removeFromLeft (190).reduced (2));
    sessionHistoryButton.setBounds (modeRow.removeFromRight (150).reduced (2));

    // Pattern combo, "Start Alankar Practice" and the swar-chip row only
    // take up layout space in Alankar mode - modeCombo.onChange calls
    // resized() explicitly whenever the mode changes (a visibility change
    // alone doesn't retrigger layout), so this stays in sync.
    if (modeCombo.getSelectedId() == 2)
    {
        area.removeFromTop (8);
        auto patternRow = area.removeFromTop (32);
        alankarStartButton.setBounds (patternRow.removeFromRight (150).reduced (2));
        patternRow.removeFromRight (8);
        alankarPatternCombo.setBounds (patternRow.reduced (2));

        area.removeFromTop (8);
        swarChipRow.setBounds (area.removeFromTop (30));
    }

    area.removeFromTop (6);
    alankarResultsLabel.setBounds (area.removeFromTop (18));

    // Mutually exclusive, same bounds - see sessionHistoryButton.onClick above.
    pitchGraph.setBounds (area);
    analyticsView.setBounds (area);
}
