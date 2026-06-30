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

/**
    Decodes and holds the raw impulse response.

    Convo's IRLibrary is deliberately simple: it owns the decoded IR buffer at its
    original sample rate and nothing more. All shaping (reverse / fade-in / decay /
    taper) happens later, in ConvolutionEngine, against this raw buffer. There is no
    note addressing or transposition here — Convo is a pure convolver, not a sampler.

    Decoding reads at most 2 channels (the convolution only ever uses two). There is
    no musical length cap: long songs load in full so they work as sound-design IRs
    (the non-uniform convolution engine keeps long IRs cheap). Only a generous safety
    ceiling (kMaxSeconds) guards against a dropped multi-hour file exhausting memory /
    overflowing the 32-bit sample count; such an extreme file is truncated, not rejected.
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

    /** True if the current IR hit the kMaxSeconds safety ceiling while decoding
        (only ever a pathological multi-hour file — normal IRs and songs load whole). */
    bool wasTruncated() const noexcept { return truncated; }

    /** True if a registered decoder handles this file's extension — can never
        drift from the formats that are actually available. */
    bool isSupported (const juce::File& file) const;

    // Safety ceiling only — not a musical limit. 10 min is far above any real IR or song,
    // stays well inside the 32-bit AudioBuffer sample count at every supported rate, and
    // bounds the worst-case decode allocation. Realistic IRs never reach it.
    static constexpr double kMaxSeconds = 600.0;

private:
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> irBuffer;   // original IR, at irSampleRate
    double irSampleRate = 0.0;
    juce::File currentFile;
    bool truncated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRLibrary)
};
