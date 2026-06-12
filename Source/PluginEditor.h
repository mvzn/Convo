#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ConvoLookAndFeel.h"

#include <memory>

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
    void drawMeter (juce::Graphics&, juce::Rectangle<int> zone, float level, const juce::String& label);

    ConvoAudioProcessor& processor;

    ConvoLookAndFeel lookAndFeel;

    // IR display (shows the *processed* IR)
    juce::AudioFormatManager  thumbnailFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail      thumbnail { 512, thumbnailFormatManager, thumbnailCache };
    juce::Label               fileNameLabel;
    juce::TextButton          loadButton { "Load IR..." };
    juce::String              lastFileName;
    int                       lastBakeGen = -1;
    bool                      fileOver = false;

    // parameter controls
    juce::Slider drySlider, wetSlider, outputSlider, toneSlider, preDelaySlider, widthSlider,
                 duckSlider, duckRelSlider, fadeInSlider, decaySlider, taperSlider;
    juce::Label  dryLabel, wetLabel, outputLabel, toneLabel, preDelayLabel, widthLabel,
                 duckLabel, duckRelLabel, fadeInLabel, decayLabel, taperLabel;
    juce::ToggleButton reverseButton { "Reverse" }, bypassButton { "Bypass" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> dryAtt, wetAtt, outputAtt, toneAtt, preDelayAtt, widthAtt,
                                      duckAtt, duckRelAtt, fadeInAtt, decayAtt, taperAtt;
    std::unique_ptr<ButtonAttachment> reverseAtt, bypassAtt;

    // meters
    float inMeter = 0.0f, outMeter = 0.0f;

    std::unique_ptr<juce::FileChooser> chooser;

    // layout regions (used by paint)
    juce::Rectangle<int> dropZone, waveZone, inMeterZone, outMeterZone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessorEditor)
};
