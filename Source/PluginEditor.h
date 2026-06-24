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

    // IR trim handles on the waveform
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void openFileChooser();
    void loadFile (const juce::File& file);
    void rebuildThumbnail();

    // presets
    void showPresetMenu();                 // popup: save new + pick by name
    void stepPreset (int direction);       // prev (-1) / next (+1) through the sorted folder
    void loadPresetFile (const juce::File& file);
    void promptSavePreset();               // async name prompt -> processor.savePreset

    void renderBackground();           // static chrome -> backgroundImage
    void renderWaveImage();            // full-IR waveform -> waveImage (the trim backdrop)
    void renderKernelImage();          // trimmed+shaped kernel -> kernelImage (the selection layer)
    void drawMeterFill (juce::Graphics&, juce::Rectangle<int> zone, float level, float peak,
                        const juce::ColourGradient& fill);
    void renderOverlay();              // (re)build the cached EQ curve + bass-mono marker on param/size change
    void drawFilterOverlay (juce::Graphics&);   // stroke the cached overlay over the wave (no recompute)
    void drawTrimHandles (juce::Graphics&);     // dim trimmed regions + draw the Start/End handles
    void updateKnobStates();           // mode hints: dim X-Over (Bass Mono off), relabel In->IR (Filter IR on)
    float uiScale() const;             // physical px per logical px, for crisp caches

    // IR trim handle geometry / hit-testing (all in editor-local coords)
    enum class TrimHandle { none, start, end };
    float trimFracToX (float frac) const;       // map a 0..1 fraction to an x in waveZone
    float trimXToFrac (int x) const;            // map an editor x to a clamped 0..1 fraction
    TrimHandle trimHandleAt (juce::Point<int> p) const;   // which handle (if any) is under the cursor
    float liveTrimStart() const;                // Start frac: the live drag value while dragging, else the param
    float liveTrimEnd()   const;                // End frac: same

    ConvoAudioProcessor& processor;

    ConvoLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };   // shows the controls' setTooltip text on hover

    // IR display (shows the *processed* IR)
    juce::AudioFormatManager  thumbnailFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    // rebuilt per IR with a length-adaptive resolution (see rebuildThumbnail) so short,
    // heavily-decayed IRs don't render as a handful of wide min/max bricks
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    juce::AudioThumbnailCache kernelThumbnailCache { 2 };
    std::unique_ptr<juce::AudioThumbnail> kernelThumbnail;   // trimmed+shaped kernel, drawn inside the selection
    juce::Label               fileNameLabel;
    juce::TextButton          loadButton { "Load IR..." };
    juce::TextButton          presetButton { "Presets" };
    juce::TextButton          prevPresetButton { juce::String::fromUTF8 ("\xE2\x97\x80") };   // ◀
    juce::TextButton          nextPresetButton { juce::String::fromUTF8 ("\xE2\x96\xB6") };   // ▶
    juce::String              lastFileName;
    int                       lastBakeGen = -1;
    bool                      fileOver = false;
    bool                      inFilterLabelsOnIR = false;   // In HP/In LP labels currently read "IR …" (Filter IR on)
    double                    bakedLenSeconds = 0.0;
    juce::String              bakedLenText;          // cached "N.NN s" label (no per-paint String build)

    // parameter controls
    juce::Slider drySlider, wetSlider, irGainSlider, outputSlider, toneSlider, inHPSlider, inLPSlider,
                 preDelaySlider, widthSlider, msBassSlider,
                 duckSlider, duckRelSlider, fadeInSlider, decaySlider, taperSlider, stretchSlider;
    juce::Label  dryLabel, wetLabel, irGainLabel, outputLabel, toneLabel, inHPLabel, inLPLabel,
                 preDelayLabel, widthLabel, msBassLabel,
                 duckLabel, duckRelLabel, fadeInLabel, decayLabel, taperLabel, stretchLabel;
    juce::ToggleButton reverseButton { "Reverse" }, rawLevelButton { "Raw IR" }, filterIRButton { "Filter IR" },
                       clipGuardButton { "Clip Guard" }, wetCompButton { "Wet Comp" },
                       msButton { "" },               // Bass Mono enable — embedded (LED only) on the X-Over knob
                       bypassButton { "Bypass" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dryAtt, wetAtt, irGainAtt, outputAtt, toneAtt, inHPAtt, inLPAtt,
                                      preDelayAtt, widthAtt, msBassAtt,
                                      duckAtt, duckRelAtt, fadeInAtt, decayAtt, taperAtt, stretchAtt;
    std::unique_ptr<ButtonAttachment> reverseAtt, rawLevelAtt, filterIRAtt, clipGuardAtt, wetCompAtt, msAtt, bypassAtt;

    // meters: shown values + slower-decaying peak-hold lines, with last-painted
    // copies so the timer can skip repaints when nothing moved
    float inMeter = 0.0f, outMeter = 0.0f, irMeter = 0.0f;
    float inPeak  = 0.0f, outPeak  = 0.0f, irPeak  = 0.0f;
    float inShown = -1.0f, outShown = -1.0f, inPeakShown = -1.0f, outPeakShown = -1.0f;
    float irShown = -1.0f, irPeakShown = -1.0f;

    // last-seen values of the overlay params (tone / In HP / In LP / X-Over / Bass Mono) so the
    // timer can repaint the wave layer when they move — they are not bake params
    float eqToneSeen = -1.0e9f, eqHpSeen = -1.0e9f, eqLpSeen = -1.0e9f, eqBassSeen = -1.0e9f;
    bool  eqMsSeen = false;

    // IR trim handles (Start/End): drag state + last-seen param values so the timer can
    // repaint the wave layer when a handle moves (the bake itself re-windows via the
    // processor's debounced timer; these params already drive the bake through APVTS)
    TrimHandle activeHandle = TrimHandle::none;   // handle currently being dragged
    TrimHandle hoverHandle  = TrimHandle::none;   // handle under the cursor (affordance)
    float dragStartFrac = 0.0f, dragEndFrac = 1.0f;   // live trim while dragging — committed to the params on mouse-up
    float trimStartSeen = -1.0e9f, trimEndSeen = -1.0e9f;
    static constexpr int kTrimHandleHitPx = 8;    // half-width of a handle's grab zone

    // cached overlay: rebuilt only when its params/size change, then just stroked in paint
    juce::Path eqCurvePath;
    float      monoMarkerX = -1.0f;    // bass-mono marker x in waveZone coords; < 0 = hidden
    juce::dsp::IIR::Coefficients<float>::Ptr eqLo, eqHi, eqHp, eqLp;  // reused -> no per-redraw alloc
    juce::ColourGradient inMeterGrad, outMeterGrad, irMeterGrad;   // cached -> no per-frame gradient alloc

    std::unique_ptr<juce::FileChooser> chooser;

    // presets: the file last loaded via the arrows/menu, so the arrows can resume from it
    juce::File           currentPresetFile;
    std::unique_ptr<juce::AlertWindow> savePresetWindow;   // async "name this preset" dialog

    // cached chrome (rendered at physical resolution for HiDPI crispness)
    juce::Image backgroundImage, waveImage, waveBlurImage, kernelImage;   // waveBlurImage: blurred backdrop; kernelImage: shaped kernel in the selection

    // layout regions (used by paint)
    juce::Rectangle<int> headerZone, dropZone, waveZone, inMeterZone, outMeterZone,
                         filterPanel, postPanel, volumePanel, duckPanel, shapePanel, irMeterZone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessorEditor)
};
