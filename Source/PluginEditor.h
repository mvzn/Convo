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

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ConvoLookAndFeel.h"

#include <memory>

/** A Slider whose value is hard-limited to a runtime cap: the thumb can't be dragged, wheeled,
    typed, or key-stepped past the cap in the first place (rather than overshooting and snapping
    back). JUCE routes every input method through snapValue(), so clamping there catches them all.
    The cap is refreshed each timer tick; cap < 0 means no cap. Used by Fade In, whose ceiling
    tracks the IR / decay-cut length. */
class CappedSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void setValueCap (double newCap) noexcept { valueCap = newCap; }

    double snapValue (double attemptedValue, DragMode dragMode) override
    {
        const double v = juce::Slider::snapValue (attemptedValue, dragMode);
        return valueCap >= 0.0 ? juce::jmin (v, valueCap) : v;
    }

private:
    double valueCap = -1.0;
};

/**
    Convo's editor. The heavy static chrome graphics (panels, meter wells, ticks, rules)
    are rendered once into a cached image on resize; the waveform is rendered into its own
    cached image only when the bake changes. Static chrome *text* (title, captions, meter
    labels) is drawn live in paint() — baking it into the scaled cache softens glyphs on
    HiDPI displays. paint() blits the caches, draws that text, then the few dynamic bits;
    the 30 Hz timer repaints nothing unless a meter value actually moved, so the editor
    idles at ~zero paint cost.
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

   #if JUCE_DEBUG
    bool keyPressed (const juce::KeyPress&) override;   // Cmd/Ctrl+Shift+S: supersampled screenshot to the desktop
   #endif

private:
    void timerCallback() override;
    void openFileChooser();
    void loadFile (const juce::File& file);
    void rebuildThumbnail();
    void showIRContextMenu();              // right-click on the IR display: reveal file / audition
    void showAboutMenu();                  // the header "i" hotspot: AGPL legal notice + source/licence links
    void setOutputFromMouseY (float y);    // drag the Output fader line on the OUT meter -> output param
    void setLedTextColour (juce::Button&, float amt, juce::Colour off, juce::Colour on);   // animated button text colour

    // presets
    void showPresetMenu();                 // popup: save new + pick by name
    void stepPreset (int direction);       // prev (-1) / next (+1) through the sorted folder
    void loadPresetFile (const juce::File& file);
    void promptSavePreset();               // async name prompt -> processor.savePreset

   #if JUCE_DEBUG
    void saveSupersampledScreenshot(); // dev tool: renders the editor off-screen at high resolution -> PNG on the desktop
   #endif

    void renderBackground();           // static chrome graphics -> backgroundImage
    void drawChromeText (juce::Graphics&);   // static chrome text, drawn live for HiDPI crispness
    void renderWaveImage();            // full-IR waveform -> waveImage (the trim backdrop)
    void renderKernelImage();          // trimmed+shaped kernel -> kernelImage (the selection layer)
    float irGainVisualGain() const;    // IR Gain as a linear factor -> waveform vertical zoom
    float waveVisualZoom (float layerPeak) const;   // per-layer zoom: height ∝ (peak x IR Gain)^0.5
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

    // Snap-to-onset: the detected end of the IR's direct-path spike is a magnet/jump target
    // for the trim handle that cuts the head, so a tail-only wet (no dry copy) is one gesture.
    bool displayReversed() const;               // Reverse param: the display shows the mirrored IR
    float onsetDisplayFrac() const;             // onset in display coords (mirrored under Reverse); -1 = none
    TrimHandle onsetHandle() const;             // the handle that cuts the head: End under Reverse, else Start

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
    juce::TextButton          presetButton { "Presets" };
    juce::TextButton          prevPresetButton { juce::String::fromUTF8 ("\xE2\x97\x80") };   // ◀
    juce::TextButton          nextPresetButton { juce::String::fromUTF8 ("\xE2\x96\xB6") };   // ▶
    juce::TextButton          playButton { "Play" };            // audition the IR through the output
    juce::TextButton          auditionSrcButton { "Baked" };    // audition source toggle: Baked / Raw
    bool                      playShown = false;                // last-painted audition state (timer-driven)
    float                     playLit = 0.0f, reverseLit = 0.0f, normLit = 0.0f;   // smooth LED lit crossfades (0..1)
    float                     bakedBlend = 1.0f;                // Baked/Raw colour crossfade (1 = copper / Baked, 0 = mint / Raw)
    juce::String              lastFileName;
    int                       lastBakeGen = -1;
    bool                      fileOver = false;
    bool                      inFilterLabelsOnIR = false;   // In HP/In LP labels currently read "IR …" (Filter IR on)
    double                    bakedLenSeconds = 0.0;
    juce::String              bakedLenText;          // cached "N.NN s" label (no per-paint String build)

    // parameter controls (Output has no knob — it's the fader line on the OUT meter)
    juce::Slider drySlider, wetSlider, irGainSlider, toneSlider, inHPSlider, inLPSlider, filterQSlider,
                 preDelaySlider, widthSlider, msBassSlider,
                 duckSlider, duckRelSlider, gateSlider, decaySlider, taperSlider, stretchSlider, dampSlider;
    CappedSlider fadeInSlider;   // hard-capped to the IR / decay-cut length so the thumb can't overshoot
    juce::Label  dryLabel, wetLabel, irGainLabel, toneLabel, inHPLabel, inLPLabel, filterQLabel,
                 preDelayLabel, widthLabel, msBassLabel,
                 duckLabel, duckRelLabel, gateLabel, fadeInLabel, decayLabel, taperLabel, stretchLabel, dampLabel;
    juce::TextButton   reverseButton { "Reverse" }, irNormButton { "Norm IR" };   // LED text-buttons on the waveform
    juce::ToggleButton filterIRButton { "Filter IR" },
                       wetCompButton { "Wet Comp" },
                       polarityButton { juce::String::fromUTF8 ("\xC3\x98") },   // Ø — invert the wet polarity
                       bypassButton { "Bypass" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dryAtt, wetAtt, irGainAtt, toneAtt, inHPAtt, inLPAtt, filterQAtt,
                                      preDelayAtt, widthAtt, msBassAtt,
                                      duckAtt, duckRelAtt, gateAtt, fadeInAtt, decayAtt, taperAtt, stretchAtt, dampAtt;
    std::unique_ptr<ButtonAttachment> reverseAtt, irNormAtt, filterIRAtt, wetCompAtt, polarityAtt, bypassAtt;
    bool draggingOutput = false;   // dragging the Output fader line on the OUT meter

    // meters: shown values + slower-decaying peak-hold lines, with last-painted
    // copies so the timer can skip repaints when nothing moved
    float inMeter = 0.0f, outMeter = 0.0f;
    float inPeak  = 0.0f, outPeak  = 0.0f;
    float inShown = -1.0f, outShown = -1.0f, inPeakShown = -1.0f, outPeakShown = -1.0f;
    float outGainShown = -1.0f;   // last-painted Output-fader position (param 0..1)
    float duckGR = 0.0f, duckGRShown = -1.0f;   // live ducking gain reduction painted on the Duck knob
    float gateActShown = -2.0f;                 // last-painted Gate-knob activity (grey -> mint)

    // last-seen values of the overlay params (tone / In HP / In LP / X-Over) so the
    // timer can repaint the wave layer when they move — they are not bake params
    float eqToneSeen = -1.0e9f, eqHpSeen = -1.0e9f, eqLpSeen = -1.0e9f, eqQSeen = -1.0e9f, eqBassSeen = -1.0e9f;

    // IR trim handles (Start/End): drag state + last-seen param values so the timer can
    // repaint the wave layer when a handle moves (the bake itself re-windows via the
    // processor's debounced timer; these params already drive the bake through APVTS)
    TrimHandle activeHandle = TrimHandle::none;   // handle currently being dragged
    TrimHandle hoverHandle  = TrimHandle::none;   // handle under the cursor (affordance)
    float dragStartFrac = 0.0f, dragEndFrac = 1.0f;   // live trim while dragging — committed to the params on mouse-up
    float onsetFrac = -1.0f;                      // raw-space onset-end fraction from the processor; -1 = none
    float trimStartSeen = -1.0e9f, trimEndSeen = -1.0e9f;
    float irGainSeen = -1.0e9f;        // last-seen IR Gain (visual factor) -> rescale the waveform
    static constexpr int kTrimHandleHitPx = 8;    // half-width of a handle's grab zone
    static constexpr int kTrimSnapPx      = 6;    // half-width of the onset marker's snap magnet

    // cached overlay: rebuilt only when its params/size change, then just stroked in paint
    juce::Path eqCurvePath;
    float      monoMarkerX = -1.0f;    // bass-mono marker x in waveZone coords; < 0 = hidden
    juce::dsp::IIR::Coefficients<float>::Ptr eqLo, eqHi, eqHp, eqLp;  // reused -> no per-redraw alloc
    juce::ColourGradient inMeterGrad, outMeterGrad;   // cached -> no per-frame gradient alloc

    std::unique_ptr<juce::FileChooser> chooser;

    // presets: the file last loaded via the arrows/menu, so the arrows can resume from it
    juce::File           currentPresetFile;
    std::unique_ptr<juce::AlertWindow> savePresetWindow;   // async "name this preset" dialog

    // cached chrome (rendered at physical resolution for HiDPI crispness)
    juce::Image backgroundImage, waveImage, waveBlurImage, kernelImage;   // waveBlurImage: blurred backdrop; kernelImage: shaped kernel in the selection

    // layout regions (used by paint)
    juce::Rectangle<int> headerZone, dropZone, waveZone, inMeterZone, outMeterZone,
                         filterPanel, postPanel, volumePanel, duckPanel, shapePanel, charPanel;
    juce::Rectangle<int> aboutZone;        // small "i" hotspot by the tagline (AGPL legal notice)
    bool updateNoticeSeen = false;         // repaint the "i" once when the async update check lands
    float fadeMaxShown = -2.0f;            // last-painted fade-in limit tick (arc proportion), -1 = none
    // true peaks of the (peak-normalized) thumbnail layers — the 8-bit thumbnail cache
    // holds unit-peak data; these put the real level back in via waveVisualZoom
    float waveDispPeak = 0.0f, kernelDispPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessorEditor)
};
