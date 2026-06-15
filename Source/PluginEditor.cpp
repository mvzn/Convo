#include "PluginEditor.h"

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

    const juce::Colour meterAmber { 0xffd9a13b };
}

ConvoAudioProcessorEditor::ConvoAudioProcessorEditor (ConvoAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);                       // paint() covers everything — skip host compositing

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
    setup (outputSlider,   outputLabel,   "Output");
    setup (toneSlider,     toneLabel,     "Tone");
    setup (inHPSlider,     inHPLabel,     "In HP");
    setup (inLPSlider,     inLPLabel,     "In LP");
    setup (preDelaySlider, preDelayLabel, "Pre-Delay");
    setup (widthSlider,    widthLabel,    "Width");
    setup (msBassSlider,   msBassLabel,   "Bass Mono");
    setup (duckSlider,     duckLabel,     "Duck");
    setup (duckRelSlider,  duckRelLabel,  "Release");
    setup (fadeInSlider,   fadeInLabel,   "Fade In");
    setup (decaySlider,    decayLabel,    "Decay");
    setup (taperSlider,    taperLabel,    "Taper");

    reverseButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);    // green = active
    clipGuardButton.setColour (juce::ToggleButton::tickColourId, ConvoColours::mint);
    wetCompButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);
    msButton.setColour        (juce::ToggleButton::tickColourId, ConvoColours::mint);
    filterIRButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::mint);
    rawLevelButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::copper);  // copper = "careful"
    bypassButton.setColour    (juce::ToggleButton::tickColourId, ConvoColours::copper);
    rawLevelButton.setTooltip ("Use the IR's recorded level unscaled "
                               "(audio can convolve 30..45 dB hot)");
    wetCompButton.setTooltip  ("Adaptive wet gain compensation: tracks the dry input level "
                               "and trims the wet to match; frozen while the input is quiet "
                               "so tails ring out");
    inHPSlider.setTooltip ("Pre-IR high-pass (low cut), 6 dB/oct, on the signal feeding the IR");
    inLPSlider.setTooltip ("Pre-IR low-pass (high cut), 6 dB/oct, on the signal feeding the IR");
    fadeInSlider.setTooltip ("Raised-cosine fade-in baked into the IR; the ramp is capped at 80% of the IR length");
    msButton.setTooltip   ("Mid/Side: convolve mid-with-mid and side-with-side (re-bakes the "
                           "IR as M/S). Wants a stereo IR (a mono IR collapses to input to mono)");
    msBassSlider.setTooltip ("Bass Mono (Mid/Side only): high-passes the side so content below "
                             "the cutoff collapses to mono. 20 Hz = off");
    filterIRButton.setTooltip ("Apply the In HP/In LP filter to the IR (baked, shown in the "
                               "display) instead of the input. Same sound; IR mode is cheaper "
                               "at runtime but re-bakes when you move the cutoffs");

    // --- mix / output ---
    drySlider.setTooltip    ("Level of the unprocessed (dry) signal in the mix");
    wetSlider.setTooltip    ("Level of the convolved (wet) signal in the mix");
    irGainSlider.setTooltip ("Gain of the impulse response itself (scales the wet convolution.) "
                             "Separate from Wet (the wet mix level)");
    outputSlider.setTooltip ("Final output trim, applied after the dry/wet mix");

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
    decaySlider.setTooltip   ("Imposes an exponential decay on the IR (-60 dB at this time) and truncates "
                              "the tail. Fully clockwise = Off (tail as recorded)");
    taperSlider.setTooltip   ("Raised-cosine fade-out baked onto the IR's tail to de-click the kernel end");

    // --- output guards / global ---
    clipGuardButton.setTooltip ("Soft-clip ceiling on the final output: transparent below -2.5 dBFS, "
                                "catches overs. Defeatable");
    bypassButton.setTooltip    ("Passes the dry input through at unity");
    loadButton.setTooltip      ("Load an impulse response (.wav / .aif / .aiff / .ogg / .flac). "
                                "You can also drag a file onto the display");

    for (auto* b : { &reverseButton, &rawLevelButton, &filterIRButton, &clipGuardButton,
                     &wetCompButton, &msButton, &bypassButton })
    {
        b->setColour (juce::ToggleButton::textColourId, ConvoColours::label);
        addAndMakeVisible (*b);
    }

    dryAtt      = std::make_unique<SliderAttachment> (apvts, "dry",         drySlider);
    wetAtt      = std::make_unique<SliderAttachment> (apvts, "wet",         wetSlider);
    irGainAtt   = std::make_unique<SliderAttachment> (apvts, "irGain",      irGainSlider);
    outputAtt   = std::make_unique<SliderAttachment> (apvts, "output",      outputSlider);
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
    reverseAtt   = std::make_unique<ButtonAttachment> (apvts, "reverse",   reverseButton);
    rawLevelAtt  = std::make_unique<ButtonAttachment> (apvts, "irRaw",     rawLevelButton);
    filterIRAtt  = std::make_unique<ButtonAttachment> (apvts, "filterIR",  filterIRButton);
    clipGuardAtt = std::make_unique<ButtonAttachment> (apvts, "clipGuard", clipGuardButton);
    wetCompAtt   = std::make_unique<ButtonAttachment> (apvts, "wetComp",   wetCompButton);
    msAtt        = std::make_unique<ButtonAttachment> (apvts, "ms",        msButton);
    bypassAtt    = std::make_unique<ButtonAttachment> (apvts, "bypass",    bypassButton);

    // show the unit on each knob's value box (dB / Hz / ms / %). The slider attachment points
    // textFromValueFunction at the parameter's getText, which omits the label, so wrap it to
    // append the parameter's own unit — skipping values that already carry one (e.g. Decay's
    // "Off" / "... ms") so nothing gets doubled.
    struct { juce::Slider* s; const char* id; } unitSliders[] = {
        { &drySlider, "dry" }, { &wetSlider, "wet" }, { &irGainSlider, "irGain" },
        { &outputSlider, "output" }, { &toneSlider, "tone" }, { &inHPSlider, "inHP" },
        { &inLPSlider, "inLP" }, { &preDelaySlider, "preDelay" }, { &widthSlider, "width" },
        { &msBassSlider, "msBass" }, { &duckSlider, "duck" }, { &duckRelSlider, "duckRelease" },
        { &fadeInSlider, "fadeIn" }, { &decaySlider, "decay" }, { &taperSlider, "taper" }
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

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    presetButton.setTooltip     ("Save the current settings as a preset, or pick a saved one");
    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    presetButton.onClick     = [this] { showPresetMenu(); };
    prevPresetButton.onClick = [this] { stepPreset (-1); };
    nextPresetButton.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (presetButton);
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    rebuildThumbnail();

    setSize (900, 610);
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
        bakedLenSeconds = n / sr;
        bakedLenText = juce::String (bakedLenSeconds, 2) + " s";
    }
    else
    {
        thumbnail->clear();
        bakedLenSeconds = 0.0;
        bakedLenText.clear();
    }

    lastBakeGen = processor.getBakeGeneration();
    renderWaveImage();
    repaint (dropZone);
}

// ---------------------------------------------------------------------------
// cached rendering
// ---------------------------------------------------------------------------

void ConvoAudioProcessorEditor::renderWaveImage()
{
    waveImage = juce::Image();
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
    thumbnail->drawChannels (g, local, 0.0, thumbnail->getTotalLength(), 1.0f);
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

    // header: wordmark + dim tagline + hairline rule
    {
        auto h = headerZone;
        g.setColour (ConvoColours::text);
        g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        const auto title = juce::String ("Convo");
        // nudge the wordmark up a few px: the 26 pt bold's large descent makes box-centred
        // text sit visually low next to the small-caps tagline / toggle row
        g.drawText (title, h.removeFromLeft (92).translated (0, -3), juce::Justification::centredLeft);

        g.setColour (ConvoColours::mint.withAlpha (0.85f));
        g.fillRect (h.getX() + 2, headerZone.getCentreY() - 8, 2, 16);

        g.setColour (ConvoColours::textDim);
        g.setFont (captionFont());
        g.drawText ("CONVOLUTION", h.withTrimmedLeft (12), juce::Justification::centredLeft);

        // span the full content width (headerZone itself lost the bypass strip in resized())
        const auto content = getLocalBounds().reduced (14);
        g.setColour (ConvoColours::border.withAlpha (0.8f));
        g.fillRect (content.getX(), headerZone.getBottom() + 3, content.getWidth(), 1);
    }

    auto panel = [&g] (juce::Rectangle<int> r, const juce::String& caption)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 })
            .drawForRectangle (g, r);
        g.setColour (ConvoColours::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (ConvoColours::border);
        g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);

        if (caption.isNotEmpty())
        {
            auto strip = r.reduced (12, 0).removeFromTop (24);
            g.setColour (ConvoColours::mint.withAlpha (0.85f));
            g.fillRect (strip.getX(), strip.getCentreY() - 5, 3, 10);
            g.setColour (ConvoColours::textDim);
            g.setFont (captionFont());
            g.drawText (caption, strip.withTrimmedLeft (10), juce::Justification::centredLeft);
        }
    };

    panel (dropZone,    {});
    panel (prePanel,    "PRE");
    panel (postPanel,   "POST");
    panel (duckPanel,   "DUCKING");
    panel (shapePanel,  "IR SHAPE");

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
    g.setColour (ConvoColours::textDim);
    g.setFont (captionFont());
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

    const float l = juce::jlimit (0.0f, 1.0f, level);
    if (l > 0.001f)
    {
        // gradient (phthalo low, mint mid, amber hot, red top) is cached in resized() so the
        // meter — repainted ~30 Hz throughout playback — never reallocates a ColourGradient
        g.setGradientFill (fillGrad);
        auto fill = well;
        g.fillRoundedRectangle (fill.removeFromBottom (well.getHeight() * l), 2.0f);
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

    if (eqLo == nullptr)   // allocate the reused coefficient objects once
    {
        eqLo = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, 700.0, 0.5, 1.0f);
        eqHi = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 700.0, 0.5, 1.0f);
        eqHp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sr, 20.0);
        eqLp = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass  (sr, 20000.0);
    }
    *eqLo = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (sr, 700.0, 0.5, juce::Decibels::decibelsToGain (-tiltDb));
    *eqHi = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 700.0, 0.5, juce::Decibels::decibelsToGain ( tiltDb));
    *eqHp = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sr, (double) hpHz);
    *eqLp = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass  (sr, (double) lpHz);

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

    if (apvts.getRawParameterValue ("ms")->load() > 0.5f)   // bass-mono marker (Mid/Side only)
    {
        const float bass = apvts.getRawParameterValue ("msBass")->load();
        if (bass > 21.0f)
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

    // frequency reference anchors along the bottom: just the log-decade marks (100 / 1k / 10k)
    // so the filter curve is readable without cluttering the display
    {
        struct { double f; const char* txt; } refs[] = { { 100.0, "100Hz" }, { 1000.0, "1kHz" }, { 10000.0, "10kHz" } };
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (const auto& r : refs)
        {
            const double tt = std::log (r.f / fLo) / std::log (fHi / fLo);
            const float  x  = zone.getX() + (float) tt * zone.getWidth();
            g.setColour (ConvoColours::border.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), zone.getBottom() - 4.0f, zone.getBottom());
            g.setColour (ConvoColours::textDim.withAlpha (0.7f));
            g.drawText (r.txt, juce::Rectangle<float> (x - 16.0f, zone.getBottom() - 15.0f, 32.0f, 11.0f),
                        juce::Justification::centred);
        }
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

ConvoAudioProcessorEditor::TrimHandle ConvoAudioProcessorEditor::trimHandleAt (juce::Point<int> p) const
{
    // only grab while a waveform is shown and the cursor is within the wave zone band
    if (! waveImage.isValid() || ! waveZone.contains (p))
        return TrimHandle::none;

    auto& a = processor.getAPVTS();
    const float sx = trimFracToX (a.getRawParameterValue ("irStart")->load());
    const float ex = trimFracToX (a.getRawParameterValue ("irEnd")->load());

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

    auto& a = processor.getAPVTS();
    const float startFrac = a.getRawParameterValue ("irStart")->load();
    const float endFrac   = a.getRawParameterValue ("irEnd")->load();
    const auto  z  = waveZone.toFloat();
    const float sx = trimFracToX (startFrac);
    const float ex = trimFracToX (endFrac);

    // dim the trimmed-off head (left of Start) and tail (right of End)
    g.setColour (ConvoColours::bg.withAlpha (0.62f));
    if (sx > z.getX() + 0.5f)
        g.fillRect (juce::Rectangle<float> (z.getX(), z.getY(), sx - z.getX(), z.getHeight()));
    if (ex < z.getRight() - 0.5f)
        g.fillRect (juce::Rectangle<float> (ex, z.getY(), z.getRight() - ex, z.getHeight()));

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
    activeHandle = trimHandleAt (e.getPosition());
    if (activeHandle != TrimHandle::none)
    {
        const char* id = activeHandle == TrimHandle::start ? "irStart" : "irEnd";
        if (auto* param = processor.getAPVTS().getParameter (id))
            param->beginChangeGesture();
        repaint (dropZone);
    }
}

void ConvoAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (activeHandle == TrimHandle::none)
        return;

    auto& a = processor.getAPVTS();
    const float frac = trimXToFrac (e.getPosition().x);

    // keep a minimum gap so the two handles can't cross (and the bake always has a region)
    constexpr float minGap = 0.005f;
    if (activeHandle == TrimHandle::start)
    {
        const float endFrac = a.getRawParameterValue ("irEnd")->load();
        if (auto* p = a.getParameter ("irStart"))
            p->setValueNotifyingHost (juce::jmin (frac, endFrac - minGap));
    }
    else
    {
        const float startFrac = a.getRawParameterValue ("irStart")->load();
        if (auto* p = a.getParameter ("irEnd"))
            p->setValueNotifyingHost (juce::jmax (frac, startFrac + minGap));
    }
    // params are 0..1 already, so the normalised value == the fraction; repaint the band
    repaint (dropZone);
}

void ConvoAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (activeHandle != TrimHandle::none)
    {
        const char* id = activeHandle == TrimHandle::start ? "irStart" : "irEnd";
        if (auto* param = processor.getAPVTS().getParameter (id))
            param->endChangeGesture();
        activeHandle = TrimHandle::none;
        repaint (dropZone);
    }
}

// Mode hints, polled at 30 Hz but only touched on a flip:
//  - Bass Mono only does anything in Mid/Side mode, so dim it (interactive) while M/S is off.
//  - In HP / In LP filter the input by default; when Filter IR is on they're baked into the IR
//    instead, so relabel "In -> IR" and tint them mint (matching the Filter IR tick) to show it.
void ConvoAudioProcessorEditor::updateKnobStates()
{
    auto& a = processor.getAPVTS();

    const bool msOn = a.getRawParameterValue ("ms")->load() > 0.5f;
    const float t = msOn ? 1.0f : 0.45f;
    if (! juce::approximatelyEqual (msBassSlider.getAlpha(), t))
    {
        msBassSlider.setAlpha (t);
        msBassLabel.setAlpha (t);
    }

    const bool onIR = a.getRawParameterValue ("filterIR")->load() > 0.5f;
    if (onIR != inFilterLabelsOnIR)
    {
        inFilterLabelsOnIR = onIR;
        const auto col = onIR ? ConvoColours::mint : ConvoColours::label;
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

    const auto clip = g.getClipBounds();

    if (clip.intersects (dropZone))
    {
        if (waveImage.isValid())
        {
            g.drawImage (waveImage, waveZone.toFloat());

            if (bakedLenText.isNotEmpty())
            {
                g.setColour (ConvoColours::textDim);
                g.setFont (juce::Font (juce::FontOptions (11.5f)));
                g.drawText (bakedLenText, waveZone.reduced (6, 4), juce::Justification::bottomRight);
            }

            drawFilterOverlay (g);   // EQ curve (tone + pre-IR HP/LP) + bass-mono marker
            drawTrimHandles (g);     // dim the trimmed-off head/tail + draggable Start/End handles
        }
        else
        {
            g.setColour (ConvoColours::textDim);
            g.drawText ("Drop .wav / .aiff / .flac / .ogg here  (or click Load IR)",
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
    if (clip.intersects (outMeterZone))
        drawMeterFill (g, outMeterZone, outMeter, outPeak, outMeterGrad);
}

void ConvoAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    headerZone = area.removeFromTop (42);
    bypassButton.setBounds (headerZone.removeFromRight (92).withSizeKeepingCentre (92, 26));
    headerZone.removeFromRight (8);
    clipGuardButton.setBounds (headerZone.removeFromRight (112).withSizeKeepingCentre (112, 26));
    headerZone.removeFromRight (8);
    wetCompButton.setBounds (headerZone.removeFromRight (112).withSizeKeepingCentre (112, 26));
    area.removeFromTop (8);                                   // Mid/Side now lives in IR SHAPE (it's a bake param)

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
    // header row: [ file name .......... ] [Presets] [◀][▶]  [Load IR...]
    loadButton.setBounds (header.removeFromRight (92));
    header.removeFromRight (10);
    nextPresetButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    prevPresetButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    presetButton.setBounds (header.removeFromRight (74));
    header.removeFromRight (8);
    fileNameLabel.setBounds (header);
    inner.removeFromTop (6);
    waveZone = inner;

    area.removeFromTop (12);
    auto row1 = area.removeFromTop (170);
    prePanel  = row1.removeFromLeft (290);   // PRE: pre-convolution filters (In HP / In LP / Bass Mono)
    row1.removeFromLeft (10);
    postPanel = row1;                         // POST: post-conv shaping + mix
    area.removeFromTop (10);
    auto row2 = area.removeFromTop (170);
    duckPanel = row2.removeFromLeft (188);   // smaller — just the two ducking knobs
    row2.removeFromLeft (10);
    shapePanel = row2;                        // IR SHAPE gets the rest (bake controls + IR Gain)

    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        l.setBounds (cell.removeFromTop (15));
        s.setBounds (cell.reduced (4, 0));
    };

    auto knobArea = [] (juce::Rectangle<int> p)
    {
        auto r = p.reduced (10, 6);
        r.removeFromTop (20);   // caption strip
        return r;
    };

    {   // PRE — pre-convolution filters that shape what feeds the IR
        auto row = knobArea (prePanel);
        const int cellW = row.getWidth() / 3;
        placeKnob (row.removeFromLeft (cellW), inHPSlider,   inHPLabel);
        placeKnob (row.removeFromLeft (cellW), inLPSlider,   inLPLabel);
        placeKnob (row.removeFromLeft (cellW), msBassSlider, msBassLabel);
    }
    {   // POST — post-convolution shaping + final mix
        auto row = knobArea (postPanel);
        const int cellW = row.getWidth() / 6;
        placeKnob (row.removeFromLeft (cellW), toneSlider,     toneLabel);
        placeKnob (row.removeFromLeft (cellW), widthSlider,    widthLabel);
        placeKnob (row.removeFromLeft (cellW), preDelaySlider, preDelayLabel);
        placeKnob (row.removeFromLeft (cellW), drySlider,      dryLabel);
        placeKnob (row.removeFromLeft (cellW), wetSlider,      wetLabel);
        placeKnob (row.removeFromLeft (cellW), outputSlider,   outputLabel);
    }
    {
        auto row = knobArea (duckPanel);
        const int cellW = row.getWidth() / 2;
        placeKnob (row.removeFromLeft (cellW), duckSlider,    duckLabel);
        placeKnob (row.removeFromLeft (cellW), duckRelSlider, duckRelLabel);
    }
    {   // IR SHAPE — everything that changes the IR bake, plus IR Gain
        auto row = knobArea (shapePanel);
        const int cellW = row.getWidth() / 5;     // 4 knobs + a toggle column
        placeKnob (row.removeFromLeft (cellW), irGainSlider, irGainLabel);
        placeKnob (row.removeFromLeft (cellW), fadeInSlider, fadeInLabel);
        placeKnob (row.removeFromLeft (cellW), decaySlider,  decayLabel);
        placeKnob (row.removeFromLeft (cellW), taperSlider,  taperLabel);
        auto cell = row.removeFromLeft (cellW);
        auto toggles = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 6, 104), 28 * 4 + 6 * 3);
        reverseButton.setBounds  (toggles.removeFromTop (28));
        toggles.removeFromTop (6);
        rawLevelButton.setBounds (toggles.removeFromTop (28));
        toggles.removeFromTop (6);
        filterIRButton.setBounds (toggles.removeFromTop (28));
        toggles.removeFromTop (6);
        msButton.setBounds       (toggles.removeFromTop (28));
    }

    renderBackground();
    renderWaveImage();
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
    inMeter  = processor.getInputLevel();
    outMeter = processor.getOutputLevel();
    inPeak   = juce::jmax (inMeter,  inPeak  * 0.96f);   // peak line falls slower than the bar
    outPeak  = juce::jmax (outMeter, outPeak * 0.96f);

    const auto name = processor.getIRLibrary().getDisplayName();
    if (name != lastFileName)
    {
        lastFileName = name;
        fileNameLabel.setText (name, juce::dontSendNotification);
    }

    if (processor.getBakeGeneration() != lastBakeGen)
        rebuildThumbnail();

    updateKnobStates();   // dim Bass Mono while Mid/Side is off (its only no-op state)

    // the EQ overlay tracks tone + pre-IR HP/LP + bass-mono, which are not bake params,
    // so poll them and repaint the wave layer only when one actually moves
    auto& apvts = processor.getAPVTS();
    const float ovTone = apvts.getRawParameterValue ("tone")->load();
    const float ovHp   = apvts.getRawParameterValue ("inHP")->load();
    const float ovLp   = apvts.getRawParameterValue ("inLP")->load();
    const float ovBass = apvts.getRawParameterValue ("msBass")->load();
    const bool  ovMs   = apvts.getRawParameterValue ("ms")->load() > 0.5f;
    if (! juce::approximatelyEqual (ovTone, eqToneSeen) || ! juce::approximatelyEqual (ovHp, eqHpSeen)
        || ! juce::approximatelyEqual (ovLp, eqLpSeen) || ! juce::approximatelyEqual (ovBass, eqBassSeen)
        || ovMs != eqMsSeen)
    {
        eqToneSeen = ovTone; eqHpSeen = ovHp; eqLpSeen = ovLp; eqBassSeen = ovBass; eqMsSeen = ovMs;
        renderOverlay();        // rebuild the cached curve once per change, not in paint
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
    if (moved (outMeter, outShown) || moved (outPeak, outPeakShown))
    {
        outShown = outMeter; outPeakShown = outPeak;
        repaint (outMeterZone);
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
