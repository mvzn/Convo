/*
    Convo — a convolution audio effect plugin
    Copyright (C) 2026 mvzn

    This program is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option) any
    later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License along
    with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "PluginEditor.h"
#include "UpdateCheck.h"

namespace
{
    void styleRotary (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 17);
        // set these on the slider itself: the textbox label copies its colours at
        // creation, before the slider is inside the themed component hierarchy
        s.setColour (juce::Slider::textBoxTextColourId,    ConvoColours::text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    juce::Font captionFont()
    {
        return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.12f);
    }

    // The caption row of a panel (mint tick + label sit here). Shared so the baked tick
    // (renderBackground) and the live-drawn label (drawChromeText) stay pixel-aligned.
    juce::Rectangle<int> captionStrip (juce::Rectangle<int> panel)
    {
        return panel.reduced (12, 0).removeFromTop (24).translated (0, 3);
    }

    const juce::Colour meterAmber { 0xffd9a13b };
}

ConvoAudioProcessorEditor::ConvoAudioProcessorEditor (ConvoAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);                       // paint() covers everything — skip host compositing

    convo::updateInfo();                    // kick off the once-per-process "newer release?" check

    thumbnailFormatManager.registerBasicFormats();
    thumbnail = std::make_unique<juce::AudioThumbnail> (512, thumbnailFormatManager, thumbnailCache);
    thumbnail->addChangeListener (this);

    auto& apvts = processor.getAPVTS();

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        styleRotary (s);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.5f)));
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, ConvoColours::label);
        addAndMakeVisible (l);
    };

    setup (drySlider,      dryLabel,      "Dry");
    setup (wetSlider,      wetLabel,      "Wet");
    setup (irGainSlider,   irGainLabel,   "IR Gain");
    setup (toneSlider,     toneLabel,     "Tone");
    setup (inHPSlider,     inHPLabel,     "In HP");
    setup (inLPSlider,     inLPLabel,     "In LP");
    setup (filterQSlider,  filterQLabel,  "Q");
    setup (preDelaySlider, preDelayLabel, "Pre-Delay");
    setup (widthSlider,    widthLabel,    "Width");
    setup (msBassSlider,   msBassLabel,   "Bass Mono");
    setup (duckSlider,     duckLabel,     "Duck");
    setup (duckRelSlider,  duckRelLabel,  "Release");
    setup (gateSlider,     gateLabel,     "Gate");
    setup (fadeInSlider,   fadeInLabel,   "Fade In");
    setup (decaySlider,    decayLabel,    "Decay");
    setup (taperSlider,    taperLabel,    "Taper");
    setup (stretchSlider,  stretchLabel,  "Stretch");
    setup (dampSlider,     dampLabel,     "Damp");

    wetCompButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);
    filterIRButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::copper);   // orange = IR-baked filter
    polarityButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::mint);
    bypassButton.setColour    (juce::ToggleButton::tickColourId, ConvoColours::copper);
    polarityButton.setTooltip ("Invert the polarity (phase) of the wet signal");

    // Reverse + Norm IR: plain buttons; the text colour (grey off -> mint on) shows state, animated
    for (auto* b : { &reverseButton, &irNormButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    irNormButton.setTooltip   ("Normalize the IR to unit energy. Off (default) = the IR's raw "
                               "recorded level, which can convolve hot on dense material");
    wetCompButton.setTooltip  ("Adaptive wet gain compensation: tracks the dry input level "
                               "and trims the wet to match; frozen while the input is quiet "
                               "so tails ring out");
    inHPSlider.setTooltip ("Pre-IR high-pass (low cut), 12 dB/oct, on the signal feeding the IR");
    inLPSlider.setTooltip ("Pre-IR low-pass (high cut), 12 dB/oct, on the signal feeding the IR");
    fadeInSlider.setTooltip ("Raised-cosine fade-in baked into the IR; the ramp is capped at 80% of the IR length");
    msBassSlider.setTooltip ("Bass Mono crossover: the wet collapses to mono below this frequency "
                             "and stays stereo above it. 20 Hz = off; turn it up to engage Bass Mono");
    filterIRButton.setTooltip ("Apply the In HP/In LP filter to the IR (baked, shown in the "
                               "display) instead of the input. Same sound; IR mode is cheaper "
                               "at runtime but re-bakes when you move the cutoffs");

    // --- mix / output ---
    drySlider.setTooltip    ("Level of the unprocessed (dry) signal in the mix");
    wetSlider.setTooltip    ("Level of the convolved (wet) signal in the mix");
    irGainSlider.setTooltip ("Gain of the impulse response itself (scales the wet convolution, "
                             "and the waveform height above). Separate from Wet (the wet mix level)");
    gateSlider.setTooltip    ("Gate on the dry signal feeding the IR: cuts the convolution input when the "
                              "input drops below the threshold (the tail still rings out). Release = Duck Release. 0% = off");
    gateSlider.getProperties().set ("gateAct", 0.0);   // marks this as the Gate knob (grey arc -> mint when active)
    filterQSlider.setTooltip ("Resonance/Q for the In HP & In LP corners: 0% = flat, up = a resonant peak at the cutoffs");

    // --- wet shaping (post-convolution, real-time) ---
    toneSlider.setTooltip     ("Spectral tilt on the wet: turn down to darken (lows up, highs down), "
                               "up to brighten. +/-12 dB shelves around 700 Hz");
    widthSlider.setTooltip    ("Stereo width of the wet (M/S): 0% = mono, 100% = unchanged, 200% = extra wide");
    preDelaySlider.setTooltip ("Delays the wet only (creative pre-delay); the dry tap stays in place. "
                               "Not reported as plugin latency");
    duckSlider.setTooltip     ("Ducks the wet by the dry input level (louder input pulls the wet back, "
                               "so transients stay clear)");
    duckRelSlider.setTooltip  ("How quickly the wet recovers after the input stops ducking it");

    // --- IR bake controls ---
    reverseButton.setTooltip ("Reverse the impulse response before shaping (classic reverse reverb)");
    decaySlider.setTooltip   ("Imposes an exponential decay on the IR and truncates the tail, relative to the "
                              "baked (trimmed) length: 0% = Off (tail as recorded), turn up to shorten the tail");
    taperSlider.setTooltip   ("Raised-cosine fade-out baked onto the IR's tail to de-click the kernel end");
    stretchSlider.setTooltip ("Time-stretch the IR (resampled in the bake): below 100% shortens it, "
                              "above 100% lengthens it. 100% = off");
    dampSlider.setTooltip    ("Damping (baked): a progressive high-frequency rolloff over the tail "
                              "(air absorption): the reverb gets darker as it decays. 0% = off");

    // --- output guards / global ---
    bypassButton.setTooltip    ("Passes the dry input through at unity");

    for (auto* b : { &filterIRButton, &wetCompButton, &polarityButton, &bypassButton })
    {
        b->setColour (juce::ToggleButton::textColourId, ConvoColours::label);
        addAndMakeVisible (*b);
    }

    dryAtt      = std::make_unique<SliderAttachment> (apvts, "dry",         drySlider);
    wetAtt      = std::make_unique<SliderAttachment> (apvts, "wet",         wetSlider);
    irGainAtt   = std::make_unique<SliderAttachment> (apvts, "irGain",      irGainSlider);
    filterQAtt  = std::make_unique<SliderAttachment> (apvts, "filterQ",     filterQSlider);
    gateAtt     = std::make_unique<SliderAttachment> (apvts, "gate",        gateSlider);
    toneAtt     = std::make_unique<SliderAttachment> (apvts, "tone",        toneSlider);
    inHPAtt     = std::make_unique<SliderAttachment> (apvts, "inHP",        inHPSlider);
    inLPAtt     = std::make_unique<SliderAttachment> (apvts, "inLP",        inLPSlider);
    preDelayAtt = std::make_unique<SliderAttachment> (apvts, "preDelay",    preDelaySlider);
    widthAtt    = std::make_unique<SliderAttachment> (apvts, "width",       widthSlider);
    msBassAtt   = std::make_unique<SliderAttachment> (apvts, "msBass",      msBassSlider);
    duckAtt     = std::make_unique<SliderAttachment> (apvts, "duck",        duckSlider);
    duckRelAtt  = std::make_unique<SliderAttachment> (apvts, "duckRelease", duckRelSlider);
    fadeInAtt   = std::make_unique<SliderAttachment> (apvts, "fadeIn",      fadeInSlider);
    decayAtt    = std::make_unique<SliderAttachment> (apvts, "decay",       decaySlider);
    taperAtt    = std::make_unique<SliderAttachment> (apvts, "taper",       taperSlider);
    stretchAtt  = std::make_unique<SliderAttachment> (apvts, "stretch",     stretchSlider);
    dampAtt     = std::make_unique<SliderAttachment> (apvts, "damp",        dampSlider);
    reverseAtt   = std::make_unique<ButtonAttachment> (apvts, "reverse",   reverseButton);
    irNormAtt    = std::make_unique<ButtonAttachment> (apvts, "irNorm",    irNormButton);
    filterIRAtt  = std::make_unique<ButtonAttachment> (apvts, "filterIR",  filterIRButton);
    wetCompAtt   = std::make_unique<ButtonAttachment> (apvts, "wetComp",   wetCompButton);
    polarityAtt  = std::make_unique<ButtonAttachment> (apvts, "polarity",  polarityButton);
    bypassAtt    = std::make_unique<ButtonAttachment> (apvts, "bypass",    bypassButton);

    // show the unit on each knob's value box (dB / Hz / ms / %). The slider attachment points
    // textFromValueFunction at the parameter's getText, which omits the label, so wrap it to
    // append the parameter's own unit — skipping values that already carry one (e.g. Decay's
    // "Off" / "... ms") so nothing gets doubled.
    struct { juce::Slider* s; const char* id; } unitSliders[] = {
        { &drySlider, "dry" }, { &wetSlider, "wet" }, { &irGainSlider, "irGain" },
        { &toneSlider, "tone" }, { &inHPSlider, "inHP" }, { &filterQSlider, "filterQ" }, { &gateSlider, "gate" },
        { &inLPSlider, "inLP" }, { &preDelaySlider, "preDelay" }, { &widthSlider, "width" },
        { &msBassSlider, "msBass" }, { &duckSlider, "duck" }, { &duckRelSlider, "duckRelease" },
        { &fadeInSlider, "fadeIn" }, { &decaySlider, "decay" }, { &taperSlider, "taper" },
        { &stretchSlider, "stretch" }, { &dampSlider, "damp" }
    };
    for (auto& u : unitSliders)
    {
        if (auto* rp = apvts.getParameter (u.id))
        {
            u.s->textFromValueFunction = [rp] (double v)
            {
                const auto txt  = rp->getText (rp->convertTo0to1 ((float) v), 0);
                const auto unit = rp->getLabel();
                if (unit.isEmpty() || txt.endsWith (unit) || ! txt.containsAnyOf ("0123456789"))
                    return txt;
                return txt + " " + unit;
            };
            u.s->updateText();
        }
    }

    lastFileName = processor.getIRLibrary().getDisplayName();
    fileNameLabel.setText (lastFileName, juce::dontSendNotification);
    fileNameLabel.setFont (juce::Font (juce::FontOptions (13.5f)));
    fileNameLabel.setColour (juce::Label::textColourId, ConvoColours::text);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);

    presetButton.setTooltip     ("Save the current settings as a preset, or pick a saved one");
    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    presetButton.onClick     = [this] { showPresetMenu(); };
    prevPresetButton.onClick = [this] { stepPreset (-1); };
    nextPresetButton.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (presetButton);
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    // IR audition: Play (toggles audition on/off) + a source toggle (Baked = processed kernel,
    // Raw = original IR). The Play label reflects the live audition state, driven by the timer.
    playButton.setTooltip ("Audition the impulse response through the output (solos the IR)");
    // Play: text colour goes grey -> mint while playing (animated); the button itself stays plain
    playButton.onClick = [this]
    {
        if (processor.isAuditioning()) processor.stopAudition();
        else                           processor.startAudition (auditionSrcButton.getToggleState());
    };
    auditionSrcButton.setTooltip ("Audition source - Baked: after all processing; Raw: the original IR");
    auditionSrcButton.setClickingTogglesState (true);
    auditionSrcButton.setToggleState (true, juce::dontSendNotification);   // default: Baked
    auditionSrcButton.onClick = [this]   // text colour crossfades mint (Raw) <-> copper (Baked) in the timer
    { auditionSrcButton.setButtonText (auditionSrcButton.getToggleState() ? "Baked" : "Raw"); };
    // Play + Baked/Raw form one segmented pill (joined visuals, separate clicks)
    playButton.getProperties().set        ("segment", "left");
    auditionSrcButton.getProperties().set ("segment", "right");
    addAndMakeVisible (playButton);
    addAndMakeVisible (auditionSrcButton);

    // seed the animated text colours so on-state buttons start coloured (not grey) at open
    reverseLit = reverseButton.getToggleState() ? 1.0f : 0.0f;
    normLit    = irNormButton.getToggleState()  ? 1.0f : 0.0f;
    bakedBlend = auditionSrcButton.getToggleState() ? 1.0f : 0.0f;
    setLedTextColour (playButton,        playLit,    ConvoColours::label, ConvoColours::mint);
    setLedTextColour (reverseButton,     reverseLit, ConvoColours::label, ConvoColours::mint);
    setLedTextColour (irNormButton,      normLit,    ConvoColours::label, ConvoColours::mint);
    setLedTextColour (auditionSrcButton, bakedBlend, ConvoColours::mint,  ConvoColours::copper);

    rebuildThumbnail();

    setSize (900, 617);   // +7 px vs. before to keep the bottom row clear of the wider header gap
    startTimerHz (30);
}

ConvoAudioProcessorEditor::~ConvoAudioProcessorEditor()
{
    stopTimer();
    thumbnail->removeChangeListener (this);
    setLookAndFeel (nullptr);
}

float ConvoAudioProcessorEditor::uiScale() const
{
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        return (float) display->scale;
    return 1.0f;
}

void ConvoAudioProcessorEditor::rebuildThumbnail()
{
    const auto&  ir = processor.getBakedIR();
    const double sr = processor.getBakedIRSampleRate();

    if (ir.getNumSamples() > 0 && sr > 0.0)
    {
        const int n = ir.getNumSamples();
        // ~one thumbnail point per output pixel (1400 ≈ the display width at 2x HiDPI), so
        // a short IR still gets enough points to draw smoothly rather than as wide bricks.
        const int srcPerPoint = juce::jmax (1, n / 1400);
        thumbnail->removeChangeListener (this);
        thumbnail = std::make_unique<juce::AudioThumbnail> (srcPerPoint, thumbnailFormatManager, thumbnailCache);
        thumbnail->addChangeListener (this);
        thumbnail->reset (ir.getNumChannels(), sr, n);
        thumbnail->addBlock (0, ir, 0, n);
    }
    else
    {
        thumbnail->clear();
    }

    // kernel layer: the trimmed+shaped audio kernel, shown sharp inside the trim selection.
    // The length readout reads from this kernel (the actual convolved buffer), not the full
    // backdrop, so it reflects the trim (and decay truncation) rather than the whole IR.
    {
        const auto&  kir = processor.getKernelIR();
        const double ksr = processor.getBakedIRSampleRate();
        if (kir.getNumSamples() > 0 && ksr > 0.0)
        {
            const int kn = kir.getNumSamples();
            kernelThumbnail = std::make_unique<juce::AudioThumbnail> (juce::jmax (1, kn / 1400),
                                                                      thumbnailFormatManager, kernelThumbnailCache);
            kernelThumbnail->reset (kir.getNumChannels(), ksr, kn);
            kernelThumbnail->addBlock (0, kir, 0, kn);
            bakedLenSeconds = kn / ksr;
            bakedLenText = juce::String (bakedLenSeconds, 2) + " s";
        }
        else
        {
            if (kernelThumbnail != nullptr) kernelThumbnail->clear();
            bakedLenSeconds = 0.0;
            bakedLenText.clear();
        }
    }

    lastBakeGen = processor.getBakeGeneration();
    renderWaveImage();
    renderKernelImage();
    repaint (dropZone);
}

// ---------------------------------------------------------------------------
// cached rendering
// ---------------------------------------------------------------------------

void ConvoAudioProcessorEditor::renderWaveImage()
{
    waveImage = juce::Image();
    waveBlurImage = juce::Image();
    if (waveZone.isEmpty() || thumbnail->getTotalLength() <= 0.0)
        return;

    const float scale = uiScale();
    const int   w     = juce::jmax (1, juce::roundToInt ((float) waveZone.getWidth()  * scale));
    const int   h     = juce::jmax (1, juce::roundToInt ((float) waveZone.getHeight() * scale));

    waveImage = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (waveImage);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto local = juce::Rectangle<int> (waveZone.getWidth(), waveZone.getHeight());
    g.setGradientFill (juce::ColourGradient (ConvoColours::mint, 0.0f, 0.0f,
                                             ConvoColours::teal.withAlpha (0.75f),
                                             0.0f, (float) local.getHeight(), false));
    thumbnail->drawChannels (g, local, 0.0, thumbnail->getTotalLength(), irGainVisualGain());

    // a blurred copy, built once here (never per frame), so the trim preview can show the
    // unselected head/tail out of focus at zero per-paint cost
    waveBlurImage = juce::Image (juce::Image::ARGB, w, h, true);
    juce::ImageConvolutionKernel blur (9);
    blur.createGaussianBlur (2.5f);
    blur.applyToImage (waveBlurImage, waveImage, waveBlurImage.getBounds());
}

// The trimmed+shaped kernel rendered across the full wave width; paint() scales it into the
// selection rectangle so the fade/decay/taper land at the Start/End handles. Rebuilt only on a
// commit (incl. trim-release), never during a drag.
void ConvoAudioProcessorEditor::renderKernelImage()
{
    kernelImage = juce::Image();
    if (waveZone.isEmpty() || kernelThumbnail == nullptr || kernelThumbnail->getTotalLength() <= 0.0)
        return;

    const float scale = uiScale();
    const int   w     = juce::jmax (1, juce::roundToInt ((float) waveZone.getWidth()  * scale));
    const int   h     = juce::jmax (1, juce::roundToInt ((float) waveZone.getHeight() * scale));

    kernelImage = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (kernelImage);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto local = juce::Rectangle<int> (waveZone.getWidth(), waveZone.getHeight());
    g.setGradientFill (juce::ColourGradient (ConvoColours::mint, 0.0f, 0.0f,
                                             ConvoColours::teal.withAlpha (0.75f),
                                             0.0f, (float) local.getHeight(), false));
    kernelThumbnail->drawChannels (g, local, 0.0, kernelThumbnail->getTotalLength(), irGainVisualGain());
}

// IR Gain mapped to the waveform's vertical zoom, so the displayed amplitude tracks the wet
// trim. 0 dB = 1.0 (unchanged); +6 dB doubles it, lower values shrink it toward the baseline.
float ConvoAudioProcessorEditor::irGainVisualGain() const
{
    return juce::Decibels::decibelsToGain (
        processor.getAPVTS().getRawParameterValue ("irGain")->load(), -60.0f);
}

void ConvoAudioProcessorEditor::renderBackground()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    const float scale = uiScale();
    backgroundImage = juce::Image (juce::Image::ARGB,
                                   juce::jmax (1, juce::roundToInt ((float) getWidth()  * scale)),
                                   juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)),
                                   true);
    juce::Graphics g (backgroundImage);
    g.addTransform (juce::AffineTransform::scale (scale));

    // window background: a barely-there vertical falloff sells depth at zero runtime cost
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff121a1e), 0.0f, 0.0f,
                                             juce::Colour (0xff0b1013), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // header: wordmark + dim tagline (both drawn live in drawChromeText for crisp text) +
    // mint tick + hairline rule (baked here — graphics scale cleanly)
    {
        auto h = headerZone;
        h.removeFromLeft (92);   // reserve the wordmark column so the tick lands beside it

        g.setColour (ConvoColours::mint.withAlpha (0.85f));
        g.fillRect (h.getX() + 2, headerZone.getCentreY() - 8, 2, 16);

        // engraved header rule: a dark cut + a 1 px catch-light just below, so the divider reads
        // as incised into the chassis (spans the full content width)
        const auto content = getLocalBounds().reduced (14);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (content.getX(), headerZone.getBottom() + 3, content.getWidth(), 1);
        g.setColour (ConvoColours::gunmetalHi.withAlpha (0.25f));
        g.fillRect (content.getX(), headerZone.getBottom() + 4, content.getWidth(), 1);
    }

    auto panel = [&g] (juce::Rectangle<int> r, const juce::String& caption)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 })
            .drawForRectangle (g, r);
        g.setColour (ConvoColours::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        // brushed-metal "makeup": a faint top-lit sheen + hairline top highlight, matching the knob
        // bezels so the whole chassis reads as one piece of dark hardware
        g.setGradientFill (juce::ColourGradient (ConvoColours::gunmetalHi.withAlpha (0.12f), 0.0f, (float) r.getY(),
                                                 ConvoColours::panel.withAlpha (0.0f),       0.0f, (float) r.getCentreY(), false));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawLine ((float) r.getX() + 6.0f, (float) r.getY() + 1.0f, (float) r.getRight() - 6.0f, (float) r.getY() + 1.0f, 1.0f);
        g.setColour (ConvoColours::border);
        g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);

        if (caption.isNotEmpty())   // mint tick baked here; the label is drawn live in drawChromeText
        {
            auto strip = captionStrip (r);
            g.setColour (ConvoColours::mint.withAlpha (0.85f));
            g.fillRect (strip.getX(), strip.getCentreY() - 5, 3, 10);
        }
    };

    panel (dropZone,    {});
    panel (filterPanel, "FILTER");
    panel (postPanel,   "POST");
    panel (volumePanel, "VOLUME");
    panel (duckPanel,   "DUCKING");
    panel (shapePanel,  "IR SHAPE");
    panel (charPanel,   "IR CHARACTER");

    // meter wells with faint scale ticks, labels underneath
    for (const auto& zone : { inMeterZone, outMeterZone })
    {
        g.setColour (juce::Colour (0xff0a0e10));
        g.fillRoundedRectangle (zone.toFloat(), 3.0f);
        g.setColour (ConvoColours::border);
        g.drawRoundedRectangle (zone.toFloat(), 3.0f, 1.0f);

        g.setColour (ConvoColours::border.withAlpha (0.5f));
        for (float frac : { 0.25f, 0.5f, 0.75f })
        {
            const int y = zone.getBottom() - juce::roundToInt (zone.getHeight() * frac);
            g.fillRect (zone.getX() + 2, y, zone.getWidth() - 4, 1);
        }
    }
    // IN / OUT labels are drawn live in drawChromeText (crisp text)
}

// All static chrome *text* is drawn here, live in paint() at the editor's true device
// resolution, rather than baked into the (scaled) backgroundImage — baking softens glyphs
// on HiDPI/fractional-scale displays. Cheap (a handful of short strings) and clipped to the
// repaint region, so the cached-graphics optimisation is untouched.
void ConvoAudioProcessorEditor::drawChromeText (juce::Graphics& g)
{
    // header wordmark + tagline
    {
        auto h = headerZone;
        g.setColour (ConvoColours::text);
        g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        // nudge the wordmark up a few px: the 26 pt bold's large descent makes box-centred
        // text sit visually low next to the small-caps tagline / toggle row
        g.drawText ("Convo", h.removeFromLeft (92).translated (0, -2), juce::Justification::centredLeft);

        g.setColour (ConvoColours::textDim);
        g.setFont (captionFont());
        g.drawText ("CONVOLUTION", h.withTrimmedLeft (12), juce::Justification::centredLeft);
    }

    // "i" info affordance for the about / AGPL legal notice (click handled in mouseDown).
    // Turns copper when a newer release is available, as a nudge to open the popup.
    {
        const bool upd = convo::updateInfo()->available.load (std::memory_order_acquire);
        const auto a   = aboutZone.toFloat();
        g.setColour (upd ? ConvoColours::copper : ConvoColours::textDim);
        g.drawEllipse (a.reduced (1.5f), 1.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("i", aboutZone, juce::Justification::centred);
    }

    // panel captions (mint ticks are baked; only the labels are live)
    g.setColour (ConvoColours::textDim);
    g.setFont (captionFont());
    const std::pair<juce::Rectangle<int>, const char*> panels[] = {
        { filterPanel, "FILTER" }, { postPanel, "POST" }, { volumePanel, "VOLUME" },
        { duckPanel, "DUCKING" }, { shapePanel, "IR SHAPE" }, { charPanel, "IR CHARACTER" }
    };
    for (const auto& pc : panels)
        g.drawText (pc.second, captionStrip (pc.first).withTrimmedLeft (10), juce::Justification::centredLeft);

    // meter labels
    g.drawText ("IN",  inMeterZone.getX(),  inMeterZone.getBottom() + 2,  inMeterZone.getWidth(),  14, juce::Justification::centred);
    g.drawText ("OUT", outMeterZone.getX(), outMeterZone.getBottom() + 2, outMeterZone.getWidth(), 14, juce::Justification::centred);
}

// ---------------------------------------------------------------------------
// painting (everything heavy is pre-rendered; this just blits + dynamics)
// ---------------------------------------------------------------------------

void ConvoAudioProcessorEditor::drawMeterFill (juce::Graphics& g, juce::Rectangle<int> zone,
                                               float level, float peak, const juce::ColourGradient& fillGrad)
{
    const auto well = zone.toFloat().reduced (1.5f);

    // clip to the rounded well so the fill and peak line follow its rounded corners — otherwise a
    // full (100%) meter's square top edges overhang the rounded frame
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path wellClip;
    wellClip.addRoundedRectangle (well, 2.0f);
    g.reduceClipRegion (wellClip);

    const float l = juce::jlimit (0.0f, 1.0f, level);
    if (l > 0.001f)
    {
        // gradient (phthalo low, mint mid, amber hot, red top) is cached in resized() so the
        // meter — repainted ~30 Hz throughout playback — never reallocates a ColourGradient
        g.setGradientFill (fillGrad);
        auto fill = well;
        g.fillRect (fill.removeFromBottom (well.getHeight() * l));   // rounded corners come from the clip
    }

    const float p = juce::jlimit (0.0f, 1.0f, peak);
    if (p > 0.001f)
    {
        const float y = well.getBottom() - well.getHeight() * p;
        g.setColour (p > 0.95f ? ConvoColours::clip : ConvoColours::mint.brighter (0.3f));
        g.fillRect (well.getX(), y - 1.0f, well.getWidth(), 2.0f);
    }
}

// (Re)build the cached frequency-response overlay: the display width is a log 20 Hz -> 20 kHz
// axis and the curve is the net magnitude of the wet EQ — Tone tilt (post-conv) times the
// pre-IR HP/LP. Called only when those params (or the wave zone) change, so paint() never
// recomputes it. The four Coefficients are reused (updated in place) — no per-redraw alloc.
void ConvoAudioProcessorEditor::renderOverlay()
{
    eqCurvePath.clear();
    monoMarkerX = -1.0f;

    const auto zone = waveZone.toFloat();
    if (zone.isEmpty())
        return;

    auto& apvts = processor.getAPVTS();
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;

    const float tiltDb = apvts.getRawParameterValue ("tone")->load() * 0.01f * 12.0f;   // matches processBlock
    const float hpHz   = apvts.getRawParameterValue ("inHP")->load();
    const float lpHz   = apvts.getRawParameterValue ("inLP")->load();
    // shared resonance/Q, mapped exactly as the processor's mapFilterQ (0% = 0.707 .. 100% = 6.0)
    const double fq = (double) juce::jmap (juce::jlimit (0.0f, 100.0f, apvts.getRawParameterValue ("filterQ")->load()) * 0.01f,
                                           0.707f, 6.0f);

    if (eqLo == nullptr)   // allocate the reused coefficient objects once
    {
        eqLo = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, 700.0, 0.5, 1.0f);
        eqHi = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 700.0, 0.5, 1.0f);
        eqHp = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 20.0, 0.707);
        eqLp = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, 20000.0, 0.707);
    }
    *eqLo = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (sr, 700.0, 0.5, juce::Decibels::decibelsToGain (-tiltDb));
    *eqHi = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 700.0, 0.5, juce::Decibels::decibelsToGain ( tiltDb));
    *eqHp = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sr, (double) hpHz, fq);
    *eqLp = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass  (sr, (double) lpHz, fq);

    constexpr double fLo = 20.0, fHi = 20000.0;
    constexpr float  dbSpan = 15.0f;                 // +/- this maps to half the zone height
    const float halfH = zone.getHeight() * 0.5f - 4.0f;
    const float midY  = zone.getCentreY();
    const int   cols  = juce::jmax (2, (int) zone.getWidth());   // one point per pixel -> stays sharp

    for (int i = 0; i <= cols; ++i)
    {
        const double t   = (double) i / (double) cols;
        const double f   = fLo * std::pow (fHi / fLo, t);
        const double mag = eqLo->getMagnitudeForFrequency (f, sr) * eqHi->getMagnitudeForFrequency (f, sr)
                         * eqHp->getMagnitudeForFrequency (f, sr) * eqLp->getMagnitudeForFrequency (f, sr);
        const float db = (float) juce::Decibels::gainToDecibels (mag, -100.0);
        const float x  = zone.getX() + (float) t * zone.getWidth();
        const float y  = midY - juce::jlimit (-1.0f, 1.0f, db / dbSpan) * halfH;
        if (i == 0) eqCurvePath.startNewSubPath (x, y);
        else        eqCurvePath.lineTo (x, y);
    }

    // bass-mono crossover marker — shown whenever the mode is engaged (crossover above 20 Hz)
    {
        const float bass = apvts.getRawParameterValue ("msBass")->load();
        if (bass > 20.5f)
        {
            const double tt = std::log (bass / fLo) / std::log (fHi / fLo);
            monoMarkerX = zone.getX() + (float) juce::jlimit (0.0, 1.0, tt) * zone.getWidth();
        }
    }
}

// Stroke the cached overlay — no recompute. Mint glow curve + copper bass-mono marker.
void ConvoAudioProcessorEditor::drawFilterOverlay (juce::Graphics& g)
{
    if (eqCurvePath.isEmpty())
        return;

    const auto curveCol = ConvoColours::mint;
    g.setColour (curveCol.withAlpha (0.08f));
    g.strokePath (eqCurvePath, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
    g.setColour (curveCol.withAlpha (0.14f));
    g.strokePath (eqCurvePath, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
    g.setColour (curveCol.withAlpha (0.5f));
    g.strokePath (eqCurvePath, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));

    const auto zone = waveZone.toFloat();
    constexpr double fLo = 20.0, fHi = 20000.0;

    // frequency labels along the bottom (log 20 Hz – 20 kHz), nudged below the wave by freqMarkY.
    // Spaced on the 1-2-5 sequence and kept off the 20 Hz / 20 kHz edges so labels never overlap.
    {
        const float freqMarkY = 12.5f;   // <-- vertical nudge of the frequency labels (px, + = down)
        const float baseY = zone.getBottom() + freqMarkY;
        auto fx = [&] (double f) { return zone.getX() + (float) (std::log (f / fLo) / std::log (fHi / fLo)) * zone.getWidth(); };

        struct { double f; const char* txt; } refs[] = {
            { 50.0, "50Hz" }, { 100.0, "100Hz" }, { 200.0, "200Hz" }, { 500.0, "500Hz" },
            { 1000.0, "1kHz" }, { 2000.0, "2kHz" }, { 5000.0, "5kHz" }, { 10000.0, "10kHz" }
        };
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (ConvoColours::textDim.withAlpha (0.7f));
        for (const auto& r : refs)
            g.drawText (r.txt, juce::Rectangle<float> (fx (r.f) - 20.0f, baseY - 15.0f, 40.0f, 11.0f),
                        juce::Justification::centred);
    }

    if (monoMarkerX >= 0.0f)
    {
        const juce::Line<float> vline (monoMarkerX, zone.getY(), monoMarkerX, zone.getBottom());
        const auto monoCol = ConvoColours::copper;

        // glow follows the same dash pattern as the core, so the halo dips between dots
        // (a tight feather) instead of bridging them into a continuous faint line
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (monoCol.withAlpha (0.15f)); g.drawDashedLine (vline, dashes, 2, 2.4f);
        g.setColour (monoCol.withAlpha (0.20f)); g.drawDashedLine (vline, dashes, 2, 1.6f);
        g.setColour (monoCol.withAlpha (0.9f));  g.drawDashedLine (vline, dashes, 2, 1.0f);   // core dots

        // label the two sides of the crossover along the top: mono to the left, stereo to the right
        g.setFont (captionFont());
        if (monoMarkerX - zone.getX() > 34.0f)
        {
            g.setColour (monoCol.withAlpha (0.9f));
            g.drawText ("mono", juce::Rectangle<float> (zone.getX(), zone.getY() + 2.0f,
                                                        monoMarkerX - zone.getX() - 5.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        if (zone.getRight() - monoMarkerX > 40.0f)
        {
            g.setColour (ConvoColours::mint.withAlpha (0.9f));   // mint = the stereo/wide side
            g.drawText ("stereo", juce::Rectangle<float> (monoMarkerX + 5.0f, zone.getY() + 2.0f,
                                                          zone.getRight() - monoMarkerX - 7.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }
}

// ---------------------------------------------------------------------------
// IR trim handles (draggable Start / End on the waveform)
// ---------------------------------------------------------------------------

float ConvoAudioProcessorEditor::trimFracToX (float frac) const
{
    const auto z = waveZone.toFloat();
    return z.getX() + juce::jlimit (0.0f, 1.0f, frac) * z.getWidth();
}

float ConvoAudioProcessorEditor::trimXToFrac (int x) const
{
    const auto z = waveZone.toFloat();
    if (z.getWidth() <= 0.0f)
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f, ((float) x - z.getX()) / z.getWidth());
}

float ConvoAudioProcessorEditor::liveTrimStart() const
{
    return activeHandle != TrimHandle::none ? dragStartFrac
                                            : processor.getAPVTS().getRawParameterValue ("irStart")->load();
}

float ConvoAudioProcessorEditor::liveTrimEnd() const
{
    return activeHandle != TrimHandle::none ? dragEndFrac
                                            : processor.getAPVTS().getRawParameterValue ("irEnd")->load();
}

ConvoAudioProcessorEditor::TrimHandle ConvoAudioProcessorEditor::trimHandleAt (juce::Point<int> p) const
{
    // only grab while a waveform is shown and the cursor is within the wave zone band
    if (! waveImage.isValid() || ! waveZone.contains (p))
        return TrimHandle::none;

    const float sx = trimFracToX (liveTrimStart());
    const float ex = trimFracToX (liveTrimEnd());

    const float dxStart = std::abs ((float) p.x - sx);
    const float dxEnd   = std::abs ((float) p.x - ex);
    // when the two handles overlap, prefer whichever the cursor is nearer
    if (dxStart <= (float) kTrimHandleHitPx && dxStart <= dxEnd) return TrimHandle::start;
    if (dxEnd   <= (float) kTrimHandleHitPx)                     return TrimHandle::end;
    return TrimHandle::none;
}

// Shade the trimmed-off regions and draw the two handles. Called from paint() over the
// wave; no recompute — just reads the current Start/End params.
void ConvoAudioProcessorEditor::drawTrimHandles (juce::Graphics& g)
{
    if (! waveImage.isValid())
        return;

    const auto  z  = waveZone.toFloat();
    const float sx = trimFracToX (liveTrimStart());   // follows the live drag while dragging
    const float ex = trimFracToX (liveTrimEnd());

    auto drawHandle = [&] (float x, TrimHandle which, bool pointsRight)
    {
        const bool hot = (activeHandle == which) || (activeHandle == TrimHandle::none && hoverHandle == which);
        const auto col = hot ? ConvoColours::mint : ConvoColours::teal.brighter (0.1f);

        g.setColour (col.withAlpha (hot ? 0.95f : 0.8f));
        g.drawLine (x, z.getY(), x, z.getBottom(), hot ? 2.0f : 1.4f);

        // a small grab tab at top so the affordance reads as draggable
        const float tab = 6.0f;
        juce::Path p;
        const float dir = pointsRight ? 1.0f : -1.0f;
        p.startNewSubPath (x, z.getY());
        p.lineTo (x + dir * tab, z.getY() + tab * 0.5f);
        p.lineTo (x, z.getY() + tab);
        p.closeSubPath();
        g.setColour (col);
        g.fillPath (p);
    };

    // Start tab points into the kept region (right); End tab points left
    drawHandle (sx, TrimHandle::start, true);
    drawHandle (ex, TrimHandle::end,   false);
}

void ConvoAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    if (aboutZone.contains (e.getPosition()))   // the "i" hotspot reads as clickable
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        if (hoverHandle != TrimHandle::none) { hoverHandle = TrimHandle::none; repaint (dropZone); }
        return;
    }

    const auto h = trimHandleAt (e.getPosition());
    if (h != hoverHandle)
    {
        hoverHandle = h;
        setMouseCursor (h == TrimHandle::none ? juce::MouseCursor::NormalCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
        repaint (dropZone);
    }
}

void ConvoAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())   // right-click on the IR display: reveal the file / audition
    {
        if (dropZone.contains (e.getPosition()))
            showIRContextMenu();
        return;
    }

    if (aboutZone.contains (e.getPosition()))   // the header "i": about / AGPL legal notice
    {
        showAboutMenu();
        return;
    }

    if (outMeterZone.expanded (6, 4).contains (e.getPosition()))   // the Output fader on the OUT meter
    {
        if (auto* op = processor.getAPVTS().getParameter ("output"))
        {
            if (e.getNumberOfClicks() >= 2)   // double-click -> reset to default (0 dB), like a knob
            {
                op->beginChangeGesture();
                op->setValueNotifyingHost (op->getDefaultValue());
                op->endChangeGesture();
                repaint (outMeterZone.expanded (16, 16));
                return;
            }
            draggingOutput = true;
            op->beginChangeGesture();
            setOutputFromMouseY ((float) e.getPosition().y);
        }
        return;
    }

    if (e.getNumberOfClicks() >= 2 && dropZone.contains (e.getPosition()))   // double-click the display to load an IR
    {
        openFileChooser();
        return;
    }

    activeHandle = trimHandleAt (e.getPosition());
    if (activeHandle != TrimHandle::none)
    {
        // seed the live drag values from the params; nothing commits (no re-bake) until mouse-up,
        // so the drag is just the cheap blur/dim preview
        auto& a = processor.getAPVTS();
        dragStartFrac = a.getRawParameterValue ("irStart")->load();
        dragEndFrac   = a.getRawParameterValue ("irEnd")->load();
        const char* id = activeHandle == TrimHandle::start ? "irStart" : "irEnd";
        if (auto* param = a.getParameter (id))
            param->beginChangeGesture();
        repaint (dropZone);
    }
}

void ConvoAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingOutput)
    {
        setOutputFromMouseY ((float) e.getPosition().y);
        return;
    }

    if (activeHandle == TrimHandle::none)
        return;

    const float frac = trimXToFrac (e.getPosition().x);
    constexpr float minGap = 0.005f;   // handles can't cross — the bake always keeps a region
    if (activeHandle == TrimHandle::start)
        dragStartFrac = juce::jmin (frac, dragEndFrac - minGap);
    else
        dragEndFrac   = juce::jmax (frac, dragStartFrac + minGap);

    repaint (dropZone);   // cheap: re-blit the sharp/blurred regions, no re-bake
}

void ConvoAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingOutput)
    {
        draggingOutput = false;
        if (auto* op = processor.getAPVTS().getParameter ("output")) op->endChangeGesture();
        repaint (outMeterZone.expanded (16, 16));
        return;
    }

    if (activeHandle != TrimHandle::none)
    {
        // commit the dragged value -> the single re-bake (audio kernel only) for this edit
        auto& a = processor.getAPVTS();
        const char* id  = activeHandle == TrimHandle::start ? "irStart" : "irEnd";
        const float val = activeHandle == TrimHandle::start ? dragStartFrac : dragEndFrac;
        if (auto* param = a.getParameter (id))
        {
            param->setValueNotifyingHost (val);   // params are 0..1, so value == fraction
            param->endChangeGesture();
        }
        activeHandle = TrimHandle::none;
        repaint (dropZone);
    }
}

void ConvoAudioProcessorEditor::setOutputFromMouseY (float y)
{
    const auto z = outMeterZone.toFloat();
    const float v = juce::jlimit (0.0f, 1.0f, (z.getBottom() - y) / juce::jmax (1.0f, z.getHeight()));
    if (auto* op = processor.getAPVTS().getParameter ("output"))
        op->setValueNotifyingHost (v);
    repaint (outMeterZone.expanded (16, 16));   // redraw the line + dB readout (and its overflow)
}

void ConvoAudioProcessorEditor::setLedTextColour (juce::Button& b, float amt, juce::Colour off, juce::Colour on)
{
    const auto c = off.interpolatedWith (on, juce::jlimit (0.0f, 1.0f, amt));   // both ids so it shows regardless of toggle
    b.setColour (juce::TextButton::textColourOnId,  c);
    b.setColour (juce::TextButton::textColourOffId, c);
}

void ConvoAudioProcessorEditor::showIRContextMenu()
{
    auto& lib = processor.getIRLibrary();
    const auto file   = lib.getCurrentFile();
    const bool hasFile = file.existsAsFile();
    const bool hasIR   = lib.hasIR();

    juce::PopupMenu m;
    m.addItem (5, "Load IR...", true, false);   // also: double-click the display
    m.addSeparator();
    // a plugin can't drive the host's own browser, so this reveals the file in the OS file explorer
    m.addItem (1, "Reveal IR in file explorer", hasFile, false);
    m.addSeparator();
    m.addItem (2, "Play IR (baked)", hasIR, false);
    m.addItem (3, "Play IR (raw / original)", hasIR, false);
    if (processor.isAuditioning())
    {
        m.addSeparator();
        m.addItem (4, "Stop audition", true, false);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, file] (int r)
        {
            switch (r)
            {
                case 5: openFileChooser();          break;
                case 1: file.revealToUser();        break;
                case 2: processor.startAudition (true);  break;
                case 3: processor.startAudition (false); break;
                case 4: processor.stopAudition();   break;
                default: break;
            }
        });
}

// The header "i": Convo's "Appropriate Legal Notices" (AGPLv3 §0/§5) — copyright, no-warranty,
// the licence, and an offer of the Corresponding Source. A plugin can't drive the host browser
// reliably, but launchInDefaultBrowser is the conventional path for the source/licence links.
void ConvoAudioProcessorEditor::showAboutMenu()
{
    auto upd = convo::updateInfo();
    juce::PopupMenu m;

    if (upd->available.load (std::memory_order_acquire))   // newer release out — copper call-to-action on top
    {
        juce::PopupMenu::Item item;
        item.itemID = 12;
        item.text   = "Update available: v" + upd->latestVersion;
        item.colour = ConvoColours::copper;
        m.addItem (item);
        m.addSeparator();
    }

    m.addSectionHeader ("Convo - convolution audio effect");
    m.addItem (1, juce::String::fromUTF8 ("\xC2\xA9 2026 mvzn"),                false, false);   // © 2026 mvzn
    m.addItem (2, "Free software under the GNU AGPLv3",                         false, false);
    m.addItem (3, "Comes with ABSOLUTELY NO WARRANTY",                         false, false);
    m.addItem (4, "Built with JUCE (juce.com), used under its AGPLv3 option",  false, false);
    m.addSeparator();
    m.addItem (10, "View source code (github.com/mvzn/Convo)");
    m.addItem (11, "View the AGPLv3 licence");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [upd] (int r)
        {
            if      (r == 10) juce::URL ("https://github.com/mvzn/Convo").launchInDefaultBrowser();
            else if (r == 11) juce::URL ("https://www.gnu.org/licenses/agpl-3.0.html").launchInDefaultBrowser();
            else if (r == 12) juce::URL (upd->releaseUrl).launchInDefaultBrowser();
        });
}

// Mode hints, polled at 30 Hz but only touched on a flip:
//  - The X-Over crossover only does anything when Bass Mono is on, so dim it while it's off.
//  - In HP / In LP filter the input by default; when Filter IR is on they're baked into the IR
//    instead, so relabel "In -> IR" and tint them mint (matching the Filter IR tick) to show it.
void ConvoAudioProcessorEditor::updateKnobStates()
{
    auto& a = processor.getAPVTS();

    const bool onIR = a.getRawParameterValue ("filterIR")->load() > 0.5f;
    if (onIR != inFilterLabelsOnIR)
    {
        inFilterLabelsOnIR = onIR;
        const auto col = onIR ? ConvoColours::copper : ConvoColours::label;   // match the Filter IR orange when baked
        inHPLabel.setText (onIR ? "IR HP" : "In HP", juce::dontSendNotification);
        inLPLabel.setText (onIR ? "IR LP" : "In LP", juce::dontSendNotification);
        inHPLabel.setColour (juce::Label::textColourId, col);
        inLPLabel.setColour (juce::Label::textColourId, col);
    }
}

void ConvoAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (backgroundImage.isValid())
        g.drawImage (backgroundImage, getLocalBounds().toFloat());
    else
        g.fillAll (ConvoColours::bg);

    drawChromeText (g);   // static chrome text, live for crispness (clipped to the repaint region)

    const auto clip = g.getClipBounds();

    if (clip.intersects (dropZone))
    {
        if (waveImage.isValid())
        {
            // full IR is shown; the trim selection keeps the kept region sharp and blurs + slightly
            // dims the unselected head/tail (blur is pre-rendered, so this is just region blits)
            const auto  zf = waveZone.toFloat();
            const float sx = trimFracToX (liveTrimStart());
            const float ex = trimFracToX (liveTrimEnd());

            if (waveBlurImage.isValid())
            {
                g.setOpacity (0.6f);
                g.drawImage (waveBlurImage, zf);
                g.setOpacity (1.0f);
            }
            else
            {
                g.drawImage (waveImage, zf);
            }
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (juce::Rectangle<float> (sx, zf.getY(), juce::jmax (0.0f, ex - sx), zf.getHeight())
                                        .getSmallestIntegerContainer());
                if (activeHandle != TrimHandle::none || ! kernelImage.isValid())
                    g.drawImage (waveImage, zf);   // dragging: sharp slice of the full IR (region preview)
                else                               // committed: trimmed+shaped kernel (fade/decay/taper at the handles)
                    g.drawImage (kernelImage, juce::Rectangle<float> (sx, zf.getY(), ex - sx, zf.getHeight()));
            }

            if (bakedLenText.isNotEmpty())
            {
                g.setColour (ConvoColours::textDim);
                g.setFont (juce::Font (juce::FontOptions (11.5f)));
                // sit on the frequency-label row from drawFilterOverlay (top = bottom - 2.5, h = 11),
                // so the length readout lines up with the filter-graph frequencies along the bottom
                g.drawText (bakedLenText,
                            juce::Rectangle<float> ((float) waveZone.getX(), (float) waveZone.getBottom() - 2.5f,
                                                    (float) waveZone.getWidth() - 6.0f, 10.0f),
                            juce::Justification::centredRight);
            }

            drawFilterOverlay (g);   // EQ curve (tone + pre-IR HP/LP) + bass-mono marker
            drawTrimHandles (g);     // Start/End handle lines + tabs (the blur/dim is done above)
        }
        else
        {
            g.setColour (ConvoColours::textDim);
            g.drawText ("Drop .wav / .aiff / .flac / .ogg here  (or double-click to load)",
                        waveZone, juce::Justification::centred);
        }

        if (fileOver)   // drag highlight
        {
            g.setColour (ConvoColours::mint.withAlpha (0.08f));
            g.fillRoundedRectangle (dropZone.toFloat(), 6.0f);
            g.setColour (ConvoColours::mint);
            g.drawRoundedRectangle (dropZone.toFloat().reduced (0.5f), 6.0f, 1.5f);
        }
    }

    if (clip.intersects (inMeterZone))
        drawMeterFill (g, inMeterZone, inMeter, inPeak, inMeterGrad);
    if (clip.intersects (outMeterZone.expanded (16, 16)))
    {
        drawMeterFill (g, outMeterZone, outMeter, outPeak, outMeterGrad);

        // Output control: a draggable horizontal fader line at the output-gain position, with the
        // dB value sitting on top of it (replaces the old Output knob).
        if (auto* op = processor.getAPVTS().getParameter ("output"))
        {
            const auto  z = outMeterZone.toFloat();
            const float y = z.getBottom() - juce::jlimit (0.0f, 1.0f, op->getValue()) * z.getHeight();

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (z.getX() - 3.0f, y + 0.5f, z.getWidth() + 6.0f, 2.0f);    // drop shadow
            g.setColour (draggingOutput ? ConvoColours::mint : ConvoColours::text);
            g.fillRect (z.getX() - 3.0f, y - 1.0f, z.getWidth() + 6.0f, 2.0f);    // the fader line

            auto pill = juce::Rectangle<float> (40.0f, 13.0f).withCentre ({ z.getCentreX(), y - 8.5f });
            g.setColour (ConvoColours::panel.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, 3.0f);
            g.setColour (draggingOutput ? ConvoColours::mint : ConvoColours::text);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (op->getCurrentValueAsText(), pill, juce::Justification::centred);
        }
    }
}

void ConvoAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    headerZone = area.removeFromTop (42);
    // Size each toggle to its own content (label + LED chip) so the gap below actually controls the
    // spacing. drawToggleButton right-justifies the label against the chip, so any extra button width
    // becomes dead space on the LEFT — which is why fixed widths made the gaps look uncontrollable.
    const juce::Font togFont (juce::FontOptions (14.0f));   // matches drawToggleButton's label font
    const int togGap = 8;                                   // <-- spacing between Polarity / Wet Comp / Bypass
    auto placeTog = [&] (juce::ToggleButton& tb)
    {
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (togFont, tb.getButtonText())) + 36;
        tb.setBounds (headerZone.removeFromRight (w).withSizeKeepingCentre (w, 26));
        headerZone.removeFromRight (togGap);
    };
    placeTog (bypassButton);
    placeTog (wetCompButton);
    placeTog (polarityButton);

    // "i" info hotspot, just right of the CONVOLUTION tagline (headerZone is now the left region,
    // the toggles having been removed from the right). Opens the AGPL legal notice / source links.
    {
        auto h = headerZone;
        h.removeFromLeft (92);                                  // past the "Convo" wordmark
        const auto tag  = h.withTrimmedLeft (12);              // where the tagline starts (matches drawChromeText)
        const int  tagW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (captionFont(), "CONVOLUTION"));
        aboutZone = juce::Rectangle<int> (tag.getX() + tagW + 9, headerZone.getCentreY() - 8, 16, 16);
    }

    area.removeFromTop (15);   // 12 px of clear space below the header rule -> matches the graph<->PRE/POST gap

    auto topRow = area.removeFromTop (168);
    auto outCol = topRow.removeFromRight (26);
    topRow.removeFromRight (6);
    auto inCol  = topRow.removeFromRight (26);
    topRow.removeFromRight (12);
    outMeterZone = outCol.withTrimmedBottom (16);   // labels live below the wells
    inMeterZone  = inCol.withTrimmedBottom (16);
    dropZone = topRow;

    // cache the meter fill gradient per zone so paint() never allocates one (see drawMeterFill)
    auto meterGrad = [] (juce::Rectangle<int> z)
    {
        const auto well = z.toFloat().reduced (1.5f);
        juce::ColourGradient grad (ConvoColours::accent, well.getX(), well.getBottom(),
                                   ConvoColours::clip,   well.getX(), well.getY(), false);
        grad.addColour (0.55, ConvoColours::mint);
        grad.addColour (0.85, meterAmber);
        return grad;
    };
    inMeterGrad  = meterGrad (inMeterZone);
    outMeterGrad = meterGrad (outMeterZone);

    auto inner  = dropZone.reduced (10);
    auto header = inner.removeFromTop (26);
    // top header row: [ Play ][ Baked/Raw ]  file name  [ Reverse ][ Norm IR ]  [ Presets ][◀][▶]
    nextPresetButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    prevPresetButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    presetButton.setBounds (header.removeFromRight (74));
    header.removeFromRight (12);
    irNormButton.setBounds (header.removeFromRight (82));    // Reverse + Norm IR inline, left of Presets
    header.removeFromRight (6);
    reverseButton.setBounds (header.removeFromRight (82));
    header.removeFromRight (12);
    playButton.setBounds (header.removeFromLeft (46));       // [ Play | Baked/Raw ] — one segmented pill, flush
    auditionSrcButton.setBounds (header.removeFromLeft (58));
    header.removeFromLeft (10);
    fileNameLabel.setBounds (header);
    inner.removeFromTop (10);   // match the 10 px gap between knob groups
    waveZone = inner;

    area.removeFromTop (10);
    auto row1 = area.removeFromTop (170);
    area.removeFromTop (10);
    auto row2 = area.removeFromTop (170);

    // Both knob rows share a 3 | 4 | 2 column split (group widths proportional to their knob
    // counts), so every column lines up across the two rows.
    auto split342 = [] (juce::Rectangle<int> row, juce::Rectangle<int>& a,
                        juce::Rectangle<int>& b, juce::Rectangle<int>& c)
    {
        const int gap = 10;
        const int usable = row.getWidth() - 2 * gap;
        a = row.removeFromLeft (juce::roundToInt ((float) usable * 3.0f / 9.0f)); row.removeFromLeft (gap);
        b = row.removeFromLeft (juce::roundToInt ((float) usable * 4.0f / 9.0f)); row.removeFromLeft (gap);
        c = row;
    };
    split342 (row1, filterPanel, shapePanel, charPanel);     // FILTER | IR SHAPE | IR CHARACTER
    split342 (row2, duckPanel,   postPanel,  volumePanel);   // DUCKING | POST | VOLUME

    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        // Tight [label | knob | value] stack, vertically centred in the cell so the knob keeps its
        // original position while the label and value box hug it (rather than spreading to the edges).
        // --- nudge these two for finer vertical spacing (px); lower = closer to the knob ---
        const int labelGap = 3;    // gap between the label and the knob
        const int valueGap = 3;    // gap between the knob and the value box
        const int labelH   = 15;   // label band height
        const int textBoxH = 17;   // value box height (must match styleRotary's setTextBoxStyle)

        const int knobBox = juce::jmin (cell.getWidth() - 8,
                                        cell.getHeight() - labelH - labelGap - valueGap - textBoxH);
        const int stackH  = labelH + labelGap + knobBox + valueGap + textBoxH;
        auto stack = cell.withSizeKeepingCentre (cell.getWidth(), stackH);

        l.setBounds (stack.removeFromTop (labelH));
        stack.removeFromTop (labelGap);
        auto col = stack.reduced (4, 0);
        s.setBounds (juce::Rectangle<int> (col.getX(), stack.getY(), col.getWidth(), knobBox + valueGap + textBoxH));
    };

    auto knobArea = [] (juce::Rectangle<int> p)
    {
        auto r = p.reduced (10, 6);
        r.removeFromTop (20);   // caption strip
        return r;
    };

    // evenly split a panel's knob area into n columns and return column i
    auto gcell = [] (juce::Rectangle<int> ka, int n, int i)
    {
        const int cw = ka.getWidth() / n;
        return juce::Rectangle<int> (ka.getX() + i * cw, ka.getY(), cw, ka.getHeight());
    };

    {   // FILTER (3): In HP / In LP / Q
        auto ka = knobArea (filterPanel);
        placeKnob (gcell (ka, 3, 0), inHPSlider,    inHPLabel);
        placeKnob (gcell (ka, 3, 1), inLPSlider,    inLPLabel);
        placeKnob (gcell (ka, 3, 2), filterQSlider, filterQLabel);
    }
    {   // IR SHAPE (4): IR Gain / Fade / Decay / Taper
        auto ka = knobArea (shapePanel);
        placeKnob (gcell (ka, 4, 0), irGainSlider, irGainLabel);
        placeKnob (gcell (ka, 4, 1), fadeInSlider, fadeInLabel);
        placeKnob (gcell (ka, 4, 2), decaySlider,  decayLabel);
        placeKnob (gcell (ka, 4, 3), taperSlider,  taperLabel);
    }
    {   // IR CHARACTER (2): Stretch / Damp
        auto ka = knobArea (charPanel);
        placeKnob (gcell (ka, 2, 0), stretchSlider, stretchLabel);
        placeKnob (gcell (ka, 2, 1), dampSlider,    dampLabel);
    }
    {   // DUCKING (3): Duck / Release / Gate
        auto ka = knobArea (duckPanel);
        placeKnob (gcell (ka, 3, 0), duckSlider,    duckLabel);
        placeKnob (gcell (ka, 3, 1), duckRelSlider, duckRelLabel);
        placeKnob (gcell (ka, 3, 2), gateSlider,    gateLabel);
    }
    {   // POST (4): Bass Mono / Pre-Delay / Tone / Width
        auto ka = knobArea (postPanel);
        placeKnob (gcell (ka, 4, 0), msBassSlider,   msBassLabel);
        placeKnob (gcell (ka, 4, 1), preDelaySlider, preDelayLabel);
        placeKnob (gcell (ka, 4, 2), toneSlider,     toneLabel);
        placeKnob (gcell (ka, 4, 3), widthSlider,    widthLabel);
    }
    {   // VOLUME (2): Dry / Wet
        auto ka = knobArea (volumePanel);
        placeKnob (gcell (ka, 2, 0), drySlider, dryLabel);
        placeKnob (gcell (ka, 2, 1), wetSlider, wetLabel);
    }

    // Filter IR: inline with the FILTER caption, right side
    {
        auto cap = filterPanel.reduced (10, 0).removeFromTop (24).translated(5,3);
        filterIRButton.setBounds (cap.removeFromRight (84).withSizeKeepingCentre (84, 20));
    }

    renderBackground();
    renderWaveImage();
    renderKernelImage();
    renderOverlay();
}

bool ConvoAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.getIRLibrary().isSupported (juce::File (f)))
            return true;
    return false;
}

void ConvoAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileOver = true;
    repaint (dropZone);
}

void ConvoAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    fileOver = false;
    repaint (dropZone);
}

void ConvoAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileOver = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (processor.getIRLibrary().isSupported (file))
        {
            loadFile (file);
            break;
        }
    }
    repaint (dropZone);
}

void ConvoAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    renderWaveImage();
    repaint (dropZone);
}

void ConvoAudioProcessorEditor::timerCallback()
{
    // the update check completes asynchronously; repaint the "i" once when it flips available
    if (! updateNoticeSeen && convo::updateInfo()->available.load (std::memory_order_acquire))
    {
        updateNoticeSeen = true;
        repaint (aboutZone.expanded (2));
    }

    inMeter  = processor.getInputLevel();
    outMeter = processor.getOutputLevel();
    inPeak   = juce::jmax (inMeter,  inPeak  * 0.96f);   // peak line falls slower than the bar
    outPeak  = juce::jmax (outMeter, outPeak * 0.96f);
    duckGR   = processor.getDuckGainReduction();

    // reflect the live audition state on the Play button (it auto-stops at the IR's end)
    const bool auditioning = processor.isAuditioning();
    if (auditioning != playShown)
    {
        playShown = auditioning;
        playButton.setButtonText (auditioning ? "Stop" : "Play");
    }
    // smooth text-colour crossfades: Play follows playback; Reverse / Norm IR follow their toggle
    // state (grey -> mint); Baked/Raw crossfades mint (Raw) -> copper (Baked)
    auto animText = [this] (juce::Button& bn, float& cur, float target, juce::Colour off, juce::Colour on)
    {
        if (std::abs (cur - target) > 0.0015f)
        {
            cur += (target - cur) * 0.3f;
            if (std::abs (cur - target) < 0.02f) cur = target;
            setLedTextColour (bn, cur, off, on);
            bn.repaint();
        }
    };
    // Play follows playback; while playing, the whole segmented pill's border lights up (both halves)
    {
        const float target = auditioning ? 1.0f : 0.0f;
        if (std::abs (playLit - target) > 0.0015f)
        {
            playLit += (target - playLit) * 0.3f;
            if (std::abs (playLit - target) < 0.02f) playLit = target;
            setLedTextColour (playButton, playLit, ConvoColours::label, ConvoColours::mint);
            playButton.getProperties().set        ("rimLit", (double) playLit);
            auditionSrcButton.getProperties().set ("rimLit", (double) playLit);
            playButton.repaint();
            auditionSrcButton.repaint();
        }
    }
    animText (reverseButton,     reverseLit, reverseButton.getToggleState() ? 1.0f : 0.0f,       ConvoColours::label, ConvoColours::mint);
    animText (irNormButton,      normLit,    irNormButton.getToggleState()  ? 1.0f : 0.0f,       ConvoColours::label, ConvoColours::mint);
    animText (auditionSrcButton, bakedBlend, auditionSrcButton.getToggleState() ? 1.0f : 0.0f,   ConvoColours::mint,  ConvoColours::copper);

    const auto name = processor.getIRLibrary().getDisplayName();
    if (name != lastFileName)
    {
        lastFileName = name;
        fileNameLabel.setText (name, juce::dontSendNotification);
    }

    if (processor.getBakeGeneration() != lastBakeGen)
        rebuildThumbnail();

    updateKnobStates();   // light the Bass Mono indicator when engaged; relabel In/IR filters

    // the EQ overlay tracks tone + pre-IR HP/LP + bass-mono, which are not bake params,
    // so poll them and repaint the wave layer only when one actually moves
    auto& apvts = processor.getAPVTS();
    const float ovTone = apvts.getRawParameterValue ("tone")->load();
    const float ovHp   = apvts.getRawParameterValue ("inHP")->load();
    const float ovLp   = apvts.getRawParameterValue ("inLP")->load();
    const float ovQ    = apvts.getRawParameterValue ("filterQ")->load();
    const float ovBass = apvts.getRawParameterValue ("msBass")->load();
    if (! juce::approximatelyEqual (ovTone, eqToneSeen) || ! juce::approximatelyEqual (ovHp, eqHpSeen)
        || ! juce::approximatelyEqual (ovLp, eqLpSeen) || ! juce::approximatelyEqual (ovQ, eqQSeen)
        || ! juce::approximatelyEqual (ovBass, eqBassSeen))
    {
        eqToneSeen = ovTone; eqHpSeen = ovHp; eqLpSeen = ovLp; eqQSeen = ovQ; eqBassSeen = ovBass;
        renderOverlay();        // rebuild the cached curve once per change, not in paint
        repaint (dropZone);
    }

    // IR Gain scales the displayed waveform amplitude (it's the wet trim, not a bake param),
    // so re-render the wave + kernel images when it moves — keyed off the visual gain so the
    // threshold is meaningful across the dB range
    const float ovIrGain = irGainVisualGain();
    if (std::abs (ovIrGain - irGainSeen) > 0.002f)
    {
        irGainSeen = ovIrGain;
        renderWaveImage();
        renderKernelImage();
        repaint (dropZone);
    }

    // trim handles track Start/End (bake params): the bake re-windows via the processor's
    // debounce, but move the handle graphics immediately when the params shift from any
    // source (host automation, preset load, another editor) so the display stays in sync
    const float ovStart = apvts.getRawParameterValue ("irStart")->load();
    const float ovEnd   = apvts.getRawParameterValue ("irEnd")->load();
    if (! juce::approximatelyEqual (ovStart, trimStartSeen) || ! juce::approximatelyEqual (ovEnd, trimEndSeen))
    {
        trimStartSeen = ovStart; trimEndSeen = ovEnd;
        repaint (dropZone);
    }

    // repaint a meter only when it visibly moved — an idle editor paints nothing
    auto moved = [] (float a, float b) { return std::abs (a - b) > 0.003f; };
    if (moved (inMeter, inShown) || moved (inPeak, inPeakShown))
    {
        inShown = inMeter; inPeakShown = inPeak;
        repaint (inMeterZone);
    }
    // OUT meter + the Output fader overlay; also repaint when the output gain moves (host automation)
    const float outGain = processor.getAPVTS().getParameter ("output") != nullptr
                        ? processor.getAPVTS().getParameter ("output")->getValue() : 0.0f;
    if (moved (outMeter, outShown) || moved (outPeak, outPeakShown) || std::abs (outGain - outGainShown) > 0.0008f)
    {
        outShown = outMeter; outPeakShown = outPeak; outGainShown = outGain;
        repaint (outMeterZone.expanded (16, 16));
    }
    // ducking GR probe: hand the live reduction to the Duck knob's LookAndFeel and repaint only when it moves
    if (moved (duckGR, duckGRShown))
    {
        duckGRShown = duckGR;
        duckSlider.getProperties().set ("gr", duckGR);
        duckSlider.repaint();
    }
    // gate activity: grey the Gate knob's arc when idle, mint when the gate is cutting
    const float gateAct = processor.getGateActivity();
    if (moved (gateAct, gateActShown))
    {
        gateActShown = gateAct;
        gateSlider.getProperties().set ("gateAct", gateAct);
        gateSlider.repaint();
    }
}

void ConvoAudioProcessorEditor::loadFile (const juce::File& file)
{
    if (processor.loadIRFile (file))
    {
        lastFileName = processor.getIRLibrary().getDisplayName();
        fileNameLabel.setText (lastFileName, juce::dontSendNotification);
        rebuildThumbnail();
    }
    else
    {
        // the previous IR (if any) is still loaded and audible — say so
        auto msg = "Failed to load: " + file.getFileName();
        if (processor.getIRLibrary().hasIR())
            msg << "  (still using " << processor.getIRLibrary().getCurrentFile().getFileName() << ")";
        fileNameLabel.setText (msg, juce::dontSendNotification);
        // match the timer's comparison value so it doesn't instantly repaint the
        // label back to the (unchanged) current IR name
        lastFileName = processor.getIRLibrary().getDisplayName();
    }
}

void ConvoAudioProcessorEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Select an impulse response",
                                                   juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.ogg;*.flac");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto result = fc.getResult();
        if (result.existsAsFile())
            loadFile (result);
    });
}

// ---------------------------------------------------------------------------
// presets — APVTS state snapshots in Documents/Convo/Presets/*.xml
// ---------------------------------------------------------------------------

void ConvoAudioProcessorEditor::loadPresetFile (const juce::File& file)
{
    if (processor.loadPreset (file))
    {
        currentPresetFile = file;
        // the IR (if any) was reloaded inside loadPreset; refresh the display
        lastFileName = processor.getIRLibrary().getDisplayName();
        fileNameLabel.setText (lastFileName, juce::dontSendNotification);
        rebuildThumbnail();
    }
}

void ConvoAudioProcessorEditor::stepPreset (int direction)
{
    const auto files = processor.getPresetFiles();
    if (files.isEmpty())
        return;

    int index = files.indexOf (currentPresetFile);
    if (index < 0)
        index = direction > 0 ? -1 : 0;   // first ▶ lands on [0]; first ◀ wraps to the end

    const int n = files.size();
    index = ((index + direction) % n + n) % n;   // wrap both ends
    loadPresetFile (files.getReference (index));
}

void ConvoAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Save current as preset...");
    menu.addSeparator();

    const auto files = processor.getPresetFiles();
    if (files.isEmpty())
    {
        menu.addItem (2, "(no presets saved)", false, false);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
        {
            const auto& f = files.getReference (i);
            menu.addItem (100 + i, f.getFileNameWithoutExtension(),
                          true, f == currentPresetFile);   // tick the active preset
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
        [this, files] (int result)
        {
            if (result == 1)
                promptSavePreset();
            else if (result >= 100 && result - 100 < files.size())
                loadPresetFile (files.getReference (result - 100));
        });
}

void ConvoAudioProcessorEditor::promptSavePreset()
{
    savePresetWindow = std::make_unique<juce::AlertWindow> (
        "Save Preset", "Name this preset:", juce::MessageBoxIconType::NoIcon);
    savePresetWindow->addTextEditor ("name", "My Preset");
    savePresetWindow->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    savePresetWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    savePresetWindow->enterModalState (true,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1)
            {
                const auto name = savePresetWindow->getTextEditorContents ("name");
                if (processor.savePreset (name))
                    currentPresetFile = processor.getPresetsFolder()
                                            .getChildFile (juce::File::createLegalFileName (name).trim() + ".xml");
            }
            savePresetWindow.reset();
        }), false);
}
