#include "PluginEditor.h"

namespace
{
    void styleRotary (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    }
}

ConvoAudioProcessorEditor::ConvoAudioProcessorEditor (ConvoAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    thumbnailFormatManager.registerBasicFormats();
    thumbnail.addChangeListener (this);

    auto& apvts = processor.getAPVTS();

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        styleRotary (s);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (l);
    };

    setup (drySlider,      dryLabel,      "Dry");
    setup (wetSlider,      wetLabel,      "Wet");
    setup (outputSlider,   outputLabel,   "Output");
    setup (toneSlider,     toneLabel,     "Tone");
    setup (preDelaySlider, preDelayLabel, "Pre-Delay");
    setup (widthSlider,    widthLabel,    "Width");
    setup (duckSlider,     duckLabel,     "Duck");
    setup (duckRelSlider,  duckRelLabel,  "Duck Rel");
    setup (fadeInSlider,   fadeInLabel,   "Fade In");
    setup (decaySlider,    decayLabel,    "Decay");
    setup (taperSlider,    taperLabel,    "Taper");

    for (auto* b : { &reverseButton, &bypassButton })
    {
        b->setColour (juce::ToggleButton::textColourId, juce::Colours::lightgrey);
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
    reverseAtt  = std::make_unique<ButtonAttachment> (apvts, "reverse",     reverseButton);
    bypassAtt   = std::make_unique<ButtonAttachment> (apvts, "bypass",      bypassButton);

    lastFileName = processor.getIRLibrary().getDisplayName();
    fileNameLabel.setText (lastFileName, juce::dontSendNotification);
    fileNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    fileNameLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fileNameLabel);

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    rebuildThumbnail();

    setSize (760, 470);
    startTimerHz (30);
}

ConvoAudioProcessorEditor::~ConvoAudioProcessorEditor()
{
    stopTimer();
    thumbnail.removeChangeListener (this);
}

void ConvoAudioProcessorEditor::rebuildThumbnail()
{
    const auto&  ir = processor.getBakedIR();
    const double sr = processor.getBakedIRSampleRate();

    if (ir.getNumSamples() > 0 && sr > 0.0)
    {
        thumbnail.reset (ir.getNumChannels(), sr, ir.getNumSamples());
        thumbnail.addBlock (0, ir, 0, ir.getNumSamples());
    }
    else
    {
        thumbnail.clear();
    }

    lastBakeGen = processor.getBakeGeneration();
    repaint();
}

void ConvoAudioProcessorEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> zone,
                                           float level, const juce::String& label)
{
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (zone.toFloat(), 3.0f);

    const float l = juce::jlimit (0.0f, 1.0f, level);
    if (l > 0.0001f)
    {
        auto fill = zone.toFloat();
        fill = fill.removeFromBottom (fill.getHeight() * l);
        g.setColour (l > 0.95f ? juce::Colours::red : juce::Colour (0xff5ad17a));
        g.fillRoundedRectangle (fill, 3.0f);
    }

    g.setColour (juce::Colour (0xff888888));
    g.drawText (label, zone.getX(), zone.getBottom(), zone.getWidth(), 14,
                juce::Justification::centred);
}

void ConvoAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2b2b));

    // drop / waveform area
    g.setColour (fileOver ? juce::Colour (0xff3d5a80) : juce::Colour (0xff1f1f1f));
    g.fillRoundedRectangle (dropZone.toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff555555));
    g.drawRoundedRectangle (dropZone.toFloat(), 6.0f, 1.0f);

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (juce::Colour (0xff6ab0ff));
        thumbnail.drawChannels (g, waveZone, 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff888888));
        g.drawText ("Drop .wav / .mp3 / .aiff / .ogg here  (or click Load IR)",
                    waveZone, juce::Justification::centred);
    }

    drawMeter (g, inMeterZone,  inMeter,  "IN");
    drawMeter (g, outMeterZone, outMeter, "OUT");
}

void ConvoAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto topRow = area.removeFromTop (150);

    outMeterZone = topRow.removeFromRight (30).reduced (2, 6);
    topRow.removeFromRight (6);
    inMeterZone  = topRow.removeFromRight (30).reduced (2, 6);
    topRow.removeFromRight (10);
    dropZone = topRow;

    auto inner  = dropZone.reduced (8);
    auto header = inner.removeFromTop (26);
    loadButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (8);
    fileNameLabel.setBounds (header);
    inner.removeFromTop (4);
    waveZone = inner;

    area.removeFromTop (16);

    const int numCols = 6;
    auto knobArea = area;
    const int rowH  = 120;
    const int cellW = knobArea.getWidth() / numCols;

    auto placeKnob = [&] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        l.setBounds (cell.removeFromTop (16));
        s.setBounds (cell.reduced (4));
    };

    // row 1
    {
        auto row = knobArea.removeFromTop (rowH);
        placeKnob (row.removeFromLeft (cellW), drySlider,      dryLabel);
        placeKnob (row.removeFromLeft (cellW), wetSlider,      wetLabel);
        placeKnob (row.removeFromLeft (cellW), outputSlider,   outputLabel);
        placeKnob (row.removeFromLeft (cellW), toneSlider,     toneLabel);
        placeKnob (row.removeFromLeft (cellW), preDelaySlider, preDelayLabel);
        placeKnob (row.removeFromLeft (cellW), widthSlider,    widthLabel);
    }

    knobArea.removeFromTop (8);

    // row 2 (5 knobs + the two toggles stacked in the last cell)
    {
        auto row = knobArea.removeFromTop (rowH);
        placeKnob (row.removeFromLeft (cellW), duckSlider,    duckLabel);
        placeKnob (row.removeFromLeft (cellW), duckRelSlider, duckRelLabel);
        placeKnob (row.removeFromLeft (cellW), fadeInSlider,  fadeInLabel);
        placeKnob (row.removeFromLeft (cellW), decaySlider,   decayLabel);
        placeKnob (row.removeFromLeft (cellW), taperSlider,   taperLabel);

        auto toggles = row.removeFromLeft (cellW).reduced (6, 16);
        reverseButton.setBounds (toggles.removeFromTop (28));
        toggles.removeFromTop (6);
        bypassButton.setBounds (toggles.removeFromTop (28));
    }
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
    repaint();
}

void ConvoAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    fileOver = false;
    repaint();
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
    repaint();
}

void ConvoAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void ConvoAudioProcessorEditor::timerCallback()
{
    inMeter  = processor.getInputLevel();
    outMeter = processor.getOutputLevel();

    const auto name = processor.getIRLibrary().getDisplayName();
    if (name != lastFileName)
    {
        lastFileName = name;
        fileNameLabel.setText (name, juce::dontSendNotification);
    }

    if (processor.getBakeGeneration() != lastBakeGen)
        rebuildThumbnail();

    repaint (inMeterZone.expanded (0, 16));
    repaint (outMeterZone.expanded (0, 16));
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
        fileNameLabel.setText ("Failed to load: " + file.getFileName(), juce::dontSendNotification);
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
