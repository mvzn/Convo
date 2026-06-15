#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ConvoLookAndFeel.h"

#include <memory>

/**
    Convo's editor. All chrome that never changes between frames (panels, captions,
    title, meter wells) is rendered once into a cached image on resize; the waveform
    is rendered into its own cached image only when the bake changes. paint() then
    just blits and draws the few dynamic bits, and the 30 Hz timer repaints nothing
    unless a meter value actually moved — the editor idles at ~zero paint cost.
*/
class ConvoAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  public juce::ChangeListener,
                                  private juce::Timer
{
public:
    explicit ConvoAudioProcessorEditor (ConvoAudioProcessor&);
    ~ConvoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // drag & drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;
    void openFileChooser();
    void loadFile (const juce::File& file);
    void rebuildThumbnail();

    void renderBackground();           // static chrome -> backgroundImage
    void renderWaveImage();            // baked-IR waveform -> waveImage
    void drawMeterFill (juce::Graphics&, juce::Rectangle<int> zone, float level, float peak,
                        const juce::ColourGradient& fill);
    void renderOverlay();              // (re)build the cached EQ curve + bass-mono marker on param/size change
    void drawFilterOverlay (juce::Graphics&);   // stroke the cached overlay over the wave (no recompute)
    void updateKnobStates();           // mode hints: dim Bass Mono (M/S off), relabel In->IR (Filter IR on)
    float uiScale() const;             // physical px per logical px, for crisp caches

    ConvoAudioProcessor& processor;

    ConvoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };   // shows the controls' setTooltip text on hover

    // IR display (shows the *processed* IR)
    juce::AudioFormatManager  thumbnailFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    // rebuilt per IR with a length-adaptive resolution (see rebuildThumbnail) so short,
    // heavily-decayed IRs don't render as a handful of wide min/max bricks
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    juce::Label               fileNameLabel;
    juce::TextButton          loadButton { "Load IR..." };
    juce::String              lastFileName;
    int                       lastBakeGen = -1;
    bool                      fileOver = false;
    bool                      inFilterLabelsOnIR = false;   // In HP/In LP labels currently read "IR …" (Filter IR on)
    double                    bakedLenSeconds = 0.0;
    juce::String              bakedLenText;          // cached "N.NN s" label (no per-paint String build)

    // parameter controls
    juce::Slider drySlider, wetSlider, irGainSlider, outputSlider, toneSlider, inHPSlider, inLPSlider,
                 preDelaySlider, widthSlider, msBassSlider, feedbackSlider, dampSlider,
                 duckSlider, duckRelSlider, fadeInSlider, decaySlider, taperSlider, stretchSlider;
    juce::Label  dryLabel, wetLabel, irGainLabel, outputLabel, toneLabel, inHPLabel, inLPLabel,
                 preDelayLabel, widthLabel, msBassLabel, feedbackLabel, dampLabel,
                 duckLabel, duckRelLabel, fadeInLabel, decayLabel, taperLabel, stretchLabel;
    juce::ToggleButton reverseButton { "Reverse" }, rawLevelButton { "Raw IR" }, filterIRButton { "Filter IR" },
                       clipGuardButton { "Clip Guard" }, wetCompButton { "Wet Comp" },
                       msButton { "Mid/Side" }, bypassButton { "Bypass" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dryAtt, wetAtt, irGainAtt, outputAtt, toneAtt, inHPAtt, inLPAtt,
                                      preDelayAtt, widthAtt, msBassAtt, feedbackAtt, dampAtt,
                                      duckAtt, duckRelAtt, fadeInAtt, decayAtt, taperAtt, stretchAtt;
    std::unique_ptr<ButtonAttachment> reverseAtt, rawLevelAtt, filterIRAtt, clipGuardAtt, wetCompAtt, msAtt, bypassAtt;

    // meters: shown values + slower-decaying peak-hold lines, with last-painted
    // copies so the timer can skip repaints when nothing moved
    float inMeter = 0.0f, outMeter = 0.0f;
    float inPeak  = 0.0f, outPeak  = 0.0f;
    float inShown = -1.0f, outShown = -1.0f, inPeakShown = -1.0f, outPeakShown = -1.0f;

    // last-seen values of the overlay params (tone / In HP / In LP / Bass Mono / M/S) so the
    // timer can repaint the wave layer when they move — they are not bake params
    float eqToneSeen = -1.0e9f, eqHpSeen = -1.0e9f, eqLpSeen = -1.0e9f, eqBassSeen = -1.0e9f;
    bool  eqMsSeen = false;

    // cached overlay: rebuilt only when its params/size change, then just stroked in paint
    juce::Path eqCurvePath;
    float      monoMarkerX = -1.0f;    // bass-mono marker x in waveZone coords; < 0 = hidden
    juce::dsp::IIR::Coefficients<float>::Ptr eqLo, eqHi, eqHp, eqLp;  // reused -> no per-redraw alloc
    juce::ColourGradient inMeterGrad, outMeterGrad;   // cached -> no per-frame gradient alloc

    std::unique_ptr<juce::FileChooser> chooser;

    // cached chrome (rendered at physical resolution for HiDPI crispness)
    juce::Image backgroundImage, waveImage;

    // layout regions (used by paint)
    juce::Rectangle<int> headerZone, dropZone, waveZone, inMeterZone, outMeterZone,
                         prePanel, postPanel, duckPanel, shapePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessorEditor)
};
