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
    thumbnail.addChangeListener (this);

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
    setup (outputSlider,   outputLabel,   "Output");
    setup (toneSlider,     toneLabel,     "Tone");
    setup (preDelaySlider, preDelayLabel, "Pre-Delay");
    setup (widthSlider,    widthLabel,    "Width");
    setup (duckSlider,     duckLabel,     "Duck");
    setup (duckRelSlider,  duckRelLabel,  "Release");
    setup (fadeInSlider,   fadeInLabel,   "Fade In");
    setup (decaySlider,    decayLabel,    "Decay");
    setup (taperSlider,    taperLabel,    "Taper");

    reverseButton.setColour   (juce::ToggleButton::tickColourId, ConvoColours::mint);    // green = active
    clipGuardButton.setColour (juce::ToggleButton::tickColourId, ConvoColours::mint);
    rawLevelButton.setColour  (juce::ToggleButton::tickColourId, ConvoColours::copper);  // copper = "careful"
    bypassButton.setColour    (juce::ToggleButton::tickColourId, ConvoColours::copper);
    rawLevelButton.setTooltip ("Use the IR's recorded level unscaled - dense full-scale "
                               "audio can convolve 30 to 45 dB hot");
    for (auto* b : { &reverseButton, &rawLevelButton, &clipGuardButton, &bypassButton })
    {
        b->setColour (juce::ToggleButton::textColourId, ConvoColours::label);
        addAndMakeVisible (*b);
    }

    dryAtt      = std::make_unique<SliderAttachment> (apvts, "dry",         drySlider);
    wetAtt      = std::make_unique<SliderAttachment> (apvts, "wet",         wetSlider);
    outputAtt   = std::make_unique<SliderAttachment> (apvts, "output",      outputSlider);
    toneAtt     = std::make_unique<SliderAttachment> (apvts, "tone",        toneSlider);
    preDelayAtt = std::make_unique<SliderAttachment> (apvts, "preDelay",    preDelaySlider);
    widthAtt    = std::make_unique<SliderAttachment> (apvts, "width",       widthSlider);
    duckAtt     = std::make_unique<SliderAttachment> (apvts, "duck",        duckSlider);
    duckRelAtt  = std::make_unique<SliderAttachment> (apvts, "duckRelease", duckRelSlider);
    fadeInAtt   = std::make_unique<SliderAttachment> (apvts, "fadeIn",      fadeInSlider);
    decayAtt    = std::make_unique<SliderAttachment> (apvts, "decay",       decaySlider);
    taperAtt    = std::make_unique<SliderAttachment> (apvts, "taper",       taperSlider);
    reverseAtt   = std::make_unique<ButtonAttachment> (apvts, "reverse",   reverseButton);
    rawLevelAtt  = std::make_unique<ButtonAttachment> (apvts, "irRaw",     rawLevelButton);
    clipGuardAtt = std::make_unique<ButtonAttachment> (apvts, "clipGuard", clipGuardButton);
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

    setSize (820, 610);
    startTimerHz (30);
}

ConvoAudioProcessorEditor::~ConvoAudioProcessorEditor()
{
    stopTimer();
    thumbnail.removeChangeListener (this);
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
        thumbnail.reset (ir.getNumChannels(), sr, ir.getNumSamples());
        thumbnail.addBlock (0, ir, 0, ir.getNumSamples());
        bakedLenSeconds = ir.getNumSamples() / sr;
    }
    else
    {
        thumbnail.clear();
        bakedLenSeconds = 0.0;
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
    if (waveZone.isEmpty() || thumbnail.getTotalLength() <= 0.0)
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
    thumbnail.drawChannels (g, local, 0.0, thumbnail.getTotalLength(), 1.0f);
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
                                               float level, float peak)
{
    const auto well = zone.toFloat().reduced (1.5f);

    const float l = juce::jlimit (0.0f, 1.0f, level);
    if (l > 0.001f)
    {
        // the gradient spans the whole well, so colours map to absolute level:
        // phthalo low, mint mid, amber hot, red at the top
        juce::ColourGradient grad (ConvoColours::accent, well.getX(), well.getBottom(),
                                   ConvoColours::clip,   well.getX(), well.getY(), false);
        grad.addColour (0.55, ConvoColours::mint);
        grad.addColour (0.85, meterAmber);
        g.setGradientFill (grad);

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

            if (bakedLenSeconds > 0.0)
            {
                g.setColour (ConvoColours::textDim);
                g.setFont (juce::Font (juce::FontOptions (11.5f)));
                g.drawText (juce::String (bakedLenSeconds, 2) + " s",
                            waveZone.reduced (6, 4), juce::Justification::bottomRight);
            }
        }
        else
        {
            g.setColour (ConvoColours::textDim);
            g.drawText ("Drop .wav / .aiff / .flac / .ogg / .mp3 here  (or click Load IR)",
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
        drawMeterFill (g, inMeterZone, inMeter, inPeak);
    if (clip.intersects (outMeterZone))
        drawMeterFill (g, outMeterZone, outMeter, outPeak);
}

void ConvoAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    headerZone = area.removeFromTop (42);
    bypassButton.setBounds (headerZone.removeFromRight (92).withSizeKeepingCentre (92, 26));
    headerZone.removeFromRight (8);
    clipGuardButton.setBounds (headerZone.removeFromRight (112).withSizeKeepingCentre (112, 26));
    area.removeFromTop (8);

    auto topRow = area.removeFromTop (168);
    auto outCol = topRow.removeFromRight (26);
    topRow.removeFromRight (6);
    auto inCol  = topRow.removeFromRight (26);
    topRow.removeFromRight (12);
    outMeterZone = outCol.withTrimmedBottom (16);   // labels live below the wells
    inMeterZone  = inCol.withTrimmedBottom (16);
    dropZone = topRow;

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
    duckPanel = row2.removeFromLeft ((row2.getWidth() - 10) * 2 / 6);
    row2.removeFromLeft (10);
    shapePanel = row2;

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

    {
        auto row = knobArea (signalPanel);
        const int cellW = row.getWidth() / 6;
        placeKnob (row.removeFromLeft (cellW), drySlider,      dryLabel);
        placeKnob (row.removeFromLeft (cellW), wetSlider,      wetLabel);
        placeKnob (row.removeFromLeft (cellW), outputSlider,   outputLabel);
        placeKnob (row.removeFromLeft (cellW), toneSlider,     toneLabel);
        placeKnob (row.removeFromLeft (cellW), preDelaySlider, preDelayLabel);
        placeKnob (row.removeFromLeft (cellW), widthSlider,    widthLabel);
    }
    {
        auto row = knobArea (duckPanel);
        const int cellW = row.getWidth() / 2;
        placeKnob (row.removeFromLeft (cellW), duckSlider,    duckLabel);
        placeKnob (row.removeFromLeft (cellW), duckRelSlider, duckRelLabel);
    }
    {
        auto row = knobArea (shapePanel);
        const int cellW = row.getWidth() / 4;
        placeKnob (row.removeFromLeft (cellW), fadeInSlider, fadeInLabel);
        placeKnob (row.removeFromLeft (cellW), decaySlider,  decayLabel);
        placeKnob (row.removeFromLeft (cellW), taperSlider,  taperLabel);
        auto cell = row.removeFromLeft (cellW);
        auto toggles = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 8, 110), 28 * 2 + 8);
        reverseButton.setBounds  (toggles.removeFromTop (28));
        toggles.removeFromTop (8);
        rawLevelButton.setBounds (toggles.removeFromTop (28));
    }

    renderBackground();
    renderWaveImage();
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
                                                   "*.wav;*.aif;*.aiff;*.ogg;*.mp3;*.flac");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto result = fc.getResult();
        if (result.existsAsFile())
            loadFile (result);
    });
}
