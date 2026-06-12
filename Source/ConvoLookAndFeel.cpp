#include "ConvoLookAndFeel.h"

ConvoLookAndFeel::ConvoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ConvoColours::bg);

    setColour (juce::Slider::textBoxTextColourId,    ConvoColours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, ConvoColours::accent);

    setColour (juce::Label::textColourId, ConvoColours::label);

    setColour (juce::ToggleButton::textColourId, ConvoColours::text);

    setColour (juce::TextButton::buttonColourId,   ConvoColours::panelRaised);
    setColour (juce::TextButton::textColourOffId,  ConvoColours::mint);
    setColour (juce::TextButton::textColourOnId,   ConvoColours::mint);

    setColour (juce::PopupMenu::backgroundColourId,        ConvoColours::panel);
    setColour (juce::PopupMenu::textColourId,              ConvoColours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ConvoColours::accent);
}

void ConvoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    using namespace juce;

    auto bounds   = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    auto radius   = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre   = bounds.getCentre();
    auto lineW    = radius * 0.14f;
    auto arcR     = radius - lineW * 0.5f;
    auto angle    = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto bodyR    = arcR - lineW * 1.3f;

    // knob body (gunmetal) with a faint top highlight and grounding shadow ring
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (centre.x - bodyR - 1.5f, centre.y - bodyR - 0.5f, bodyR * 2.0f + 3.0f, bodyR * 2.0f + 3.0f);
    g.setColour (ConvoColours::knobBody);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setGradientFill (ColourGradient (ConvoColours::gunmetalHi.withAlpha (0.35f), centre.x, centre.y - bodyR,
                                       ConvoColours::knobBody.withAlpha (0.0f),    centre.x, centre.y, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (ConvoColours::border);
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // unfilled arc track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (ConvoColours::arcTrack);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // filled value arc (phthalo -> mint)
    if (slider.isEnabled() && sliderPos > 0.0f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setGradientFill (ColourGradient (ConvoColours::accent, centre.x, centre.y + arcR,
                                           ConvoColours::mint,   centre.x, centre.y - arcR, false));
        g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // pointer: a line from mid-body to the rim, capped with a dot
    const auto dir = Point<float> (std::cos (angle - MathConstants<float>::halfPi),
                                   std::sin (angle - MathConstants<float>::halfPi));
    const auto inner = centre + dir * (bodyR * 0.45f);
    const auto outer = centre + dir * (bodyR * 0.92f);
    g.setColour (ConvoColours::mint);
    g.drawLine ({ inner, outer }, jmax (2.0f, lineW * 0.6f));
    const float dot = lineW * 0.95f;
    g.fillEllipse (Rectangle<float> (dot, dot).withCentre (outer));
}

void ConvoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool)
{
    using namespace juce;

    const bool on = button.getToggleState();
    auto onColour = button.findColour (ToggleButton::tickColourId);
    if (onColour == Colour())               // fall back if no per-button colour set
        onColour = ConvoColours::mint;

    auto b   = button.getLocalBounds().toFloat();
    auto led = b.removeFromLeft (b.getHeight()).reduced (4.0f);

    if (on)   // soft glow
    {
        g.setColour (onColour.withAlpha (0.25f));
        g.fillRoundedRectangle (led.expanded (2.5f), 5.0f);
    }

    g.setColour (on ? onColour : ConvoColours::knobBody);
    g.fillRoundedRectangle (led, 3.0f);
    g.setColour (on ? onColour.brighter (0.4f) : ConvoColours::border);
    g.drawRoundedRectangle (led, 3.0f, 1.0f);

    g.setColour (shouldDrawButtonAsHighlighted ? ConvoColours::text : ConvoColours::label);
    g.setFont (14.0f);
    g.drawText (button.getButtonText(), b.reduced (6.0f, 0.0f), Justification::centredLeft);
}

void ConvoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&, bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    using namespace juce;

    auto b = button.getLocalBounds().toFloat().reduced (0.5f);

    auto base = ConvoColours::panelRaised;
    if (shouldDrawButtonAsDown)            base = ConvoColours::accentDeep;
    else if (shouldDrawButtonAsHighlighted) base = ConvoColours::knobBody.brighter (0.12f);

    g.setColour (base);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (shouldDrawButtonAsHighlighted ? ConvoColours::mint : ConvoColours::border);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
}

juce::Font ConvoLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}
