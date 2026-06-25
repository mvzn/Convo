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

    const float outerR = arcR - lineW * 1.3f;     // stationary bezel outer edge (just inside the arc)
    const float capR   = outerR * 0.85f;          // rotating cap = 85% of the bezel diameter

    // --- soft ambient shadow cast below the knob (premium-hardware grounding) ---
    for (int i = 4; i >= 1; --i)
    {
        const float rr = outerR + (float) i * 1.6f;
        g.setColour (Colours::black.withAlpha (0.09f));
        g.fillEllipse (centre.x - rr, centre.y - rr + 3.5f, rr * 2.0f, rr * 2.0f);
    }

    // --- stationary bezel ring: dark matte metal, top-lit ---
    g.setColour (Colours::black);
    g.fillEllipse (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f);
    g.setGradientFill (ColourGradient (ConvoColours::gunmetalHi, centre.x, centre.y - outerR,
                                       ConvoColours::bg,         centre.x, centre.y + outerR, false));
    g.fillEllipse (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f, 1.0f);

    // --- two thin decorative arc segments embedded in the bezel face ---
    {
        const float bezR = (outerR + capR) * 0.5f;
        Path a1, a2;
        a1.addCentredArc (centre.x, centre.y, bezR, bezR, 0.0f,
                          MathConstants<float>::pi * 1.12f, MathConstants<float>::pi * 1.58f, true);
        a2.addCentredArc (centre.x, centre.y, bezR, bezR, 0.0f,
                          MathConstants<float>::pi * 0.12f, MathConstants<float>::pi * 0.58f, true);
        g.setColour (ConvoColours::gunmetalHi.withAlpha (0.55f));
        g.strokePath (a1, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
        g.strokePath (a2, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // --- recess shadow: a dark ring so the cap reads as sunk a few px into the bezel ---
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (centre.x - capR - 3.0f, centre.y - capR - 3.0f, (capR + 3.0f) * 2.0f, (capR + 3.0f) * 2.0f);

    // --- rotating cap: radial brushed-metal texture ---
    g.setGradientFill (ColourGradient (ConvoColours::gunmetalHi, centre.x, centre.y - capR * 0.5f,
                                       ConvoColours::knobBody,   centre.x, centre.y + capR, true));
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    {
        Graphics::ScopedSaveState clip (g);
        Path capArea; capArea.addEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
        g.reduceClipRegion (capArea);
        const int spokes = 72;                         // faint alternating radial striations -> brushed look
        for (int i = 0; i < spokes; ++i)
        {
            const float a = angle + (float) i / (float) spokes * MathConstants<float>::twoPi;  // rotates with the cap
            const Point<float> d (std::cos (a), std::sin (a));
            g.setColour ((i & 1 ? ConvoColours::gunmetalHi : Colours::black).withAlpha (0.05f));
            g.drawLine ({ centre + d * (capR * 0.12f), centre + d * capR }, 1.0f);
        }
    }
    g.setColour (Colours::black.withAlpha (0.4f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), centre.x, centre.y - capR,
                                       Colours::transparentWhite,        centre.x, centre.y, false));
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);

    // --- unfilled value-arc track + filled value arc (the outer "LED dial", kept) ---
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (ConvoColours::arcTrack);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    if (slider.isEnabled() && sliderPos > 0.0f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setGradientFill (ColourGradient (ConvoColours::accent, centre.x, centre.y + arcR,
                                           ConvoColours::mint,   centre.x, centre.y - arcR, false));
        g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // --- live gain-reduction probe (Duck knob): a thin mint arc in the gap just inside the
    //     value track. It hangs off the set-duck position and recedes toward the start as the
    //     wet is pulled down (full reduction sweeps the whole value arc). The "gr" property is
    //     set per-frame by the editor's timer; absent (-> 0) on every other knob, a no-op there. ---
    const float gr = (float) slider.getProperties().getWithDefault ("gr", 0.0);
    if (gr > 0.001f)
    {
        const float grR   = arcR - lineW * 0.95f;   // dedicated ring between the value arc and the bezel
        const float grEnd = rotaryStartAngle + jmax (0.0f, sliderPos - gr) * (rotaryEndAngle - rotaryStartAngle);
        Path grArc;
        grArc.addCentredArc (centre.x, centre.y, grR, grR, 0.0f, grEnd, angle, true);   // grEnd..set-point
        g.setColour (ConvoColours::mint.withAlpha (0.22f));   // soft glow underlay
        g.strokePath (grArc, PathStrokeType (lineW * 1.1f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (ConvoColours::mint);
        g.strokePath (grArc, PathStrokeType (lineW * 0.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // --- LED indicator dot on the rotating cap, near its outer edge (glows like the arc) ---
    const Point<float> dir (std::cos (angle - MathConstants<float>::halfPi),
                            std::sin (angle - MathConstants<float>::halfPi));
    const Point<float> led = centre + dir * (capR * 0.8f);
    const float ledR = jmax (2.0f, lineW * 0.55f);
    g.setColour (ConvoColours::mint.withAlpha (0.22f));
    g.fillEllipse (Rectangle<float> (ledR * 3.2f, ledR * 3.2f).withCentre (led));
    g.setColour (ConvoColours::mint.withAlpha (0.45f));
    g.fillEllipse (Rectangle<float> (ledR * 2.2f, ledR * 2.2f).withCentre (led));
    g.setColour (ConvoColours::mint);
    g.fillEllipse (Rectangle<float> (ledR * 1.4f, ledR * 1.4f).withCentre (led));
    g.setColour (Colours::white.withAlpha (0.65f));
    g.fillEllipse (Rectangle<float> (ledR * 0.6f, ledR * 0.6f).withCentre (led));
}

void ConvoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace juce;

    const bool on = button.getToggleState();
    auto onColour = button.findColour (ToggleButton::tickColourId);
    if (onColour == Colour())               // fall back if no per-button colour set
        onColour = ConvoColours::mint;

    // --- indicator mode: a bare LED light, no key. Driven externally (read-only) — used for the
    //     Bass Mono "engaged" lamp. Lit + glowing when on, a dim recessed dot when off. ---
    if (button.getProperties().getWithDefault ("indicator", false))
    {
        auto r    = button.getLocalBounds().toFloat();
        const float dotR = jmin (r.getWidth(), r.getHeight()) * 0.30f;   // leave room for the glow halo
        auto dot  = Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (r.getCentre());
        if (on)
        {
            g.setColour (onColour.withAlpha (0.22f)); g.fillEllipse (dot.expanded (dotR * 0.6f));
            g.setColour (onColour.withAlpha (0.45f)); g.fillEllipse (dot.expanded (dotR * 0.28f));
            g.setColour (onColour);                   g.fillEllipse (dot);
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillEllipse (dot.withSizeKeepingCentre (dotR * 0.7f, dotR * 0.7f).translated (0.0f, -dotR * 0.15f));
        }
        else
        {
            g.setColour (ConvoColours::knobBody.darker (0.3f)); g.fillEllipse (dot);
            g.setColour (ConvoColours::border);                 g.drawEllipse (dot, 1.0f);
        }
        return;
    }

    // --- the original small square LED chip on the right, label on the left. Depth is carried by
    //     shadows: off = raised (a soft layered drop shadow), on = lit and pressed in (inner shadow). ---
    auto b      = button.getLocalBounds().toFloat();
    auto ledCol = b.removeFromRight (jmin (b.getHeight(), 22.0f));               // LED column, right of the label
    auto led    = ledCol.withSizeKeepingCentre (ledCol.getWidth() - 6.0f, ledCol.getWidth() - 6.0f);   // keep it SQUARE
    const float rad = 3.0f;
    const bool  pushedIn = on || shouldDrawButtonAsDown;

    if (! pushedIn)   // raised: soft layered drop shadow grounds the chip
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (led.translated (0.0f, (float) i * 0.9f).expanded ((float) i * 0.5f, (float) i * 0.2f), rad + 1.0f);
        }
    }

    if (on)   // glow halo around the lit chip
    {
        g.setColour (onColour.withAlpha (0.30f));
        g.fillRoundedRectangle (led.expanded (3.0f), 6.0f);
    }

    g.setColour (on ? onColour : ConvoColours::knobBody.darker (0.2f));
    g.fillRoundedRectangle (led, rad);

    if (pushedIn)   // pressed in: inner shadow down from the top edge -> recessed
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (led, rad);
        g.reduceClipRegion (clip);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (on ? 0.30f : 0.58f),
                                           led.getCentreX(), led.getY() - 1.0f,
                                           Colours::transparentBlack,
                                           led.getCentreX(), led.getY() + led.getHeight() * 0.7f, false));
        g.fillRect (led);
    }
    else            // raised: faint top highlight (domed chip)
    {
        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawLine (led.getX() + 1.5f, led.getY() + 1.0f, led.getRight() - 1.5f, led.getY() + 1.0f, 1.0f);
    }

    g.setColour (on ? onColour.brighter (0.4f) : ConvoColours::border);
    g.drawRoundedRectangle (led, rad, 1.0f);

    g.setColour (shouldDrawButtonAsHighlighted ? ConvoColours::text : ConvoColours::label);
    g.setFont (14.0f);
    g.drawText (button.getButtonText(), b.reduced (6.0f, 0.0f), Justification::centredRight);
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
