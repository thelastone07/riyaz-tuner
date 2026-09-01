// src/ui/RiyaazLookAndFeel.cpp
#include "RiyaazLookAndFeel.h"

namespace
{
    constexpr float kCornerSize = 3.0f;

    juce::Font makeFont (float size, int styleFlags, float trackingEm = 0.0f)
    {
        juce::Font font (juce::FontOptions ("Georgia", size, styleFlags));
        if (trackingEm > 0.0f)
            font.setExtraKerningFactor (trackingEm);
        return font;
    }
}

juce::Font RiyaazLookAndFeel::displayFont()      { return makeFont (40.0f, juce::Font::bold); }
juce::Font RiyaazLookAndFeel::screenTitleFont()  { return makeFont (24.0f, juce::Font::bold); }
juce::Font RiyaazLookAndFeel::sectionLabelFont() { return makeFont (13.0f, juce::Font::bold, 0.12f); }
juce::Font RiyaazLookAndFeel::bodyFont()         { return makeFont (16.0f, juce::Font::plain); }
juce::Font RiyaazLookAndFeel::smallMetaFont()    { return makeFont (12.0f, juce::Font::plain); }

RiyaazLookAndFeel::RiyaazLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, RiyaazColours::canvas);

    // Most juce::Label instances in this app are section captions ("Tanpura",
    // "Metronome BPM", ...) - this is that default. Screen titles, the status
    // readout and results text are visually distinct enough that each sets
    // its own font/colour explicitly at the call site, which overrides this.
    setColour (juce::Label::textColourId, RiyaazColours::mutedText);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId, RiyaazColours::surface);
    setColour (juce::TextButton::buttonOnColourId, RiyaazColours::surface);
    setColour (juce::TextButton::textColourOffId, RiyaazColours::primaryText);
    setColour (juce::TextButton::textColourOnId, RiyaazColours::primaryText);

    setColour (juce::ComboBox::backgroundColourId, RiyaazColours::surface);
    setColour (juce::ComboBox::outlineColourId, RiyaazColours::border);
    setColour (juce::ComboBox::textColourId, RiyaazColours::primaryText);
    setColour (juce::ComboBox::arrowColourId, RiyaazColours::mutedText);
    setColour (juce::ComboBox::buttonColourId, RiyaazColours::surface);

    setColour (juce::PopupMenu::backgroundColourId, RiyaazColours::surface);
    setColour (juce::PopupMenu::textColourId, RiyaazColours::primaryText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, RiyaazColours::surfaceHover);
    setColour (juce::PopupMenu::highlightedTextColourId, RiyaazColours::gold);

    setColour (juce::Slider::backgroundColourId, RiyaazColours::border);
    setColour (juce::Slider::trackColourId, RiyaazColours::gold);
    setColour (juce::Slider::thumbColourId, RiyaazColours::primaryText);
    setColour (juce::Slider::textBoxTextColourId, RiyaazColours::primaryText);
    setColour (juce::Slider::textBoxBackgroundColourId, RiyaazColours::surface);
    setColour (juce::Slider::textBoxOutlineColourId, RiyaazColours::border);

    setColour (juce::TextEditor::backgroundColourId, RiyaazColours::surface);
    setColour (juce::TextEditor::textColourId, RiyaazColours::primaryText);
    setColour (juce::TextEditor::outlineColourId, RiyaazColours::border);
    setColour (juce::TextEditor::focusedOutlineColourId, RiyaazColours::borderHover);
    setColour (juce::TextEditor::highlightColourId, RiyaazColours::gold.withAlpha (0.35f));

    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, RiyaazColours::border);
}

juce::Font RiyaazLookAndFeel::getLabelFont (juce::Label&)
{
    return sectionLabelFont();
}

juce::Font RiyaazLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return makeFont (juce::jmin (15.0f, (float) buttonHeight * 0.42f), juce::Font::plain);
}

juce::Font RiyaazLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return makeFont (14.0f, juce::Font::plain);
}

juce::Font RiyaazLookAndFeel::getPopupMenuFont()
{
    return makeFont (14.0f, juce::Font::plain);
}

void RiyaazLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    // A toggled-on button (e.g. "Session History" while the history view is
    // showing) gets the same look as hover, permanently - a lightweight way
    // to say "this is the active view" without a second colour scheme.
    const bool isOn = button.getToggleState();
    juce::Colour fill = isOn ? RiyaazColours::surfaceHover : RiyaazColours::surface;
    juce::Colour outline = isOn ? RiyaazColours::borderHover : RiyaazColours::border;

    if (! button.isEnabled())
    {
        fill = RiyaazColours::disabledFill;
        outline = RiyaazColours::border;
    }
    else if (shouldDrawButtonAsDown)
    {
        fill = RiyaazColours::surfacePressed;
    }
    else if (shouldDrawButtonAsHighlighted && ! isOn)
    {
        fill = RiyaazColours::surfaceHover;
        outline = RiyaazColours::borderHover;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, kCornerSize);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds, kCornerSize, 1.0f);
}

void RiyaazLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                         bool /*shouldDrawButtonAsHighlighted*/, bool shouldDrawButtonAsDown)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));

    // Reads button.findColour() rather than a hardcoded constant, so an
    // individual button can opt into permanently-gold "primary action" text
    // (e.g. "Use saved Sa", "Create") via setColour (TextButton::textColourOffId,
    // RiyaazColours::gold) without needing a second styling mechanism -
    // every other button just inherits the LookAndFeel-wide default set in
    // the constructor.
    juce::Colour textColour = button.findColour (juce::TextButton::textColourOffId);
    if (button.getToggleState())
        textColour = RiyaazColours::gold; // toggled-on buttons always read as the accented/active one
    if (! button.isEnabled())
        textColour = RiyaazColours::disabledText;
    else if (shouldDrawButtonAsDown)
        textColour = RiyaazColours::gold; // matches the design system's "pressed state accents gold" specimen

    g.setColour (textColour);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                       juce::Justification::centred, 1);
}

void RiyaazLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                           const juce::Slider::SliderStyle /*style*/, juce::Slider& slider)
{
    const float trackHeight = 4.0f;
    const float trackY = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> track ((float) x, trackY - trackHeight * 0.5f, (float) width, trackHeight);

    g.setColour (RiyaazColours::border);
    g.fillRoundedRectangle (track, trackHeight * 0.5f);

    if (slider.isHorizontal())
    {
        const float fillWidth = juce::jlimit ((float) x, (float) (x + width), sliderPos) - (float) x;
        g.setColour (slider.isEnabled() ? RiyaazColours::gold : RiyaazColours::disabledFill);
        g.fillRoundedRectangle (track.withWidth (fillWidth), trackHeight * 0.5f);
    }

    const float thumbDiameter = 14.0f;
    const auto thumb = juce::Rectangle<float> (thumbDiameter, thumbDiameter).withCentre ({ sliderPos, trackY });
    g.setColour (slider.isEnabled() ? RiyaazColours::primaryText : RiyaazColours::disabledText);
    g.fillEllipse (thumb);
    g.setColour (RiyaazColours::border);
    g.drawEllipse (thumb, 1.0f);
}

void RiyaazLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                       int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (box.isEnabled() ? RiyaazColours::surface : RiyaazColours::disabledFill);
    g.fillRoundedRectangle (bounds, kCornerSize);
    g.setColour (RiyaazColours::border);
    g.drawRoundedRectangle (bounds, kCornerSize, 1.0f);

    const float arrowSize = 9.0f;
    const auto arrowArea = juce::Rectangle<float> (arrowSize, arrowSize)
                                .withCentre ({ (float) width - 16.0f, (float) height * 0.5f });
    juce::Path arrow;
    arrow.addTriangle (arrowArea.getX(), arrowArea.getY(),
                        arrowArea.getRight(), arrowArea.getY(),
                        arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour (box.isEnabled() ? RiyaazColours::mutedText : RiyaazColours::disabledText);
    g.fillPath (arrow);
}

void RiyaazLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (RiyaazColours::surface);
    g.fillRect (0, 0, width, height);
    g.setColour (RiyaazColours::border);
    g.drawRect (0, 0, width, height, 1);
}

void RiyaazLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, kCornerSize);
}

void RiyaazLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    g.setColour (editor.hasKeyboardFocus (true) ? RiyaazColours::borderHover : RiyaazColours::border);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, kCornerSize, 1.0f);
}
