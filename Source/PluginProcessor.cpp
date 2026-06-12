#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

ConvoAudioProcessor::ConvoAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    dryParam      = apvts.getRawParameterValue ("dry");
    wetParam      = apvts.getRawParameterValue ("wet");
    outputParam   = apvts.getRawParameterValue ("output");
    toneParam     = apvts.getRawParameterValue ("tone");
    preDelayParam = apvts.getRawParameterValue ("preDelay");
    widthParam    = apvts.getRawParameterValue ("width");
    duckParam     = apvts.getRawParameterValue ("duck");
    duckRelParam  = apvts.getRawParameterValue ("duckRelease");
    fadeInParam   = apvts.getRawParameterValue ("fadeIn");
    decayParam    = apvts.getRawParameterValue ("decay");
    taperParam    = apvts.getRawParameterValue ("taper");
    reverseParam  = apvts.getRawParameterValue ("reverse");
    bypassParam   = apvts.getRawParameterValue ("bypass");
    bypassParameter = apvts.getParameter ("bypass");

    startTimerHz (30);
}

ConvoAudioProcessor::~ConvoAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout ConvoAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // --- real-time signal controls ---
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "dry", 1 }, "Dry",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "wet", 1 }, "Wet",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone",
        NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, "%"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "preDelay", 1 }, "Pre-Delay",
        NormalisableRange<float> (0.0f, 500.0f, 0.1f, 0.4f), 0.0f, "ms"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "width", 1 }, "Width",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, "%"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "duck", 1 }, "Duck",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, "%"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "duckRelease", 1 }, "Duck Rel",
        NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 200.0f, "ms"));

    // --- IR-bake controls ---
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fadeIn", 1 }, "Fade In",
        NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.4f), 0.0f, "ms"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay",
        NormalisableRange<float> (50.0f, kDecayOffMs, 1.0f, 0.3f), kDecayOffMs, "ms",
        AudioProcessorParameter::genericParameter,
        [] (float v, int) { return v >= ConvoAudioProcessor::kDecayOffMs - 0.5f
                                       ? juce::String ("Off") : juce::String (v, 0) + " ms"; }));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "taper", 1 }, "Taper",
        NormalisableRange<float> (0.0f, 500.0f, 1.0f), 10.0f, "ms"));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "reverse", 1 }, "Reverse", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

bool ConvoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    return true;
}

void ConvoAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels      = (juce::uint32) juce::jmax (1, getMainBusNumOutputChannels());

    convolution.prepare (spec);

    lowShelf.prepare (spec);
    highShelf.prepare (spec);
    *lowShelf.state  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sampleRate, 700.0f, 0.5f, 1.0f);
    *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 700.0f, 0.5f, 1.0f);
    lowShelf.reset();
    highShelf.reset();

    maxPreDelaySamples = (int) std::ceil (0.5 * sampleRate) + 1;
    preDelayLine.setMaximumDelayInSamples (maxPreDelaySamples);
    preDelayLine.prepare (spec);
    preDelayLine.reset();

    dryDelayLine.setMaximumDelayInSamples (ConvolutionEngine::kLongLatency + 1);
    dryDelayLine.prepare (spec);
    dryDelayLine.reset();
    currentDryDelay = -1;

    dryGainSm.reset    (sampleRate, 0.02);
    wetGainSm.reset    (sampleRate, 0.02);
    outputGainSm.reset (sampleRate, 0.02);
    toneSm.reset       (sampleRate, 0.05);
    widthSm.reset      (sampleRate, 0.02);
    duckSm.reset       (sampleRate, 0.02);
    bypassSm.reset     (sampleRate, 0.01);
    loadFade.reset     (sampleRate, 0.015);

    dryGainSm.setCurrentAndTargetValue    (juce::Decibels::decibelsToGain (dryParam->load(),    -60.0f));
    wetGainSm.setCurrentAndTargetValue    (juce::Decibels::decibelsToGain (wetParam->load(),    -60.0f));
    outputGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load(), -60.0f));
    toneSm.setCurrentAndTargetValue       (toneParam->load());
    widthSm.setCurrentAndTargetValue      (widthParam->load() * 0.01f);
    duckSm.setCurrentAndTargetValue       (duckParam->load()  * 0.01f);
    bypassSm.setCurrentAndTargetValue     (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    loadFade.setCurrentAndTargetValue     (1.0f);

    duckEnv = 0.0f;

    inWork.setSize  ((int) spec.numChannels, samplesPerBlock);
    wetWork.setSize ((int) spec.numChannels, samplesPerBlock);
}

void ConvoAudioProcessor::releaseResources()
{
    convolution.reset();
    lowShelf.reset();
    highShelf.reset();
    preDelayLine.reset();
    dryDelayLine.reset();
}

void ConvoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numCh      = mainOut.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh <= 0 || numSamples <= 0)
        return;

    // pending load: arm the click-masking output fade and adopt the new dry-delay length
    if (loadFadePending.exchange (false))
    {
        loadFade.setCurrentAndTargetValue (0.0f);
        loadFade.setTargetValue (1.0f);
    }
    {
        const int dd = juce::jlimit (0, ConvolutionEngine::kLongLatency, dryDelaySamples.load());
        if (dd != currentDryDelay)
        {
            dryDelayLine.setDelay ((float) dd);
            currentDryDelay = dd;
        }
    }

    // --- gather the dry input ---
    inWork.setSize (numCh, numSamples, false, false, true);
    bool haveInput = false;
    if (auto* inBus = getBus (true, 0))
    {
        if (inBus->isEnabled())
        {
            auto in = getBusBuffer (buffer, true, 0);
            const int inCh = in.getNumChannels();
            if (inCh > 0)
            {
                haveInput = true;
                for (int ch = 0; ch < numCh; ++ch)
                    inWork.copyFrom (ch, 0, in, juce::jmin (ch, inCh - 1), 0, numSamples);
            }
        }
    }
    if (! haveInput)
        inWork.clear();

    // input meter (pre-delay dry)
    float magIn = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        magIn = juce::jmax (magIn, inWork.getMagnitude (ch, 0, numSamples));
    inputLevel.store (magIn);

    // --- wet = convolved copy of the input ---
    wetWork.setSize (numCh, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        wetWork.copyFrom (ch, 0, inWork, ch, 0, numSamples);
    {
        juce::dsp::AudioBlock<float> wetBlock (wetWork);
        convolution.process (wetBlock);
    }

    // tone (tilt): rebuild shelf coefficients once per block from the smoothed value
    {
        toneSm.setTargetValue (toneParam->load());
        const float tonePct = toneSm.skip (numSamples) * 0.01f;     // -1..1
        const float tiltDb  = tonePct * 12.0f;
        *lowShelf.state  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (
            currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain (-tiltDb));
        *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain ( tiltDb));

        juce::dsp::AudioBlock<float> wetBlock (wetWork);
        juce::dsp::ProcessContextReplacing<float> wctx (wetBlock);
        lowShelf.process (wctx);
        highShelf.process (wctx);
    }

    // width (M/S) on the wet, smoothed per sample (stereo only)
    if (numCh >= 2)
    {
        widthSm.setTargetValue (widthParam->load() * 0.01f);
        auto* L = wetWork.getWritePointer (0);
        auto* R = wetWork.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float w    = widthSm.getNextValue();
            const float mid  = 0.5f * (L[i] + R[i]);
            const float side = 0.5f * (L[i] - R[i]) * w;
            L[i] = mid + side;
            R[i] = mid - side;
        }
    }
    else
    {
        widthSm.skip (numSamples);
    }

    // pre-delay on the wet (creative offset; not reported as latency)
    {
        const float pdSamps = juce::jlimit (0.0f, (float) (maxPreDelaySamples - 2),
                                            preDelayParam->load() * 0.001f * (float) currentSampleRate);
        preDelayLine.setDelay (pdSamps);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = wetWork.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                preDelayLine.pushSample (ch, w[i]);
                w[i] = preDelayLine.popSample (ch);
            }
        }
    }

    // align the dry tap to the engine latency
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = inWork.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            dryDelayLine.pushSample (ch, d[i]);
            d[i] = dryDelayLine.popSample (ch);
        }
    }

    // --- mix: dry + ducked wet, bypass crossfade, output trim, load fade ---
    dryGainSm.setTargetValue    (juce::Decibels::decibelsToGain (dryParam->load(),    -60.0f));
    wetGainSm.setTargetValue    (juce::Decibels::decibelsToGain (wetParam->load(),    -60.0f));
    outputGainSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load(), -60.0f));
    duckSm.setTargetValue       (duckParam->load() * 0.01f);
    bypassSm.setTargetValue     (bypassParam->load() > 0.5f ? 1.0f : 0.0f);

    const float relMs     = duckRelParam->load();
    const float relCoeff  = std::exp (-1.0f / juce::jmax (1.0f, relMs * 0.001f * (float) currentSampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        // mono dry envelope (instant attack, param release) for ducking
        float inAbs = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            inAbs = juce::jmax (inAbs, std::abs (inWork.getSample (ch, i)));
        duckEnv = inAbs > duckEnv ? inAbs : inAbs + (duckEnv - inAbs) * relCoeff;

        const float duckAmt  = duckSm.getNextValue();
        const float duckGain = 1.0f - duckAmt * juce::jlimit (0.0f, 1.0f, duckEnv);

        const float dGain = dryGainSm.getNextValue();
        const float wGain = wetGainSm.getNextValue();
        const float oGain = outputGainSm.getNextValue();
        const float byp   = bypassSm.getNextValue();
        const float fade  = loadFade.getNextValue();

        const float dEff = dGain + byp * (1.0f - dGain);   // bypass -> dry unity
        const float wEff = wGain * (1.0f - byp) * duckGain; // bypass -> wet muted
        const float oEff = oGain + byp * (1.0f - oGain);    // bypass -> output unity

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = inWork.getSample (ch, i);
            const float wet = wetWork.getSample (ch, i);
            mainOut.setSample (ch, i, (dry * dEff + wet * wEff) * oEff * fade);
        }
    }

    // output meter
    float magOut = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        magOut = juce::jmax (magOut, mainOut.getMagnitude (ch, 0, numSamples));
    outputLevel.store (magOut);
}

IRBakeParams ConvoAudioProcessor::currentBakeParams() const
{
    IRBakeParams p;
    p.fadeInMs     = fadeInParam->load();
    const float dec = decayParam->load();
    p.decayOff     = dec >= kDecayOffMs - 0.5f;
    p.decaySeconds = dec * 0.001f;
    p.taperMs      = taperParam->load();
    p.reverse      = reverseParam->load() > 0.5f;
    return p;
}

void ConvoAudioProcessor::timerCallback()
{
    if (! irLibrary.hasIR())
        return;

    const auto cur = currentBakeParams();
    if (cur != lastBaked)
    {
        convolution.rebake (irLibrary.getIR(), irLibrary.getSampleRate(), cur, bakedIR);
        bakedIRSampleRate = irLibrary.getSampleRate();
        lastBaked = cur;
        bakeGeneration.fetch_add (1);
    }
}

bool ConvoAudioProcessor::loadIRFile (const juce::File& file)
{
    if (! irLibrary.loadFile (file))
        return false;

    const auto cur = currentBakeParams();
    const int  lat = convolution.loadIR (irLibrary.getIR(), irLibrary.getSampleRate(), cur, bakedIR);

    bakedIRSampleRate = irLibrary.getSampleRate();
    lastBaked = cur;

    dryDelaySamples.store (lat);
    setLatencySamples (lat);
    loadFadePending.store (true);
    bakeGeneration.fetch_add (1);

    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);
    return true;
}

void ConvoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (irLibrary.getCurrentFile().existsAsFile())
        apvts.state.setProperty ("irPath", irLibrary.getCurrentFile().getFullPathName(), nullptr);

    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ConvoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto path = apvts.state.getProperty ("irPath").toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile())
                    loadIRFile (f);
            }
        }
    }
}

juce::AudioProcessorEditor* ConvoAudioProcessor::createEditor()
{
    return new ConvoAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConvoAudioProcessor();
}
