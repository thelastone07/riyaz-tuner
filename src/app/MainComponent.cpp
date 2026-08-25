// src/app/MainComponent.cpp
#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setText ("Starting...", juce::dontSendNotification);

    addAndMakeVisible (pitchGraph);

    setSize (600, 400);

    // Mono input (mic), no audio output needed - the app only analyzes,
    // it doesn't play anything back yet (tanpura/metronome are later work).
    setAudioChannels (1, 0);

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

void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    releaseResources(); // idempotent - stops+joins any existing worker and drops the old pipeline first, making this method safe to call more than once

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
    if (worker == nullptr || bufferToFill.buffer == nullptr || bufferToFill.buffer->getNumChannels() == 0)
        return;

    const float* channelData = bufferToFill.buffer->getReadPointer (0, bufferToFill.startSample);
    worker->pushAudio (channelData, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
    if (worker != nullptr)
        worker->stop();
    worker.reset();
    pipeline.reset();
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

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    statusLabel.setBounds (area.removeFromTop (60));
    pitchGraph.setBounds (area);
}
