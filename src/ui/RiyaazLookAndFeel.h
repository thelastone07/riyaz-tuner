// src/ui/RiyaazLookAndFeel.h
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The shared visual system for every screen in the app - one palette, one
// typeface, one set of component styles - so ProfilePickerComponent,
// MainComponent and AnalyticsComponent can't visually drift apart. Install
// once via juce::LookAndFeel::setDefaultLookAndFeel() before any component
// is constructed (see Main.cpp). Design source: the "Riyaaz visual system
// design" Claude Design project (style guide + screen mockups).
namespace RiyaazColours
{
    // A calm, warm practice-room palette - aged paper and tanpura wood, not
    // a neutral UI grey. Never pure black/white.
    const juce::Colour canvas          { 0xff1C1512 }; // main window fill
    const juce::Colour surface         { 0xff2A2019 }; // buttons/combos/sliders/text-fields' default fill
    const juce::Colour surfaceHover    { 0xff342820 };
    const juce::Colour surfacePressed  { 0xff221A15 };
    const juce::Colour primaryText     { 0xffEDE2D3 };
    const juce::Colour mutedText       { 0xff9C8B7B };
    const juce::Colour placeholderText { 0xff7C6D60 };
    const juce::Colour border          { 0xff3D2F26 };
    const juce::Colour borderHover     { 0xff4A3A2E };
    const juce::Colour gold            { 0xffC6A15B }; // in-tune / success / primary accent
    const juce::Colour goldLitInner    { 0xffE8C983 }; // radial-gradient "lit" fill, inner stop
    const juce::Colour goldLitOuter    { 0xff9A7C3F }; // radial-gradient "lit" fill, outer stop
    const juce::Colour terracotta      { 0xffC1443A }; // out-of-tune / attention
    const juce::Colour indigo          { 0xff7B85B8 }; // live pitch trace / Khali marker
    const juce::Colour disabledFill    { 0xff3A2F27 };
    const juce::Colour disabledText    { 0xff6B5D51 };
    const juce::Colour graphPanel      { 0xff201812 }; // pitch graph / analytics panel background
}

class RiyaazLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RiyaazLookAndFeel();

    // The type system: one serif family, five sizes by role. Georgia - a
    // close, always-installed-on-Windows stand-in for the design system's
    // chosen web font (Spectral), which would otherwise need to be embedded
    // as a bundled font asset. Public so components can request the same
    // sizes the LookAndFeel itself uses for headline-style text that a
    // plain juce::Label's own getLabelFont() can't express (e.g. the 40px
    // display readout).
    static juce::Font displayFont();      // 40px bold - tonic/frequency readouts
    static juce::Font screenTitleFont();  // 24px bold - screen/panel headers
    static juce::Font sectionLabelFont(); // 13px bold, tracked +0.12em - caption labels (this app's default Label style)
    static juce::Font bodyFont();         // 16px regular - values, list rows
    static juce::Font smallMetaFont();    // 12px regular - timestamps, hints

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};
