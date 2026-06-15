#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoftClip.h"
#include "MidSide.h"

#include <cmath>

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
    msParam       = apvts.getRawParameterValue ("ms");
    msBassParam   = apvts.getRawParameterValue ("msBass");
    preDelayParam = apvts.getRawParameterValue ("preDelay");
    widthParam    = apvts.getRawParameterValue ("width");
    duckParam     = apvts.getRawParameterValue ("duck");
    duckRelParam  = apvts.getRawParameterValue ("duckRelease");
    fadeInParam   = apvts.getRawParameterValue ("fadeIn");
    decayParam    = apvts.getRawParameterValue ("decay");
    taperParam    = apvts.getRawParameterValue ("taper");
    reverseParam  = apvts.getRawParameterValue ("reverse");
    rawLevelParam = apvts.getRawParameterValue ("irRaw");
    clipGuardParam = apvts.getRawParameterValue ("clipGuard");
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
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -12.0f, "dB"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "wet", 1 }, "Wet",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -12.0f, "dB"));

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

    // pre-IR input filter (first-order, 6 dB/oct), applied to the wet source before
    // convolution; the extremes (20 Hz HP / 20 kHz LP) are effectively flat
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "inHP", 1 }, "Input HP",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.25f), 20.0f, "Hz"));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "inLP", 1 }, "Input LP",
        NormalisableRange<float> (200.0f, 20000.0f, 1.0f, 0.25f), 20000.0f, "Hz"));

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

    // --- IR-bake controls ---
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fadeIn", 1 }, "Fade In",
        NormalisableRange<float> (0.0f, 10000.0f, 1.0f, 0.35f), 0.0f, "ms"));

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

    // off = auto-level (kernel scaled to unity energy); on = the IR's raw recorded
    // level, which can convolve 30..45 dB hot on dense full-scale material
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "irRaw", 1 }, "Raw IR Level", false));

    // final-output soft-clip ceiling; transparent until the signal runs hot
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "clipGuard", 1 }, "Clip Guard", true));

    // adaptive wet gain compensation: continuously trims the wet so its loudness
    // tracks the dry input, held while the input is quiet so reverb tails ring out
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "wetComp", 1 }, "Wet Comp", true));

    // mid/side convolution: bake the kernel to M/S and convolve mid-with-mid,
    // side-with-side (re-bakes the IR; masked by the load fade)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "ms", 1 }, "Mid/Side", false));

    // bass-mono crossover (M/S mode only): high-pass the side so content below the
    // cutoff collapses to mono. 20 Hz = flat (off).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "msBass", 1 }, "Bass Mono",
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
    *inputHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sampleRate, inHPParam->load());
    *inputLP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass  (sampleRate, inLPParam->load());
    inputHP.reset();
    inputLP.reset();

    sideHP.prepare (spec);
    *sideHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sampleRate, msBassParam->load());
    sideHP.reset();

    maxPreDelaySamples = (int) std::ceil (0.5 * sampleRate) + 1;
    preDelayLine.setMaximumDelayInSamples (maxPreDelaySamples);
    preDelayLine.prepare (spec);
    preDelayLine.reset();

    // the long engine's real latency grows with the host block size, so the dry
    // delay must be sized for it — and the true value is only known here
    maxDryDelaySamples = ConvolutionEngine::longEngineLatencyForBlockSize (maxSamples);
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
    msBassSm.reset     (sampleRate, 0.05);
    clipGuardSm.reset  (sampleRate, 0.01);

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
    msBassSm.setCurrentAndTargetValue     (msBassParam->load());
    clipGuardSm.setCurrentAndTargetValue  (clipGuardParam->load() > 0.5f ? 1.0f : 0.0f);

    duckEnv         = 0.0f;
    wetCompTarget   = 1.0f;
    prevMsEncode    = false;
    prevFilterInput = true;

    inWork.setSize  ((int) spec.numChannels, maxSamples);
    wetWork.setSize ((int) spec.numChannels, maxSamples);
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
        *inputHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (
            currentSampleRate, inHPSm.skip (numSamples));
        *inputLP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (
            currentSampleRate, inLPSm.skip (numSamples));
        juce::dsp::ProcessContextReplacing<float> fctx (wetBlock);
        inputHP.process (fctx);
        inputLP.process (fctx);
    }
    else
    {
        inHPSm.skip (numSamples);   // keep the smoothers tracking so re-engaging starts in place
        inLPSm.skip (numSamples);
    }

    // mid/side mode: encode the wet source to M = (L+R)/2, S = (L-R)/2 so channel-wise
    // convolution with the M/S-baked kernel becomes mid-with-mid, side-with-side.
    // msActive is published by the message thread only once the M/S kernel is loaded.
    const bool msEncode = msActive.load() && numCh >= 2;
    if (msEncode != prevMsEncode)
    {
        if (msEncode) sideHP.reset();   // clean engage; the M/S toggle is masked by the load fade
        prevMsEncode = msEncode;
    }
    if (msEncode)
    {
        convo::msEncode (wetWork.getWritePointer (0), wetWork.getWritePointer (1), numSamples);

        // bass-mono crossover: high-pass the side so content below the cutoff collapses to
        // mono (the lows stay in the mid). 20 Hz = flat. Smoothed, first-order to match.
        msBassSm.setTargetValue (msBassParam->load());
        *sideHP.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (
            currentSampleRate, msBassSm.skip (numSamples));
        auto sideBlock = wetBlock.getSingleChannelBlock (1);
        juce::dsp::ProcessContextReplacing<float> sctx (sideBlock);
        sideHP.process (sctx);
    }
    else
    {
        msBassSm.skip (numSamples);
    }

    convolution.process (wetBlock);

    if (msEncode)   // decode mid/side back to L/R
        convo::msDecode (wetWork.getWritePointer (0), wetWork.getWritePointer (1), numSamples);

    // tone (tilt): rebuild shelf coefficients once per block from the smoothed value.
    // ArrayCoefficients + Coefficients::operator= reuse the existing array storage,
    // so this is allocation-free (unlike makeLowShelf(), which news a Coefficients).
    {
        toneSm.setTargetValue (toneParam->load());
        const float tonePct = toneSm.skip (numSamples) * 0.01f;     // -1..1
        const float tiltDb  = tonePct * 12.0f;
        *lowShelf.state  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (
            currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain (-tiltDb));
        *highShelf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain ( tiltDb));

        juce::dsp::ProcessContextReplacing<float> wctx (wetBlock);
        lowShelf.process (wctx);
        highShelf.process (wctx);
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
    dryGainSm.setTargetValue    (juce::Decibels::decibelsToGain (dryParam->load(),    -60.0f));
    wetGainSm.setTargetValue    (juce::Decibels::decibelsToGain (wetParam->load(),    -60.0f));
    irGainSm.setTargetValue     (juce::Decibels::decibelsToGain (irGainParam->load(), -60.0f));
    outputGainSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load(), -60.0f));
    duckSm.setTargetValue       (duckParam->load() * 0.01f);
    bypassSm.setTargetValue     (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    noIrSm.setTargetValue       (convolution.hasIR() ? 0.0f : 1.0f);   // no IR -> behave like bypass

    const float relMs     = duckRelParam->load();
    const float relCoeff  = std::exp (-1.0f / juce::jmax (1.0f, relMs * 0.001f * (float) currentSampleRate));
    clipGuardSm.setTargetValue (clipGuardParam->load() > 0.5f ? 1.0f : 0.0f);

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
        const float iGain = irGainSm.getNextValue();   // IR Gain: gain of the IR convolved with the input
        const float cGain = wetCompSm.getNextValue();   // adaptive wet gain compensation
        const float cg    = clipGuardSm.getNextValue(); // clip-guard blend (click-free toggle)
        const float oGain = outputGainSm.getNextValue();
        // no-IR state behaves exactly like bypass: dry at unity, wet muted —
        // a freshly inserted Convo must never silence the track
        const float byp   = juce::jmax (bypassSm.getNextValue(), noIrSm.getNextValue());
        const float fade  = loadFade.getNextValue();

        const float dEff = dGain + byp * (1.0f - dGain);   // bypass -> dry unity
        const float wEff = wGain * (1.0f - byp) * duckGain; // bypass -> wet muted
        const float oEff = oGain + byp * (1.0f - oGain);    // bypass -> output unity

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = inWork.getSample (ch, i);
            const float wet = wetWork.getSample (ch, i) * iGain * cGain;   // IR Gain then wet comp
            float s = (dry * dEff + wet * wEff) * oEff * fade;
            s = (1.0f - cg) * s + cg * convo::softClip (s);   // blended so the toggle is click-free
            mainOut.setSample (ch, i, s);
        }
    }

    // output meter (decaying peak hold, same as the input meter)
    float magOut = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        magOut = juce::jmax (magOut, mainOut.getMagnitude (ch, 0, numSamples));
    outputLevel.store (juce::jmax (magOut, outputLevel.load() * 0.85f));
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
    p.autoLevel    = rawLevelParam->load() < 0.5f;
    p.msMode       = msParam->load() > 0.5f;
    p.filterIR     = filterIRParam->load() > 0.5f;
    p.inHPHz       = inHPParam->load();
    p.inLPHz       = inLPParam->load();
    return p;
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
        const juce::File f (path);
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
        const bool msChanged     = (cur.msMode   != lastBaked.msMode);
        const bool targetChanged = (cur.filterIR != lastBaked.filterIR);
        // M/S and the filter-target change audio-thread routing; Reverse and Raw IR change the
        // kernel abruptly (reversal / a big level jump). All four are discrete toggles, so arm
        // the output fade to mask the swap and keep the toggle click-free. The continuous knobs
        // (fade-in/decay/taper) are dragged, so they keep relying on JUCE's seamless kernel
        // crossfade — arming the fade on every drag tick would pump the output.
        const bool toggleChanged = msChanged || targetChanged
                                 || (cur.reverse   != lastBaked.reverse)
                                 || (cur.autoLevel != lastBaked.autoLevel);
        convolution.rebake (irLibrary.getIR(), irLibrary.getSampleRate(), cur, bakedIR);
        bakedIRSampleRate = irLibrary.getSampleRate();
        lastBaked = cur;
        tailSeconds.store ((float) (bakedIR.getNumSamples() / juce::jmax (1.0, bakedIRSampleRate) + 0.5));

        if (msChanged)     msActive.store (cur.msMode);
        if (targetChanged) filterInput.store (! cur.filterIR);
        if (toggleChanged) loadFadePending.store (true);
        bakeGeneration.fetch_add (1);
    }
    lastSeenBakeParams = cur;
}

bool ConvoAudioProcessor::loadIRFile (const juce::File& file)
{
    if (! irLibrary.loadFile (file))
        return false;

    const auto cur = currentBakeParams();
    const int  lat = convolution.loadIR (irLibrary.getIR(), irLibrary.getSampleRate(), cur, bakedIR);

    bakedIRSampleRate  = irLibrary.getSampleRate();
    lastBaked          = cur;
    lastSeenBakeParams = cur;
    msActive.store (cur.msMode);          // kernel is baked to match the current M/S mode
    filterInput.store (! cur.filterIR);   // and to the current pre-IR filter target
    tailSeconds.store ((float) (bakedIR.getNumSamples() / juce::jmax (1.0, bakedIRSampleRate) + 0.5));

    loadFadePending.store (true);      // arm the click mask before publishing the delay
    dryDelaySamples.store (lat);
    setLatencySamples (lat);
    bakeGeneration.fetch_add (1);

    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);
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
