#include "IRLibrary.h"

IRLibrary::IRLibrary()
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, OGG (+ platform extras)

   #if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat (new juce::MP3AudioFormat(), false);
   #endif
}

bool IRLibrary::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return false;

    const int numChannels = (int) reader->numChannels;
    const int numSamples  = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> tmp (numChannels, numSamples);
    reader->read (&tmp, 0, numSamples, 0, true, true);

    irBuffer     = std::move (tmp);
    irSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    currentFile  = file;
    return true;
}

juce::String IRLibrary::getDisplayName() const
{
    return currentFile.existsAsFile() ? currentFile.getFileName()
                                      : juce::String ("No IR loaded");
}

bool IRLibrary::isSupported (const juce::File& file) const
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav"  || ext == ".aif" || ext == ".aiff"
        || ext == ".ogg"  || ext == ".mp3" || ext == ".flac";
}
