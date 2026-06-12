#pragma once

#include <JuceHeader.h>

/**
    Decodes and holds the raw impulse response.

    Convo's IRLibrary is deliberately simple: it owns the decoded IR buffer at its
    original sample rate and nothing more. All shaping (reverse / fade-in / decay /
    taper) happens later, in ConvolutionEngine, against this raw buffer. There is no
    note addressing or transposition here (that lived in Convsyn).
*/
class IRLibrary
{
public:
    IRLibrary();

    /** Decode an audio file into the IR buffer. Returns true on success.
        Call from the message thread (allocates / does file IO). */
    bool loadFile (const juce::File& file);

    bool hasIR() const noexcept { return irBuffer.getNumSamples() > 0; }

    const juce::AudioBuffer<float>& getIR() const noexcept { return irBuffer; }
    double getSampleRate() const noexcept { return irSampleRate; }

    const juce::File& getCurrentFile() const noexcept { return currentFile; }
    juce::String getDisplayName() const;

    /** True if the file extension is one we can decode. */
    bool isSupported (const juce::File& file) const;

private:
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> irBuffer;   // original IR, at irSampleRate
    double irSampleRate = 0.0;
    juce::File currentFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRLibrary)
};
