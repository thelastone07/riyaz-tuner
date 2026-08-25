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
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    auto prepareStatus = engine.prepare (sampleRate);
    if (prepareStatus != PitchEngineStatus::Ok)
    {
        juce::MessageManager::callAsync ([this]
        {
            statusLabel.setText ("Could not load the pitch model (models/crepe/small.onnx) - check it exists relative to the app's working directory.",
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

    juce::MessageManager::callAsync ([this]
    {
        statusLabel.setText ("Calibrating - sing a steady, comfortable note for a few seconds...",
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
    lastUpdate = update;

    if (update.phase == PitchPipelinePhase::Calibrating)
    {
        juce::String text;
        switch (update.calibrationStatus)
        {
            case CalibrationStatus::InProgress: text = "Calibrating..."; break;
            case CalibrationStatus::Success:    text = "Calibrated!"; break;
            case CalibrationStatus::Timeout:    text = "Didn't hear a steady note - try again."; break;
            case CalibrationStatus::Unstable:   text = "Pitch was too unstable - try holding a steadier note."; break;
        }
        statusLabel.setText (text, juce::dontSendNotification);
    }
    else if (update.swarLabel.has_value() && update.centsFromSa.has_value())
    {
        juce::String text = "Sa = " + juce::String (update.saHz, 1) + "Hz   "
                           + swarToString (update.swarLabel->swar) + " ("
                           + registerToString (update.swarLabel->octaveRegister) + ")   "
                           + juce::String (update.swarLabel->centsFromCenter, 1) + "c";
        statusLabel.setText (text, juce::dontSendNotification);

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
