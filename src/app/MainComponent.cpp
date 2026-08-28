// src/app/MainComponent.cpp
#include "MainComponent.h"

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
    startTimerHz (2);
    // --- END TEMPORARY DIAGNOSTICS ---

    addAndMakeVisible (pitchGraph);

    setSize (600, 500);

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
            pitchGraph.addPoint (update.timestampMs, *update.centsFromSa);
    }
}

// --- TEMPORARY DIAGNOSTICS ---
// Runs on the message thread (juce::Timer callbacks always do), polling the
// worker's atomics independently of pitchWorkerUpdate() - so this keeps
// updating even if the worker has stalled and pitchWorkerUpdate() has
// stopped firing entirely, which is exactly the distinction under
// investigation (audio still flowing in vs. the worker genuinely stuck).
void MainComponent::timerCallback()
{
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

    pitchGraph.setBounds (area);
}
