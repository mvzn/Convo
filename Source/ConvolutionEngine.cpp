#include "ConvolutionEngine.h"

#include <algorithm>
#include <cmath>

void ConvolutionEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    prepSampleRate = spec.sampleRate;
    shortEngine.prepare (spec);
    longEngine.prepare (spec);
}

void ConvolutionEngine::reset()
{
    shortEngine.reset();
    longEngine.reset();
}

juce::AudioBuffer<float> ConvolutionEngine::bake (const juce::AudioBuffer<float>& raw,
                                                  double irSampleRate,
                                                  const IRBakeParams& bp)
{
    const int numCh = raw.getNumChannels();
    const int n     = raw.getNumSamples();
    if (numCh == 0 || n == 0 || irSampleRate <= 0.0)
        return {};

    juce::AudioBuffer<float> out;
    out.makeCopyOf (raw);

    // 1. reverse (before all windowing, so fade-in shapes what becomes the new onset)
    if (bp.reverse)
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            std::reverse (d, d + n);
        }

    // 2. fade-in: raised-cosine ramp 0 -> 1 over the first fadeInSamps
    const int fadeInSamps = juce::jlimit (0, n, (int) std::round (bp.fadeInMs * 0.001 * irSampleRate));
    for (int i = 0; i < fadeInSamps; ++i)
    {
        const float x = (float) i / (float) fadeInSamps;                                   // 0..1
        const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);       // 0..1
        for (int ch = 0; ch < numCh; ++ch)
            out.getWritePointer (ch)[i] *= g;
    }

    // 3. decay: impose exp(-3*t/decaySamps) (-60 dB at decaySamps) from the end of fade-in,
    //    and truncate where it crosses -60 dB to save CPU. decayOff => leave the tail as recorded.
    int lEff = n;
    if (! bp.decayOff && bp.decaySeconds > 0.0f)
    {
        const double decaySamps = juce::jmax (1.0, (double) bp.decaySeconds * irSampleRate);
        lEff = fadeInSamps;
        for (int i = fadeInSamps; i < n; ++i)
        {
            const double t = (double) (i - fadeInSamps);
            const double g = std::pow (10.0, -3.0 * t / decaySamps);
            if (g < 0.001)            // -60 dB
                break;
            for (int ch = 0; ch < numCh; ++ch)
                out.getWritePointer (ch)[i] *= (float) g;
            lEff = i + 1;
        }
    }
    lEff = juce::jlimit (1, n, lEff);

    // 4. tail-taper: raised-cosine 1 -> 0 over the last taperSamps before lEff (de-click)
    const int taperSamps = juce::jlimit (0, lEff, (int) std::round (bp.taperMs * 0.001 * irSampleRate));
    for (int k = 0; k < taperSamps; ++k)
    {
        const int   i = lEff - taperSamps + k;
        const float x = (float) k / (float) taperSamps;                                    // 0..1
        const float g = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * x);       // 1..0
        for (int ch = 0; ch < numCh; ++ch)
            out.getWritePointer (ch)[i] *= g;
    }

    // 5. truncate to the effective length
    if (lEff < n)
        out.setSize (numCh, lEff, true, true, true);

    return out;
}

void ConvolutionEngine::loadIntoActive (const juce::AudioBuffer<float>& baked, double irSampleRate)
{
    if (baked.getNumSamples() == 0)
        return;

    auto& eng = (active.load() == 1) ? longEngine : shortEngine;

    juce::AudioBuffer<float> copy;
    copy.makeCopyOf (baked);
    eng.loadImpulseResponse (std::move (copy),
                             irSampleRate,
                             baked.getNumChannels() > 1 ? juce::dsp::Convolution::Stereo::yes
                                                        : juce::dsp::Convolution::Stereo::no,
                             juce::dsp::Convolution::Trim::no,
                             juce::dsp::Convolution::Normalise::no);
}

int ConvolutionEngine::loadIR (const juce::AudioBuffer<float>& raw, double irSampleRate,
                               const IRBakeParams& bp, juce::AudioBuffer<float>& outBaked)
{
    const double seconds = (irSampleRate > 0.0) ? (double) raw.getNumSamples() / irSampleRate : 0.0;

    active.store (seconds >= kThresholdSeconds ? 1 : 0);
    latencySamples = (active.load() == 1) ? kLongLatency : 0;

    const auto baked = bake (raw, irSampleRate, bp);
    outBaked.makeCopyOf (baked);
    loadIntoActive (baked, irSampleRate);
    loaded.store (baked.getNumSamples() > 0);
    return latencySamples;
}

void ConvolutionEngine::rebake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                                const IRBakeParams& bp, juce::AudioBuffer<float>& outBaked)
{
    if (raw.getNumSamples() == 0)
        return;

    const auto baked = bake (raw, irSampleRate, bp);
    outBaked.makeCopyOf (baked);
    loadIntoActive (baked, irSampleRate);
    loaded.store (baked.getNumSamples() > 0);
}

void ConvolutionEngine::process (juce::dsp::AudioBlock<float> block)
{
    if (! loaded.load())
    {
        block.clear();
        return;
    }

    juce::dsp::ProcessContextReplacing<float> ctx (block);
    if (active.load() == 1)
        longEngine.process (ctx);
    else
        shortEngine.process (ctx);
}
