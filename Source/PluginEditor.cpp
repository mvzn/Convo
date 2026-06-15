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
    rawLevelButton.setTooltip ("Use the IR's recorded level unscaled — dense full-scale "
                               "audio can convolve 30..45 dB hot");
    wetCompButton.setTooltip  ("Adaptive wet gain compensation: tracks the dry input level "
                               "and trims the wet to match; frozen while the input is quiet "
                               "so tails ring out");
    inHPSlider.setTooltip ("Pre-IR high-pass (low cut), 6 dB/oct, on the signal feeding the IR");
    inLPSlider.setTooltip ("Pre-IR low-pass (high cut), 6 dB/oct, on the signal feeding the IR");
    fadeInSlider.setTooltip ("Raised-cosine fade-in baked into the IR; the ramp is capped at 80% of the IR length");
    msButton.setTooltip   ("Mid/Side: convolve mid-with-mid and side-with-side (re-bakes the "
                           "IR as M/S). Wants a stereo IR — a mono IR collapses to mono");
    msBassSlider.setTooltip ("Bass Mono (Mid/Side only): high-passes the side so content below "
                             "the cutoff collapses to mono. 20 Hz = off");
    filterIRButton.setTooltip ("Apply the In HP/In LP filter to the IR (baked, shown in the "
                               "display) instead of the input. Same sound; IR mode is cheaper "
                               "at runtime but re-bakes when you move the cutoffs");
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

    lastFileName = processor.getIRLibrary().getDisplayName();
    fileNameLabel.setText (lastFileName, juce::dontSendNotification);
    fileNameLabel.setFont (juce::Font (juce::FontOptions (13.5f)));
    fileNameLabel.setColour (juce::Label::textColourId, ConvoColours::text);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

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
        g.drawText (title, h.removeFromLeft (92), juce::Justification::centredLeft);

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
    panel (signalPanel, "SIGNAL");
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

    if (monoMarkerX >= 0.0f)
    {
        const auto zone = waveZone.toFloat();
        const juce::Line<float> vline (monoMarkerX, zone.getY(), monoMarkerX, zone.getBottom());
        const auto monoCol = ConvoColours::copper;

        g.setColour (monoCol.withAlpha (0.10f)); g.drawLine (vline, 3.0f);   // tight glow
        g.setColour (monoCol.withAlpha (0.16f)); g.drawLine (vline, 2.0f);
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (monoCol.withAlpha (0.5f));  g.drawDashedLine (vline, dashes, 2, 1.0f);

        // label the two sides of the crossover: mono below, stereo above
        g.setFont (captionFont());
        if (monoMarkerX - zone.getX() > 34.0f)
        {
            g.setColour (monoCol.withAlpha (0.75f));
            g.drawText ("mono", juce::Rectangle<float> (zone.getX(), zone.getBottom() - 16.0f,
                                                        monoMarkerX - zone.getX() - 5.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        if (zone.getRight() - monoMarkerX > 40.0f)
        {
            g.setColour (ConvoColours::mint.withAlpha (0.7f));   // mint = the stereo/wide side
            g.drawText ("stereo", juce::Rectangle<float> (monoMarkerX + 5.0f, zone.getBottom() - 16.0f,
                                                          zone.getRight() - monoMarkerX - 7.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
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
    loadButton.setBounds (header.removeFromRight (92));
    header.removeFromRight (8);
    fileNameLabel.setBounds (header);
    inner.removeFromTop (6);
    waveZone = inner;

    area.removeFromTop (12);
    signalPanel = area.removeFromTop (170);
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

    {   // SIGNAL — real-time mix / tone / stereo (everything that doesn't re-bake the IR)
        auto row = knobArea (signalPanel);
        const int cellW = row.getWidth() / 7;
        placeKnob (row.removeFromLeft (cellW), drySlider,      dryLabel);
        placeKnob (row.removeFromLeft (cellW), wetSlider,      wetLabel);
        placeKnob (row.removeFromLeft (cellW), outputSlider,   outputLabel);
        placeKnob (row.removeFromLeft (cellW), toneSlider,     toneLabel);
        placeKnob (row.removeFromLeft (cellW), preDelaySlider, preDelayLabel);
        placeKnob (row.removeFromLeft (cellW), widthSlider,    widthLabel);
        placeKnob (row.removeFromLeft (cellW), msBassSlider,   msBassLabel);
    }
    {
        auto row = knobArea (duckPanel);
        const int cellW = row.getWidth() / 2;
        placeKnob (row.removeFromLeft (cellW), duckSlider,    duckLabel);
        placeKnob (row.removeFromLeft (cellW), duckRelSlider, duckRelLabel);
    }
    {   // IR SHAPE — everything that changes the IR bake, plus IR Gain
        auto row = knobArea (shapePanel);
        const int cellW = row.getWidth() / 7;     // 6 knobs + a toggle column
        placeKnob (row.removeFromLeft (cellW), irGainSlider, irGainLabel);
        placeKnob (row.removeFromLeft (cellW), inHPSlider,   inHPLabel);
        placeKnob (row.removeFromLeft (cellW), inLPSlider,   inLPLabel);
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
