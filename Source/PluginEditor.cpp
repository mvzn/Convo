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
    setup (stretchSlider,  stretchLabel,  "Stretch");
    setup (dampSlider,     dampLabel,     "Damp");

    reverseButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);    // green = active
    wetCompButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);
    msButton.setColour        (juce::ToggleButton::tickColourId, ConvoColours::mint);
    filterIRButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::mint);
    irNormButton.setColour    (juce::ToggleButton::tickColourId, ConvoColours::mint);
    bypassButton.setColour    (juce::ToggleButton::tickColourId, ConvoColours::copper);
    irNormButton.setTooltip   ("Normalize the IR to unit energy. Off (default) = the IR's raw "
                               "recorded level, which can convolve hot on dense material");
    wetCompButton.setTooltip  ("Adaptive wet gain compensation: tracks the dry input level "
                               "and trims the wet to match; frozen while the input is quiet "
                               "so tails ring out");
    inHPSlider.setTooltip ("Pre-IR high-pass (low cut), 6 dB/oct, on the signal feeding the IR");
    inLPSlider.setTooltip ("Pre-IR low-pass (high cut), 6 dB/oct, on the signal feeding the IR");
    fadeInSlider.setTooltip ("Raised-cosine fade-in baked into the IR; the ramp is capped at 80% of the IR length");
    msButton.setTooltip   ("Bass Mono on/off (the LED on the knob): fold the wet below the crossover "
                           "to mono, keeping everything above it stereo. Stereo IRs only");
    msBassSlider.setTooltip ("Bass Mono crossover: the wet collapses to mono below this frequency "
                             "and stays stereo above it (6 dB/oct, cleanest phase). 20 Hz = off");
    filterIRButton.setTooltip ("Apply the In HP/In LP filter to the IR (baked, shown in the "
                               "display) instead of the input. Same sound; IR mode is cheaper "
                               "at runtime but re-bakes when you move the cutoffs");

    // --- mix / output ---
    drySlider.setTooltip    ("Level of the unprocessed (dry) signal in the mix");
    wetSlider.setTooltip    ("Level of the convolved (wet) signal in the mix");
    irGainSlider.setTooltip ("Gain of the impulse response itself (scales the wet convolution, "
                             "and the waveform height above). Separate from Wet (the wet mix level)");
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
    stretchSlider.setTooltip ("Time-stretch the IR (resampled in the bake): below 100% shortens it, "
                              "above 100% lengthens it. 100% = off");
    dampSlider.setTooltip    ("Damping (baked): a progressive high-frequency rolloff over the tail "
                              "(air absorption) \xe2\x80\x94 the reverb gets darker as it decays. 0% = off");

    // --- output guards / global ---
    bypassButton.setTooltip    ("Passes the dry input through at unity");
    loadButton.setTooltip      ("Load an impulse response (.wav / .aif / .aiff / .ogg / .flac). "
                                "You can also drag a file onto the display");

    for (auto* b : { &reverseButton, &irNormButton, &filterIRButton,
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
    stretchAtt  = std::make_unique<SliderAttachment> (apvts, "stretch",     stretchSlider);
    dampAtt     = std::make_unique<SliderAttachment> (apvts, "damp",        dampSlider);
    reverseAtt   = std::make_unique<ButtonAttachment> (apvts, "reverse",   reverseButton);
    irNormAtt    = std::make_unique<ButtonAttachment> (apvts, "irNorm",    irNormButton);
    filterIRAtt  = std::make_unique<ButtonAttachment> (apvts, "filterIR",  filterIRButton);
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
        { &fadeInSlider, "fadeIn" }, { &decaySlider, "decay" }, { &taperSlider, "taper" },
        { &stretchSlider, "stretch" }
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

    // header: wordmark + dim tagline + hairline rule
    {
        auto h = headerZone;
        g.setColour (ConvoColours::text);
        g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        const auto title = juce::String ("Convo");
        // nudge the wordmark up a few px: the 26 pt bold's large descent makes box-centred
        // text sit visually low next to the small-caps tagline / toggle row
        g.drawText (title, h.removeFromLeft (92).translated (0, -2), juce::Justification::centredLeft);

        g.setColour (ConvoColours::mint.withAlpha (0.85f));
        g.fillRect (h.getX() + 2, headerZone.getCentreY() - 8, 2, 16);

        g.setColour (ConvoColours::textDim);
        g.setFont (captionFont());
        g.drawText ("CONVOLUTION", h.withTrimmedLeft (12), juce::Justification::centredLeft);

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

    // bass-mono crossover marker — shown whenever the feature is on
    if (apvts.getRawParameterValue ("ms")->load() > 0.5f)
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

    // frequency grid along the bottom: minor log ticks (the 2..9 multiples between decades) plus
    // labelled decades (100 / 1k / 10k). The whole grid sits below the wave by freqMarkY.
    {
        const float freqMarkY = 15.0f;   // <-- vertical nudge of the freq ticks + labels (px, + = down)
        const float baseY = zone.getBottom() + freqMarkY;
        auto fx = [&] (double f) { return zone.getX() + (float) (std::log (f / fLo) / std::log (fHi / fLo)) * zone.getWidth(); };

        // minor ticks: 2..9 x each decade (30..90, 200..900, 2k..9k) — short, faint
        g.setColour (ConvoColours::border.withAlpha (0.35f));
        for (double dec : { 10.0, 100.0, 1000.0 })
            for (int m = 2; m <= 9; ++m)
            {
                const double f = dec * (double) m;
                if (f > fLo && f < fHi)
                    g.drawVerticalLine (juce::roundToInt (fx (f)), baseY - 2.0f, baseY);
            }

        // labelled decades: a slightly longer tick + text above it
        struct { double f; const char* txt; } refs[] = { { 100.0, "100Hz" }, { 1000.0, "1kHz" }, { 10000.0, "10kHz" } };
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (const auto& r : refs)
        {
            const float x = fx (r.f);
            g.setColour (ConvoColours::border.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (x), baseY - 4.0f, baseY);
            g.setColour (ConvoColours::textDim.withAlpha (0.7f));
            g.drawText (r.txt, juce::Rectangle<float> (x - 16.0f, baseY - 15.0f, 32.0f, 11.0f),
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

// Mode hints, polled at 30 Hz but only touched on a flip:
//  - The X-Over crossover only does anything when Bass Mono is on, so dim it while it's off.
//  - In HP / In LP filter the input by default; when Filter IR is on they're baked into the IR
//    instead, so relabel "In -> IR" and tint them mint (matching the Filter IR tick) to show it.
void ConvoAudioProcessorEditor::updateKnobStates()
{
    auto& a = processor.getAPVTS();

    // Bass Mono works on any IR (the wet's stereo comes from the input), so it's always
    // available — just dim its X-Over knob while the mode is off (its only no-op state).
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
                g.drawText (bakedLenText, waveZone.reduced (6, 4), juce::Justification::bottomRight);
            }

            drawFilterOverlay (g);   // EQ curve (tone + pre-IR HP/LP) + bass-mono marker
            drawTrimHandles (g);     // Start/End handle lines + tabs (the blur/dim is done above)
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
    wetCompButton.setBounds (headerZone.removeFromRight (112).withSizeKeepingCentre (112, 26));
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
    inner.removeFromTop (10);   // match the 10 px gap between knob groups
    waveZone = inner;

    area.removeFromTop (10);
    auto row1 = area.removeFromTop (170);
    // Top row: FILTER (In HP / In LP) | IR SHAPE (4 knobs) | IR CHARACTER (Stretch / Damp +
    // toggles). FILTER is its own 2-knob group on the left (DUCKING below it shares its width);
    // IR SHAPE + IR CHARACTER split the rest 4 + 3, lining up with POST | VOLUME on the row below.
    filterPanel = row1.removeFromLeft (juce::roundToInt ((row1.getWidth() - 10) * 2.0f / 9.0f));
    row1.removeFromLeft (10);
    auto postArea = row1;                       // IR SHAPE + IR CHARACTER split this span (4 + 3)
    area.removeFromTop (10);
    auto row2 = area.removeFromTop (170);
    duckPanel = row2.removeFromLeft (filterPanel.getWidth());   // DUCKING sits under FILTER
    row2.removeFromLeft (10);
    // Bottom row post-area: POST (4 knobs) | VOLUME (3 knobs), split 4 + 3 (same as IR SHAPE | IR
    // CHARACTER above, so every column lines up across the two rows).
    {
        const int gap    = 10;
        const int usable = row2.getWidth() - gap;
        const int volW   = juce::roundToInt ((3.0f * (float) usable + 20.0f) / 7.0f);
        postPanel   = row2.withWidth (usable - volW);
        volumePanel = row2.withTrimmedLeft ((usable - volW) + gap);
    }

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

    // Top row post-area: IR SHAPE (4 knobs) | IR CHARACTER (3 slots), split 4 + 3 (equal cell
    // widths, same split as POST | VOLUME below so every column lines up across the two rows).
    {
        const int gap    = 10;
        const int usable = postArea.getWidth() - gap;
        const int charW  = juce::roundToInt ((3.0f * (float) usable + 20.0f) / 7.0f);
        shapePanel  = postArea.withWidth (usable - charW);
        charPanel   = postArea.withTrimmedLeft ((usable - charW) + gap);
    }
    auto postKA = knobArea (postPanel);     // 4-column grid (POST + IR SHAPE share these columns)
    auto volKA  = knobArea (volumePanel);   // 3-column grid (VOLUME + IR CHARACTER)
    const int cwP = postKA.getWidth() / 4;
    const int cwV = volKA.getWidth()  / 3;
    // place at a POST / VOLUME column, taking the given row's y so the IR SHAPE row lines up
    auto pcolP = [&] (int i, juce::Rectangle<int> r) { return juce::Rectangle<int> (postKA.getX() + i * cwP, r.getY(), cwP, r.getHeight()); };
    auto pcolV = [&] (int j, juce::Rectangle<int> r) { return juce::Rectangle<int> (volKA.getX()  + j * cwV, r.getY(), cwV, r.getHeight()); };

    {   // FILTER — pre-IR input filters
        auto row = knobArea (filterPanel);
        const int cellW = row.getWidth() / 2;
        placeKnob (row.removeFromLeft (cellW), inHPSlider, inHPLabel);
        placeKnob (row.removeFromLeft (cellW), inLPSlider, inLPLabel);
    }
    {   // POST: Bass Mono / Pre-Delay / Tone / Width
        placeKnob (pcolP (0, postKA), msBassSlider,   msBassLabel);   // Bass Mono
        placeKnob (pcolP (1, postKA), preDelaySlider, preDelayLabel);
        placeKnob (pcolP (2, postKA), toneSlider,     toneLabel);
        placeKnob (pcolP (3, postKA), widthSlider,    widthLabel);
        // Bass Mono enable: a small square LED toggle sitting just under its knob
        auto kb = msBassSlider.getBounds();
        msButton.setBounds (juce::Rectangle<int> (18, 16).withCentre ({ kb.getCentreX(), kb.getCentreY() + 30 }));
    }
    {   // VOLUME (Dry/Wet/Output)
        placeKnob (pcolV (0, volKA), drySlider,    dryLabel);
        placeKnob (pcolV (1, volKA), wetSlider,    wetLabel);
        placeKnob (pcolV (2, volKA), outputSlider, outputLabel);
    }
    {
        auto row = knobArea (duckPanel);
        const int cellW = row.getWidth() / 2;
        placeKnob (row.removeFromLeft (cellW), duckSlider,    duckLabel);
        placeKnob (row.removeFromLeft (cellW), duckRelSlider, duckRelLabel);
    }
    {   // IR SHAPE — IR Gain / Fade / Decay / Taper, aligned under POST
        auto row = knobArea (shapePanel);
        placeKnob (pcolP (0, row), irGainSlider, irGainLabel);   // under Bass Mono
        placeKnob (pcolP (1, row), fadeInSlider, fadeInLabel);   // under Pre-Delay
        placeKnob (pcolP (2, row), decaySlider,  decayLabel);    // under Tone
        placeKnob (pcolP (3, row), taperSlider,  taperLabel);    // under Width
    }
    {   // IR CHARACTER — Stretch, Damp + the toggle column, aligned under VOLUME
        auto row = knobArea (charPanel);
        placeKnob (pcolV (0, row), stretchSlider, stretchLabel);   // under Dry
        placeKnob (pcolV (1, row), dampSlider,    dampLabel);      // under Wet
        // toggle column under the Output column
        const int btnH = 28, gap = 6, colH = btnH * 3 + gap * 2;
        const int togLeft  = volKA.getX() + 2 * cwV;
        const int togRight = outputSlider.getBounds().getRight();
        auto toggles = juce::Rectangle<int> (togLeft, row.getCentreY() - colH / 2,
                                             togRight - togLeft, colH);
        reverseButton.setBounds  (toggles.removeFromTop (btnH));
        toggles.removeFromTop (gap);
        irNormButton.setBounds (toggles.removeFromTop (btnH));
        toggles.removeFromTop (gap);
        filterIRButton.setBounds (toggles.removeFromTop (btnH));
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

    updateKnobStates();   // dim the X-Over crossover while Bass Mono is off (its only no-op state)

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
