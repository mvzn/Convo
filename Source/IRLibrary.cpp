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

#include "IRLibrary.h"

IRLibrary::IRLibrary()
{
    // WAV, AIFF, FLAC, OGG, and — when JUCE_USE_MP3AUDIOFORMAT is on — MP3 too,
    // so no extra registerFormat call (it would assert as a duplicate in debug).
    // MP3 stays off in the demo build (licensing); a post-demo MP3->WAV import
    // converter is the planned path for accepting MP3 IRs (see prd.md Roadmap).
    formatManager.registerBasicFormats();
}

bool IRLibrary::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return false;

    const double sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;

    // bound the decode BEFORE any (int) narrowing: length stays in 64-bit until
    // it is provably within the cap
    const juce::int64 maxSamples64  = (juce::int64) (kMaxSeconds * sampleRate);
    const bool        willTruncate  = reader->lengthInSamples > maxSamples64;
    const int         numSamples    = (int) juce::jmin (reader->lengthInSamples, maxSamples64);
    const int         numChannels   = juce::jmin (2, (int) reader->numChannels);

    juce::AudioBuffer<float> tmp (numChannels, numSamples);
    reader->read (&tmp, 0, numSamples, 0, true, true);

    irBuffer     = std::move (tmp);
    irSampleRate = sampleRate;
    currentFile  = file;
    truncated    = willTruncate;
    return true;
}

juce::String IRLibrary::getDisplayName() const
{
    if (! currentFile.existsAsFile())
        return "No IR loaded";

    return truncated ? currentFile.getFileName() + " (truncated to " + juce::String ((int) kMaxSeconds) + " s)"
                     : currentFile.getFileName();
}

bool IRLibrary::isSupported (const juce::File& file) const
{
    return formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}
