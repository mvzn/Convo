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

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoftClip.h"
#include "MidSide.h"

#include <cmath>

// shared resonance/Q mapping for the pre-IR In HP/LP corners: 0% = flat (Butterworth 0.707),
// 100% = resonant. Used by both the real-time filter and the IR-bake filter so they match.
static float mapFilterQ (float percent) noexcept
{
    return juce::jmap (juce::jlimit (0.0f, 100.0f, percent) * 0.01f, 0.707f, 6.0f);
}

// max-channel L2 energy of a baked kernel — the same loudness measure auto-level uses, so the
// ratio of two kernels' energies is the convolution-loudness ratio between them. Published per
// generation so the audio thread can keep a rebake loudness-neutral for Wet Comp.
static float kernelEnergy (const juce::AudioBuffer<float>& k) noexcept
{
    double maxE = 0.0;
    for (int ch = 0; ch < k.getNumChannels(); ++ch)
    {
        const float* h = k.getReadPointer (ch);
        double e = 0.0;
        for (int i = 0; i < k.getNumSamples(); ++i)
            e += (double) h[i] * (double) h[i];
        maxE = juce::jmax (maxE, e);
    }
    return (float) maxE;
}

ConvoAudioProcessor::ConvoAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    dryParam      = apvts.getRawParameterValue ("dry");
    wetParam      = apvts.getRawParameterValue ("wet");
    irGainParam   = apvts.getRawParameterValue ("irGain");
    outputParam   = apvts.getRawParameterValue ("output");
    toneParam     = apvts.getRawParameterValue ("tone");
    inHPParam     = apvts.getRawParameterValue ("inHP");
    inLPParam     = apvts.getRawParameterValue ("inLP");
    filterIRParam = apvts.getRawParameterValue ("filterIR");
    msBassParam   = apvts.getRawParameterValue ("msBass");
    preDelayParam = apvts.getRawParameterValue ("preDelay");
    widthParam    = apvts.getRawParameterValue ("width");
    duckParam     = apvts.getRawParameterValue ("duck");
    duckRelParam  = apvts.getRawParameterValue ("duckRelease");
    gateParam     = apvts.getRawParameterValue ("gate");
    polarityParam = apvts.getRawParameterValue ("polarity");
    filterQParam  = apvts.getRawParameterValue ("filterQ");
    irStartParam  = apvts.getRawParameterValue ("irStart");
    irEndParam    = apvts.getRawParameterValue ("irEnd");
    fadeInParam   = apvts.getRawParameterValue ("fadeIn");
    decayParam    = apvts.getRawParameterValue ("decay");
    taperParam    = apvts.getRawParameterValue ("taper");
    stretchParam  = apvts.getRawParameterValue ("stretch");
    dampParam     = apvts.getRawParameterValue ("damp");
    reverseParam  = apvts.getRawParameterValue ("reverse");
    irNormParam   = apvts.getRawParameterValue ("irNorm");
    wetCompParam  = apvts.getRawParameterValue ("wetComp");
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
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -60.0f, "dB"));

    // gain of the IR itself — scales the impulse convolved with the input.
    // applied at the convolved output (identical by linearity, but real-time
    // and avoids re-baking); distinct control from Wet, the convolved-signal mix
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "irGain", 1 }, "IR Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone",
        NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, "%"));

    // pre-IR input filter (2nd-order, 12 dB/oct, shared Q), applied to the wet source before
    // convolution; the extremes (20 Hz HP / 20 kHz LP) are effectively flat
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "inHP", 1 }, "Input HP",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.25f), 20.0f, "Hz"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "inLP", 1 }, "Input LP",
        NormalisableRange<float> (200.0f, 20000.0f, 1.0f, 0.25f), 20000.0f, "Hz"));

    // shared resonance/Q for the In HP & In LP corners: 0% = flat (Q 0.707), up = resonant peak
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "filterQ", 1 }, "Filter Q",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, "%"));

    // pre-IR filter target: off = filter the input at runtime (automatable); on = bake the
    // filter into the kernel (shows in the IR display, cheaper at runtime). Same audio result.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "filterIR", 1 }, "Filter IR", false));

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

    // Gate (gated reverb): gates the wet by the dry input level — when the input falls below the
    // threshold the wet fades out (cuts the tail when the source stops). 0% = off. Real-time stage.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gate", 1 }, "Gate",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, "%"));

    // Wet polarity invert (crossfaded through zero so the flip is click-free).
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "polarity", 1 }, "Polarity", false));

    // --- IR-bake controls ---
    // IR trim: keep only the region between Start and End (fraction of the IR length).
    // Driven by draggable handles on the waveform display; bake params, so they re-window
    // the kernel through the same debounce path as Fade In / Decay / Taper.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "irStart", 1 }, "IR Start",
        NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f, "%",
        AudioProcessorParameter::genericParameter,
        [] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; }));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "irEnd", 1 }, "IR End",
        NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 1.0f, "%",
        AudioProcessorParameter::genericParameter,
        [] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; }));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fadeIn", 1 }, "Fade In",
        NormalisableRange<float> (0.0f, 10000.0f, 1.0f, 0.35f), 0.0f, "ms"));

    // Decay is an amount relative to the baked (trimmed/stretched) length: 0 % = Off (tail as
    // recorded), turning it up shortens the tail starting from that length. See currentBakeParams.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%",
        AudioProcessorParameter::genericParameter,
        [] (float v, int) { return v < 0.5f ? juce::String ("Off") : juce::String (v, 0) + " %"; }));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "taper", 1 }, "Taper",
        NormalisableRange<float> (0.0f, 500.0f, 1.0f), 10.0f, "ms"));

    // time-stretch the IR (resampled in bake): <100% shortens, >100% lengthens. 100% = off.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "stretch", 1 }, "Stretch",
        NormalisableRange<float> (25.0f, 400.0f, 1.0f, 0.5f), 100.0f, "%"));

    // damping (bake): progressive HF rolloff over the tail (air absorption). 0 = off.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "damp", 1 }, "Damp",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, "%"));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "reverse", 1 }, "Reverse", false));

    // off (default) = the IR's raw recorded level, which can convolve 30..45 dB hot on dense
    // full-scale material; on = auto-level (kernel scaled to unity energy)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "irNorm", 1 }, "Norm IR", false));

    // (the final-output soft-clip ceiling is always on — no parameter; see processBlock)

    // adaptive wet gain compensation: continuously trims the wet so its loudness
    // tracks the dry input, held while the input is quiet so reverb tails ring out
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "wetComp", 1 }, "Wet Comp", true));

    // bass-mono crossover: the side is high-passed at this frequency, so content below it folds
    // to the centre. 20 Hz = off; moving the knob up engages the mode (no separate enable param).
    // It's a pure audio-thread stage (high-passes the side post-convolution, no re-bake). ID stays "msBass".
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "msBass", 1 }, "Bass Mono Freq",
        NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.35f), 20.0f, "Hz"));

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
    const int maxSamples = juce::jmax (1, samplesPerBlock);

    // ~8 ms audition crossfade (declicks the solo start/stop); reset any in-flight playback
    auditionFadeStep   = 1.0f / (float) juce::jmax (1, juce::roundToInt (0.008 * sampleRate));
    auditionFade       = 0.0f;
    auditionFadeTarget = 0.0f;
    auditionPlayIdx    = -1;
    auditionPos        = 0.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxSamples;
    spec.numChannels      = (juce::uint32) juce::jmax (1, getMainBusNumOutputChannels());

    convolution.prepare (spec);

    lowShelf.prepare (spec);
    highShelf.prepare (spec);
    // ArrayCoefficients assignment so the coefficient arrays get their final capacity
    // here; the per-block updates in processBlock then never allocate
    *lowShelf.state  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (sampleRate, 700.0f, 0.5f, 1.0f);
    *highShelf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 700.0f, 0.5f, 1.0f);
    lowShelf.reset();
    highShelf.reset();

    inputHP.prepare (spec);
    inputLP.prepare (spec);
    const float fq0 = mapFilterQ (filterQParam->load());
    *inputHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, inHPParam->load(), fq0);
    *inputLP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass  (sampleRate, inLPParam->load(), fq0);
    inputHP.reset();
    inputLP.reset();

    // bass-mono side high-pass: 2nd-order (12 dB/oct, Q = 0.5 / critically damped) — steep
    // enough to genuinely mono the deep bass while Q = 0.5 keeps the phase clean. The mid is
    // never filtered, so center content stays phase-flat.
    sideHP.prepare (spec);
    *sideHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, msBassParam->load(), 0.5f);
    sideHP.reset();

    maxPreDelaySamples = (int) std::ceil (0.5 * sampleRate) + 1;
    preDelayLine.setMaximumDelayInSamples (maxPreDelaySamples);
    preDelayLine.prepare (spec);
    preDelayLine.reset();

    // both engines are zero-latency now (the long engine is non-uniform partitioned with
    // its tail latency internally compensated), so the dry tap needs no alignment delay;
    // the delay line stays in the path at delay 0 (bit-exact pass-through)
    maxDryDelaySamples = 0;
    dryDelayLine.setMaximumDelayInSamples (maxDryDelaySamples + 1);
    dryDelayLine.prepare (spec);
    dryDelayLine.reset();
    currentDryDelay = -1;

    const int lat = convolution.getLatencySamples();
    dryDelaySamples.store (lat);
    setLatencySamples (lat);

    dryGainSm.reset    (sampleRate, 0.02);
    wetGainSm.reset    (sampleRate, 0.02);
    irGainSm.reset     (sampleRate, 0.02);
    outputGainSm.reset (sampleRate, 0.02);
    toneSm.reset       (sampleRate, 0.05);
    widthSm.reset      (sampleRate, 0.02);
    duckSm.reset       (sampleRate, 0.02);
    bypassSm.reset     (sampleRate, 0.01);
    loadFade.reset     (sampleRate, 0.015);
    noIrSm.reset       (sampleRate, 0.05);
    wetCompSm.reset    (sampleRate, 0.25);   // slow follower: a loudness keeper, not a compressor
    inHPSm.reset       (sampleRate, 0.05);
    inLPSm.reset       (sampleRate, 0.05);
    filterQSm.reset    (sampleRate, 0.05);
    polaritySm.reset   (sampleRate, 0.005);   // ~5 ms crossfade through zero on a flip
    msBassSm.reset     (sampleRate, 0.05);
    preDelaySm.reset   (sampleRate, 0.05);   // glide the delay length so modulation doesn't teleport the read pointer

    dryGainSm.setCurrentAndTargetValue    (juce::Decibels::decibelsToGain (dryParam->load(),    -60.0f));
    wetGainSm.setCurrentAndTargetValue    (juce::Decibels::decibelsToGain (wetParam->load(),    -60.0f));
    irGainSm.setCurrentAndTargetValue     (juce::Decibels::decibelsToGain (irGainParam->load(), -60.0f));
    outputGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load(), -60.0f));
    toneSm.setCurrentAndTargetValue       (toneParam->load());
    widthSm.setCurrentAndTargetValue      (widthParam->load() * 0.01f);
    duckSm.setCurrentAndTargetValue       (duckParam->load()  * 0.01f);
    bypassSm.setCurrentAndTargetValue     (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    loadFade.setCurrentAndTargetValue     (1.0f);
    noIrSm.setCurrentAndTargetValue       (convolution.hasIR() ? 0.0f : 1.0f);
    wetCompSm.setCurrentAndTargetValue    (1.0f);
    inHPSm.setCurrentAndTargetValue       (inHPParam->load());
    inLPSm.setCurrentAndTargetValue       (inLPParam->load());
    filterQSm.setCurrentAndTargetValue    (filterQParam->load());
    polaritySm.setCurrentAndTargetValue   (polarityParam->load() > 0.5f ? -1.0f : 1.0f);
    msBassSm.setCurrentAndTargetValue     (msBassParam->load());
    preDelaySm.setCurrentAndTargetValue   (juce::jlimit (0.0f, (float) (maxPreDelaySamples - 2),
                                                         preDelayParam->load() * 0.001f * (float) sampleRate));

    duckEnv         = 0.0f;
    gateGain        = 1.0f;
    wetCompTarget   = 1.0f;
    bakeGenSeen       = bakeGeneration.load();      // adopt the current kernel without a spurious swap-snap
    audioKernelEnergy = bakedKernelEnergy.load();
    prevFilterInput = true;
    prevToneActive  = prevInHpActive = prevInLpActive = prevBassActive
                    = prevPreDelayActive = true;

    inWork.setSize  ((int) spec.numChannels, maxSamples);
    wetWork.setSize ((int) spec.numChannels, maxSamples);
    duckGainBuf.assign ((size_t) maxSamples, 1.0f);
    gateGainBuf.assign ((size_t) maxSamples, 1.0f);
}

void ConvoAudioProcessor::releaseResources()
{
    convolution.reset();
    lowShelf.reset();
    highShelf.reset();
    inputHP.reset();
    inputLP.reset();
    sideHP.reset();
    preDelayLine.reset();
    dryDelayLine.reset();
}

void ConvoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numCh = juce::jmin (mainOut.getNumChannels(), inWork.getNumChannels());
    // clamp to the prepared size instead of growing the work buffers — an oversized
    // block is a host-contract violation, and growing would allocate on this thread.
    // Any trailing samples pass through untouched (in-place buffer).
    const int numSamples = juce::jmin (buffer.getNumSamples(), inWork.getNumSamples());
    if (numCh <= 0 || numSamples <= 0)
        return;

    // pending load: arm the click-masking output fade and adopt the new dry-delay
    // length. The fade flag is checked first and stored first on the message thread,
    // so a delay jump can never be observed with the mask unarmed.
    if (loadFadePending.exchange (false))
    {
        loadFade.setCurrentAndTargetValue (0.0f);
        loadFade.setTargetValue (1.0f);
    }
    {
        const int dd = juce::jlimit (0, maxDryDelaySamples, dryDelaySamples.load());
        if (dd != currentDryDelay)
        {
            dryDelayLine.setDelay ((float) dd);
            currentDryDelay = dd;
        }
    }

    // --- gather the dry input ---
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

    // input meter (pre-delay dry): block peak with a decaying hold so the UI poll
    // doesn't miss peaks between its 30 Hz reads
    float magIn = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        magIn = juce::jmax (magIn, inWork.getMagnitude (ch, 0, numSamples));
    inputLevel.store (juce::jmax (magIn, inputLevel.load() * 0.85f));

    // dry-reference loudness for adaptive wet comp — only computed when Wet Comp is on
    const bool wetCompActive = wetCompParam->load() > 0.5f;
    float dryRms = 0.0f;
    if (wetCompActive)
    {
        float drySumSq = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float r = inWork.getRMSLevel (ch, 0, numSamples);
            drySumSq += r * r;
        }
        dryRms = std::sqrt (drySumSq / (float) numCh);
    }

    // --- wet = convolved copy of the input ---
    for (int ch = 0; ch < numCh; ++ch)
        wetWork.copyFrom (ch, 0, inWork, ch, 0, numSamples);

    auto wetBlock = juce::dsp::AudioBlock<float> (wetWork)
                        .getSubsetChannelBlock (0, (size_t) numCh)
                        .getSubBlock (0, (size_t) numSamples);

    // pre-IR input filter: first-order HP + LP (6 dB/oct) on the wet source only, so the
    // dry tap stays unfiltered. Runs only when the filter targets the input (otherwise it
    // is baked into the kernel). Reset on re-engage so stale state can't pop — the toggle
    // itself is masked by the load fade.
    const bool filterOnInput = filterInput.load();
    if (filterOnInput != prevFilterInput)
    {
        if (filterOnInput) { inputHP.reset(); inputLP.reset(); }
        prevFilterInput = filterOnInput;
    }
    if (filterOnInput)
    {
        inHPSm.setTargetValue (inHPParam->load());
        inLPSm.setTargetValue (inLPParam->load());
        filterQSm.setTargetValue (filterQParam->load());
        const float hp = inHPSm.skip (numSamples);
        const float lp = inLPSm.skip (numSamples);
        const float fq = mapFilterQ (filterQSm.skip (numSamples));   // 0..100% -> Q
        // skip each filter while it sits at its flat extreme (20 Hz / 20 kHz); reset on re-engage
        const bool hpActive = inHPSm.isSmoothing() || hp > 21.0f;
        const bool lpActive = inLPSm.isSmoothing() || lp < 19900.0f;
        if (hpActive != prevInHpActive) { if (hpActive) inputHP.reset(); prevInHpActive = hpActive; }
        if (lpActive != prevInLpActive) { if (lpActive) inputLP.reset(); prevInLpActive = lpActive; }
        juce::dsp::ProcessContextReplacing<float> fctx (wetBlock);
        if (hpActive) { *inputHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (currentSampleRate, hp, fq); inputHP.process (fctx); }
        if (lpActive) { *inputLP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass  (currentSampleRate, lp, fq); inputLP.process (fctx); }
    }
    else
    {
        inHPSm.skip (numSamples);   // keep the smoothers tracking so re-engaging starts in place
        inLPSm.skip (numSamples);
        filterQSm.skip (numSamples);
    }

    // Pre-convolution dynamics, both keyed off the dry input peak:
    //  - Gate (gated reverb): gates the dry signal feeding the IR — cuts the convolution input when
    //    the input drops below the threshold (the existing tail still rings out). Fast attack;
    //    release = the Duck Release setting. Applied to the convolution input here.
    //  - Duck: the per-sample gain is filled here (envelope keyed off the input) but applied later,
    //    to the wet output, in the mix loop.
    {
        duckSm.setTargetValue (duckParam->load() * 0.01f);
        const float dRelMs = duckRelParam->load();
        const float dRelCo = std::exp (-1.0f / juce::jmax (1.0f, dRelMs * 0.001f * (float) currentSampleRate));

        const float gatePct = gateParam->load();
        const bool  gateOn  = gatePct > 0.5f;
        const float gateThr = juce::Decibels::decibelsToGain (juce::jmap (gatePct * 0.01f, 0.0f, 1.0f, -60.0f, -6.0f));
        const float gateAtt = std::exp (-1.0f / juce::jmax (1.0f, 0.002f * (float) currentSampleRate));
        const float gateRel = dRelCo;   // gate release tracks the Duck Release

        float maxGateGR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float inAbs = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                inAbs = juce::jmax (inAbs, std::abs (inWork.getSample (ch, i)));
            duckEnv = inAbs > duckEnv ? inAbs : inAbs + (duckEnv - inAbs) * dRelCo;
            const float dAmt = duckSm.getNextValue();
            duckGainBuf[(size_t) i] = 1.0f - dAmt * juce::jlimit (0.0f, 1.0f, duckEnv);

            const float gTarget = (! gateOn || inAbs > gateThr) ? 1.0f : 0.0f;
            gateGain = gTarget + (gateGain - gTarget) * (gTarget > gateGain ? gateAtt : gateRel);
            gateGainBuf[(size_t) i] = gateGain;
            maxGateGR = juce::jmax (maxGateGR, 1.0f - gateGain);
        }

        // gate the convolution input (unity / bit-exact no-op when the gate is off)
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = wetWork.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                w[i] *= gateGainBuf[(size_t) i];
        }
        gateGR.store (juce::jmax (maxGateGR, gateGR.load() * 0.85f));   // decaying peak hold for the Gate-knob indicator
    }

    convolution.process (wetBlock);

    // Post-convolution wet shaping, in signal-flow order: pre-delay -> tone -> bass mono ->
    // width. These stages are all linear, so the order is sound-invariant — it just mirrors
    // the mental model (the reverb arrives after the pre-delay, then we colour it, then place
    // its stereo image).

    // pre-delay on the wet (creative offset; not reported as latency). Skipped at ~0 ms
    // (a no-op passthrough); the line is reset on re-engage so it can't pop stale samples.
    {
        const float pdSamps = juce::jlimit (0.0f, (float) (maxPreDelaySamples - 2),
                                            preDelayParam->load() * 0.001f * (float) currentSampleRate);
        const bool pdActive = pdSamps > 0.5f;
        // on re-engage, clear the line and snap the smoother so the glide starts at
        // the current setting (the load/empty line masks the jump, no click)
        if (pdActive != prevPreDelayActive)
        {
            if (pdActive) { preDelayLine.reset(); preDelaySm.setCurrentAndTargetValue (pdSamps); }
            prevPreDelayActive = pdActive;
        }
        preDelaySm.setTargetValue (pdSamps);
        if (pdActive)
        {
            float* w[2] = { nullptr, nullptr };
            for (int ch = 0; ch < numCh; ++ch)
                w[ch] = wetWork.getWritePointer (ch);

            // setDelay applies to all channels, so ramp once per sample (channels inner)
            // — the read pointer glides instead of teleporting at the block boundary
            for (int i = 0; i < numSamples; ++i)
            {
                preDelayLine.setDelay (preDelaySm.getNextValue());
                for (int ch = 0; ch < numCh; ++ch)
                {
                    preDelayLine.pushSample (ch, w[ch][i]);
                    w[ch][i] = preDelayLine.popSample (ch);
                }
            }
        }
        else
        {
            preDelaySm.skip (numSamples);
        }
    }

    // tone (tilt): rebuild shelf coefficients once per block from the smoothed value.
    // ArrayCoefficients + Coefficients::operator= reuse the existing array storage,
    // so this is allocation-free (unlike makeLowShelf(), which news a Coefficients).
    {
        toneSm.setTargetValue (toneParam->load());
        const float tonePct = toneSm.skip (numSamples) * 0.01f;     // -1..1
        // tilt is flat at tone == 0 (unity shelves) -> skip both passes; reset on re-engage
        const bool toneActive = toneSm.isSmoothing() || std::abs (tonePct) > 0.0005f;
        if (toneActive != prevToneActive) { if (toneActive) { lowShelf.reset(); highShelf.reset(); } prevToneActive = toneActive; }
        if (toneActive)
        {
            const float tiltDb = tonePct * 12.0f;
            *lowShelf.state  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (
                currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain (-tiltDb));
            *highShelf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
                currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain ( tiltDb));

            juce::dsp::ProcessContextReplacing<float> wctx (wetBlock);
            lowShelf.process (wctx);
            highShelf.process (wctx);
        }
    }

    // bass mono: collapse the wet below the crossover to mono, leave the rest stereo. Encode
    // M/S, high-pass the side at the crossover (2nd-order, Q = 0.5 / critically damped, 12 dB/oct
    // — steep enough that the deep bass is genuinely mono and that Width, which runs next on this
    // already-high-passed side, can't re-widen it; Q = 0.5 keeps the phase clean for the order).
    // Above the crossover the M/S round-trip is sample-exact (identical to the feature off). Works
    // on any IR: the wet's stereo image comes from the input, so a mono IR collapses to mono too.
    // Bass Mono engages purely from the crossover: 20 Hz = off (bit-exact, skipped), moving the
    // knob off 20 Hz engages it. No separate enable param; the side filter resets on re-engage.
    {
        const bool bassMonoOn = numCh >= 2;
        msBassSm.setTargetValue (msBassParam->load());
        const float bassFc = msBassSm.skip (numSamples);
        const bool bassActive = bassMonoOn && (msBassSm.isSmoothing() || bassFc > 20.5f);
        if (bassActive != prevBassActive) { if (bassActive) sideHP.reset(); prevBassActive = bassActive; }
        if (bassActive)
        {
            convo::msEncode (wetWork.getWritePointer (0), wetWork.getWritePointer (1), numSamples);
            *sideHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (currentSampleRate, bassFc, 0.5f);
            auto sideBlock = wetBlock.getSingleChannelBlock (1);
            juce::dsp::ProcessContextReplacing<float> sctx (sideBlock);
            sideHP.process (sctx);
            convo::msDecode (wetWork.getWritePointer (0), wetWork.getWritePointer (1), numSamples);
        }
    }

    // width (M/S) on the wet, smoothed per sample (stereo only). Skipped when steady at
    // 100%: there w == 1 and mid +/- side reconstructs L/R bit-exactly, so the loop is a
    // no-op pass we can avoid (the common default case).
    if (numCh >= 2)
    {
        widthSm.setTargetValue (widthParam->load() * 0.01f);
        if (widthSm.isSmoothing() || ! juce::approximatelyEqual (widthSm.getTargetValue(), 1.0f))
        {
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
    }
    else
    {
        widthSm.skip (numSamples);
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

    // kernel-swap compensation: a rebake changes the kernel's energy in ~50 ms, but the 0.25 s
    // follower lags, so the wet would overshoot for a beat (the "blows out for a second" on a
    // decay/damp/stretch tweak). On a new generation, counter-scale the follower's *current* value
    // by sqrt(oldEnergy/newEnergy) so the audible wet is continuous across the swap; the RMS
    // follower then carries on from the matched point. Feed-forward from the published energies,
    // so it also covers tweaking while silent. No-op when Wet Comp is off (raw level steps as
    // before) and when energy is unchanged (e.g. Norm IR on -> both kernels ~unit energy).
    {
        const int bg = bakeGeneration.load();
        if (bg != bakeGenSeen)
        {
            bakeGenSeen = bg;
            const float newE = bakedKernelEnergy.load();
            if (wetCompActive && audioKernelEnergy > 0.0f && newE > 0.0f)
            {
                const float makeup = std::sqrt (audioKernelEnergy / newE);
                const float g = juce::jlimit (0.125f, 8.0f, wetCompSm.getCurrentValue() * makeup);
                wetCompSm.setCurrentAndTargetValue (g);
                wetCompTarget = g;   // hold the snapped value if the follower freezes (input quiet)
            }
            audioKernelEnergy = newE;
        }
    }

    // adaptive wet gain compensation (dry-referenced, tail-safe). Both RMS passes are skipped
    // when Wet Comp is off (gate computed above); chase the dry/wet ratio only while the input
    // is live, hold otherwise so reverb tails ring out. The 0.25 s smoother does the rest.
    if (wetCompActive)
    {
        float wetSumSq = 0.0f;     // wet loudness (post wet-chain, pre-gain)
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float r = wetWork.getRMSLevel (ch, 0, numSamples);
            wetSumSq += r * r;
        }
        const float wetRms = std::sqrt (wetSumSq / (float) numCh);

        if (dryRms > 0.0018f && wetRms > 1.0e-6f)                       // gate ~ -55 dBFS RMS
            wetCompTarget = juce::jlimit (0.125f, 8.0f, dryRms / wetRms); // clamp to +/-18 dB
        wetCompSm.setTargetValue (wetCompTarget);
    }
    else
    {
        wetCompSm.setTargetValue (1.0f);
    }

    // --- mix: dry + ducked wet, bypass crossfade, output trim, load fade ---
    polaritySm.setTargetValue   (polarityParam->load() > 0.5f ? -1.0f : 1.0f);
    dryGainSm.setTargetValue    (juce::Decibels::decibelsToGain (dryParam->load(),    -60.0f));
    wetGainSm.setTargetValue    (juce::Decibels::decibelsToGain (wetParam->load(),    -60.0f));
    irGainSm.setTargetValue     (juce::Decibels::decibelsToGain (irGainParam->load(), -60.0f));
    outputGainSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load(), -60.0f));
    bypassSm.setTargetValue     (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    noIrSm.setTargetValue       (convolution.hasIR() ? 0.0f : 1.0f);   // no IR -> behave like bypass

    float maxGR = 0.0f;   // most ducking gain reduction this block, for the Duck-knob probe
    for (int i = 0; i < numSamples; ++i)
    {
        // duck the wet output (the gain was precomputed before the convolution; gate is always pre)
        const float duckGain = duckGainBuf[(size_t) i];

        const float dGain = dryGainSm.getNextValue();
        const float wGain = wetGainSm.getNextValue();
        const float iGain = irGainSm.getNextValue();   // IR Gain: gain of the IR convolved with the input
        const float cGain = wetCompSm.getNextValue();   // adaptive wet gain compensation
        const float oGain = outputGainSm.getNextValue();
        const float pol   = polaritySm.getNextValue();   // wet polarity sign (crossfades through 0)
        // no-IR state behaves exactly like bypass: dry at unity, wet muted —
        // a freshly inserted Convo must never silence the track
        const float byp   = juce::jmax (bypassSm.getNextValue(), noIrSm.getNextValue());
        const float fade  = loadFade.getNextValue();

        // probe the live ducking gain reduction (from the precomputed gain, so the probe reads the
        // same pre/post routing); zeroed when bypassed / no IR (the wet is muted there)
        maxGR = juce::jmax (maxGR, (1.0f - duckGainBuf[(size_t) i]) * (1.0f - byp));

        const float dEff = dGain + byp * (1.0f - dGain);   // bypass -> dry unity
        const float wEff = wGain * (1.0f - byp) * duckGain * pol; // bypass -> wet muted; pol inverts (gate is pre-conv)
        const float oEff = oGain + byp * (1.0f - oGain);    // bypass -> output unity

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = inWork.getSample (ch, i);
            const float wet = wetWork.getSample (ch, i) * iGain * cGain;   // IR Gain then wet comp
            float s = (dry * dEff + wet * wEff) * oEff * fade;
            s = convo::softClip (s);   // always-on soft-clip ceiling (transparent below -2.5 dBFS)
            mainOut.setSample (ch, i, s);
        }
    }

    duckGR.store (juce::jmax (maxGR, duckGR.load() * 0.85f));   // decaying peak hold, same as the meters

    // IR audition: while playing, crossfade the output over to the IR (resampled to the host rate),
    // soloing it so you hear the kernel clearly. A short fade in/out declicks the start and stop —
    // hard-switching from the mix to the IR's onset used to pop. Reads a pre-filled buffer, no alloc.
    {
        const int gen = auditionGen.load (std::memory_order_acquire);
        if (gen != auditionGenSeen)
        {
            auditionGenSeen = gen;
            const int newIdx = auditionReadIdx.load (std::memory_order_acquire);
            if (newIdx >= 0)                 // start / re-trigger: play from the top, fade in
            {
                auditionPlayIdx    = newIdx;
                auditionPos        = 0.0;
                auditionFadeTarget = 1.0f;
            }
            else                             // stop: fade out (keep reading the source until it lands)
            {
                auditionFadeTarget = 0.0f;
            }
        }

        if (auditionPlayIdx >= 0 && (auditionFade > 0.0f || auditionFadeTarget > 0.0f))
        {
            const auto&  src   = auditionBuf[auditionPlayIdx];
            const int    srcN  = src.getNumSamples();
            const int    srcCh = src.getNumChannels();
            const double ratio = auditionBufRate[auditionPlayIdx] / currentSampleRate;

            for (int i = 0; i < numSamples; ++i)
            {
                if      (auditionFade < auditionFadeTarget) auditionFade = juce::jmin (auditionFadeTarget, auditionFade + auditionFadeStep);
                else if (auditionFade > auditionFadeTarget) auditionFade = juce::jmax (auditionFadeTarget, auditionFade - auditionFadeStep);

                const bool  haveSrc = auditionPos < (double) srcN;
                const int   i0 = haveSrc ? (int) auditionPos : 0;
                const int   i1 = juce::jmin (i0 + 1, srcN - 1);
                const float fr = haveSrc ? (float) (auditionPos - (double) i0) : 0.0f;

                for (int ch = 0; ch < numCh; ++ch)
                {
                    const int   sc = juce::jmin (ch, srcCh - 1);
                    const float a  = haveSrc ? (src.getSample (sc, i0) * (1.0f - fr) + src.getSample (sc, i1) * fr) : 0.0f;
                    const float mixed = mainOut.getSample (ch, i) * (1.0f - auditionFade) + a * auditionFade;
                    mainOut.setSample (ch, i, convo::softClip (mixed));
                }

                if (haveSrc) auditionPos += ratio;
            }

            if (auditionPos >= (double) srcN) auditionFadeTarget = 0.0f;              // reached the end -> fade out
            if (auditionFadeTarget <= 0.0f && auditionFade <= 0.0f) auditionPlayIdx = -1;   // faded out -> idle
        }

        // "playing" for the editor: soloing or fading in, but not while fading back out to a stop
        auditionActive.store (auditionPlayIdx >= 0 && auditionFadeTarget > 0.0f);
    }

    // output meter (decaying peak hold, same as the input meter)
    float magOut = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        magOut = juce::jmax (magOut, mainOut.getMagnitude (ch, 0, numSamples));
    outputLevel.store (juce::jmax (magOut, outputLevel.load() * 0.85f));
}

void ConvoAudioProcessor::startAudition (bool baked)
{
    // message thread: copy the chosen source into the buffer the audio thread is NOT reading,
    // then publish it. Both source buffers live on the message thread (bake + decode also run
    // there, serialized by the message loop), so reading them here is race-free.
    const juce::AudioBuffer<float>* src = nullptr;
    double srcRate = currentSampleRate;
    if (baked)
    {
        if (audioBakeScratch.getNumSamples() > 0) { src = &audioBakeScratch; srcRate = bakedIRSampleRate; }
    }
    else if (irLibrary.hasIR())
    {
        src = &irLibrary.getIR();
        srcRate = irLibrary.getSampleRate();
    }
    if (src == nullptr)
        return;

    const int idx = auditionWriteIdx ^ 1;
    auditionBuf[idx].makeCopyOf (*src);
    auditionBufRate[idx] = srcRate > 0.0 ? srcRate : currentSampleRate;
    auditionWriteIdx = idx;
    auditionReadIdx.store (idx, std::memory_order_release);        // buffer is filled before this publish
    auditionGen.fetch_add (1, std::memory_order_release);         // bump last so the audio thread re-reads
}

void ConvoAudioProcessor::stopAudition()
{
    auditionReadIdx.store (-1, std::memory_order_release);
    auditionGen.fetch_add (1, std::memory_order_release);
}

IRBakeParams ConvoAudioProcessor::currentBakeParams() const
{
    IRBakeParams p;
    p.startFrac    = irStartParam->load();
    p.endFrac      = irEndParam->load();
    p.fadeInMs     = fadeInParam->load();
    const float dec = decayParam->load();            // 0..100 %, 0 = Off
    p.decayOff      = dec < 0.5f;
    // map the amount to the -60 dB point as a fraction of the baked length: 0 % -> 1.0 (full
    // length, just engaging), 100 % -> kDecayMinFrac (shortest tail). bake() scales this by len.
    p.decayFraction = 1.0f - (dec * 0.01f) * (1.0f - kDecayMinFrac);
    p.taperMs      = taperParam->load();
    p.stretch      = stretchParam->load() * 0.01f;   // % -> factor
    p.dampAmt      = dampParam->load() * 0.01f;      // % -> 0..1
    p.reverse      = reverseParam->load() > 0.5f;
    p.autoLevel    = irNormParam->load() > 0.5f;   // Norm IR on => auto-level; off (default) => raw
    p.filterIR     = filterIRParam->load() > 0.5f;
    p.inHPHz       = inHPParam->load();
    p.inLPHz       = inLPParam->load();
    p.inFilterQ    = mapFilterQ (filterQParam->load());
    return p;
}

double ConvoAudioProcessor::getMaxFadeInMs() const
{
    if (! irLibrary.hasIR())
        return 0.0;
    return ConvolutionEngine::maxFadeInMs (irLibrary.getIR().getNumSamples(),
                                           irLibrary.getSampleRate(),
                                           currentBakeParams());
}

void ConvoAudioProcessor::timerCallback()
{
    // state restore lands here so file IO and IR baking stay on the message thread
    if (pendingIRLoad.exchange (false))
    {
        juce::String path;
        {
            const juce::ScopedLock l (pendingIRPathLock);
            path.swapWith (pendingIRPath);
        }
        const juce::File f = resolveIRPath (path);
        if (f.existsAsFile())
            loadIRFile (f);
    }

    if (! irLibrary.hasIR())
        return;

    // debounce: rebake only once the bake params have been stable for a full tick,
    // so dragging a knob doesn't re-window the whole IR 30 times a second
    const auto cur = currentBakeParams();
    if (cur != lastBaked && cur == lastSeenBakeParams)
    {
        const bool targetChanged = (cur.filterIR != lastBaked.filterIR);
        // The filter-target change flips audio-thread routing; Reverse and Norm IR change the
        // kernel abruptly (reversal / a big level jump). All three are discrete toggles, so arm
        // the output fade to mask the swap and keep the toggle click-free. The continuous knobs
        // (fade-in/decay/taper) are dragged, so they keep relying on JUCE's seamless kernel
        // crossfade — arming the fade on every drag tick would pump the output. (Bass Mono is a
        // pure audio-thread stage, not a bake param, so it never re-bakes or arms the fade.)
        const bool toggleChanged = targetChanged
                                 || (cur.reverse   != lastBaked.reverse)
                                 || (cur.autoLevel != lastBaked.autoLevel);

        // The display shows the FULL IR as the (blurred) backdrop, with the trimmed+shaped kernel
        // drawn inside the selection. The backdrop only re-bakes when a NON-trim param changes; the
        // generation bumps on every commit so the editor rebuilds the kernel layer — including on a
        // trim-release, but never during a drag (the drag doesn't write the params).
        auto curDisp  = cur;       curDisp.startFrac  = 0.0f; curDisp.endFrac  = 1.0f;
        auto lastDisp = lastBaked; lastDisp.startFrac = 0.0f; lastDisp.endFrac = 1.0f;
        const bool displayChanged = (curDisp != lastDisp);

        convolution.rebake (irLibrary.getIR(), irLibrary.getSampleRate(), cur, audioBakeScratch);
        bakedIRSampleRate = irLibrary.getSampleRate();
        tailSeconds.store ((float) (audioBakeScratch.getNumSamples() / juce::jmax (1.0, bakedIRSampleRate) + 0.5));
        lastBaked = cur;

        if (displayChanged)
            bakedIR = ConvolutionEngine::bake (irLibrary.getIR(), irLibrary.getSampleRate(), curDisp);
        bakedKernelEnergy.store (kernelEnergy (audioBakeScratch));   // publish before the gen bump (see processBlock)
        bakeGeneration.fetch_add (1);   // every commit -> editor rebuilds the kernel layer (incl. trim-release)

        if (targetChanged) filterInput.store (! cur.filterIR);
        if (toggleChanged) loadFadePending.store (true);
    }
    lastSeenBakeParams = cur;
}

bool ConvoAudioProcessor::loadIRFile (const juce::File& file)
{
    if (! irLibrary.loadFile (file))
        return false;

    const auto cur = currentBakeParams();
    const int  lat = convolution.loadIR (irLibrary.getIR(), irLibrary.getSampleRate(), cur, audioBakeScratch);

    bakedIRSampleRate  = irLibrary.getSampleRate();
    lastBaked          = cur;
    lastSeenBakeParams = cur;
    filterInput.store (! cur.filterIR);   // kernel is baked to the current pre-IR filter target
    tailSeconds.store ((float) (audioBakeScratch.getNumSamples() / juce::jmax (1.0, bakedIRSampleRate) + 0.5));

    // display IR is the FULL (untrimmed) picture; trim shows as a selection overlay in the editor
    {
        auto curDisp = cur; curDisp.startFrac = 0.0f; curDisp.endFrac = 1.0f;
        bakedIR = ConvolutionEngine::bake (irLibrary.getIR(), irLibrary.getSampleRate(), curDisp);
    }

    loadFadePending.store (true);      // arm the click mask before publishing the delay
    dryDelaySamples.store (lat);
    setLatencySamples (lat);
    bakedKernelEnergy.store (kernelEnergy (audioBakeScratch));   // publish before the gen bump (see processBlock)
    bakeGeneration.fetch_add (1);

    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);
    return true;
}

juce::File ConvoAudioProcessor::getPresetsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile ("Convo").getChildFile ("Presets");
    if (! folder.isDirectory())
        folder.createDirectory();   // harmless if it already exists or can't be created
    return folder;
}

juce::File ConvoAudioProcessor::getIRsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile ("Convo").getChildFile ("IRs");
    if (! folder.isDirectory())
        folder.createDirectory();
    return folder;
}

juce::File ConvoAudioProcessor::resolveIRPath (const juce::String& storedPath)
{
    if (storedPath.isEmpty())
        return {};

    const juce::File direct (storedPath);
    if (direct.existsAsFile())
        return direct;                                        // exact path on this machine — unchanged

    // portability fallback: same filename in the shared IRs folder. Only reached when the
    // stored absolute path is gone (e.g. a preset moved between machines), so it never
    // changes behaviour for an in-place session that already resolves.
    const auto byName = getIRsFolder().getChildFile (direct.getFileName());
    if (byName.existsAsFile())
        return byName;

    return direct;   // not found anywhere; the caller's existsAsFile() check fails as before
}

juce::Array<juce::File> ConvoAudioProcessor::getPresetFiles() const
{
    juce::Array<juce::File> files;
    const auto folder = getPresetsFolder();
    if (folder.isDirectory())
        files = folder.findChildFiles (juce::File::findFiles, false, "*.xml");

    // case-insensitive name sort so the prev/next arrows step in a stable, human order
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b)
        { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()); } };
    ByName comparator;
    files.sort (comparator, true);
    return files;
}

bool ConvoAudioProcessor::savePreset (const juce::String& presetName)
{
    const auto name = juce::File::createLegalFileName (presetName).trim();
    if (name.isEmpty())
        return false;

    auto file = getPresetsFolder().getChildFile (name + ".xml");
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file);
    return false;
}

bool ConvoAudioProcessor::loadPreset (const juce::File& presetFile)
{
    if (! presetFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (presetFile);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // loadPreset is always on the message thread, so load the IR right away (unlike
    // setStateInformation, which may run on a worker and defers to the timer)
    const auto path = apvts.state.getProperty ("irPath").toString();
    if (path.isNotEmpty())
    {
        const juce::File f = resolveIRPath (path);
        if (f.existsAsFile())
            loadIRFile (f);
    }
    return true;
}

void ConvoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // irPath is kept up to date by loadIRFile (message thread); don't touch the
    // ValueTree here — hosts may call getState from a worker thread
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

            // hosts may call setState off the message thread; the actual decode and
            // bake happen in timerCallback
            const auto path = apvts.state.getProperty ("irPath").toString();
            if (path.isNotEmpty())
            {
                {
                    const juce::ScopedLock l (pendingIRPathLock);
                    pendingIRPath = path;
                }
                pendingIRLoad.store (true);
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
