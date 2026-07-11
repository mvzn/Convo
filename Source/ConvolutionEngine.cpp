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

#include "ConvolutionEngine.h"
#include "MidSide.h"

#include <algorithm>
#include <cmath>

void ConvolutionEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    prepSampleRate = spec.sampleRate;
    maxBlockSize   = (int) spec.maximumBlockSize;

    for (int i = 0; i < 4; ++i)
        engineAt (i).prepare (spec);

    // prepare() rebuilds each engine's kernel synchronously from its factory copy,
    // so any in-flight transition can be completed right here (no audio is running).
    scratch.setSize ((int) spec.numChannels, maxBlockSize);
    settleSamples = juce::roundToInt (0.08 * spec.sampleRate); // > JUCE's 50 ms install crossfade
    xfadeSamples  = juce::jmax (1, juce::roundToInt (0.03 * spec.sampleRate));

    active.store (targetEngine);
    loaded.store (haveKernel);
    pendingTarget.store (-1);
    pendingGain.store (1.0f);
    swapCompScale.store (1.0f);    // the processor resets its Wet Comp state alongside
    transitionGen.fetch_add (1);   // audio thread drops any stale transition phase

    latencySamples = computeLatency (targetEngine);
}

void ConvolutionEngine::reset()
{
    for (int i = 0; i < 4; ++i)
        engineAt (i).reset();
}

int ConvolutionEngine::computeLatency (int /*engineIndex*/) const noexcept
{
    // Both engines are zero-latency: the short engine is JUCE's zero-latency uniform
    // partition; the long engine is a non-uniform partition whose head is also
    // zero-latency (JUCE compensates the tail's internal latency). The plugin therefore
    // reports no latency for any IR length.
    return 0;
}

int ConvolutionEngine::expectedKernelSize (int bakedLen, double irSampleRate, double engineSampleRate) noexcept
{
    if (juce::approximatelyEqual (irSampleRate, engineSampleRate))
        return bakedLen;

    // mirrors juce_Convolution.cpp resampleImpulseResponse():
    // finalSize = roundToInt (jmax (1.0, n / (srcRate / destRate)))
    return juce::roundToInt (juce::jmax (1.0, bakedLen / (irSampleRate / engineSampleRate)));
}

juce::AudioBuffer<float> ConvolutionEngine::bake (const juce::AudioBuffer<float>& raw,
                                                  double irSampleRate,
                                                  const IRBakeParams& bp)
{
    const int numCh = raw.getNumChannels();
    int       n     = raw.getNumSamples();   // working length; updated if the IR is stretched
    if (numCh == 0 || n == 0 || irSampleRate <= 0.0)
        return {};

    juce::ScopedNoDenormals noDenormals;   // flush denormals in the offline decay/taper/filter math

    juce::AudioBuffer<float> out;
    out.makeCopyOf (raw);

    // 0. trim: keep only the IR region between startFrac and endFrac (head/tail trim). Applied
    //    first so stretch + reverse/fade/decay/taper all operate on the kept region. Clamped to
    //    at least one sample so a fully collapsed range (start >= end) still yields a valid kernel.
    {
        float s = juce::jlimit (0.0f, 1.0f, bp.startFrac);
        float e = juce::jlimit (0.0f, 1.0f, bp.endFrac);
        // The display backdrop shows the reversed IR when Reverse is on, and the Start/End
        // handles select a region of that displayed (reversed) waveform. Trim runs *before*
        // the reversal below, so mirror the selection into raw coordinates when reversing —
        // otherwise the kept region is the mirror image of what the user selected on screen
        // (and the shown kernel no longer lines up with the backdrop it's drawn over).
        if (bp.reverse) { const float ms = 1.0f - e, me = 1.0f - s; s = ms; e = me; }
        int first = juce::jlimit (0, n - 1, (int) std::floor ((double) s * (double) n));
        int last  = juce::jlimit (0, n,     (int) std::ceil  ((double) e * (double) n));   // exclusive
        if (last <= first)
            last = first + 1;
        const int trimLen = last - first;
        if (trimLen < n)
        {
            juce::AudioBuffer<float> trimmed (numCh, trimLen);
            for (int ch = 0; ch < numCh; ++ch)
                trimmed.copyFrom (ch, 0, out, ch, first, trimLen);
            out = std::move (trimmed);
            n   = trimLen;
        }
    }

    // 0b. stretch: time-scale the (trimmed) IR by resampling (linear interpolation), so fade-in /
    //     decay / taper operate in the stretched time frame. No anti-alias filter on down-stretch
    //     (<1) — minor and acceptable for an IR. Engine/latency unchanged (rebake keeps the file's
    //     engine), per the "bake knobs never reselect the engine" rule.
    if (! juce::approximatelyEqual (bp.stretch, 1.0f) && bp.stretch > 0.0f && n > 1)
    {
        const int newLen = juce::jlimit (1, 1 << 24, (int) std::lround ((double) n * (double) bp.stretch));
        if (newLen > 1 && newLen != n)
        {
            juce::AudioBuffer<float> stretched (numCh, newLen);
            const double ratio = (double) (n - 1) / (double) (newLen - 1);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = out.getReadPointer (ch);
                float*       dst = stretched.getWritePointer (ch);
                for (int i = 0; i < newLen; ++i)
                {
                    const double pos = (double) i * ratio;
                    const int    i0  = juce::jlimit (0, n - 1, (int) pos);
                    const int    i1  = juce::jmin (n - 1, i0 + 1);
                    const float  f   = (float) (pos - (double) i0);
                    dst[i] = src[i0] + (src[i1] - src[i0]) * f;
                }
            }
            out = std::move (stretched);
            n   = newLen;
        }
    }
    const int len = out.getNumSamples();   // working length after trim + stretch

    // 1. reverse (before all windowing, so fade-in shapes what becomes the new onset)
    if (bp.reverse)
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = out.getWritePointer (ch);
            std::reverse (d, d + len);
        }

    // 2. decay-cut length: the -60 dB point as a fraction of the baked (trim+stretch) length,
    //    computed BEFORE the fade-in so a fade-in can never extend the kernel past the cut (the
    //    old code seeded lEff from fadeInSamps, so a long fade re-baked a near-full IR at Decay
    //    100%). decayOff / fraction >= 1 keeps the full length. Mirrors maxFadeInMs().
    int lEff = len;
    if (! bp.decayOff && bp.decayFraction < 1.0f)
        lEff = juce::jlimit (1, len, (int) std::ceil ((double) bp.decayFraction * (double) len));

    // 3. fade-in: raised-cosine ramp 0 -> 1 over the first fadeInSamps, capped so at least
    //    kMinTailMs of IR survives within the kept (decay-cut) length — beyond that the fade
    //    would swallow the whole kernel.
    const int minTailSamps = juce::jmax (1, (int) std::round (kMinTailMs * 0.001 * irSampleRate));
    const int fadeInMax    = juce::jmax (0, lEff - minTailSamps);
    const int fadeInSamps  = juce::jlimit (0, fadeInMax, (int) std::round (bp.fadeInMs * 0.001 * irSampleRate));
    for (int i = 0; i < fadeInSamps; ++i)
    {
        const float x = (float) i / (float) fadeInSamps;                                   // 0..1
        const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);       // 0..1
        for (int ch = 0; ch < numCh; ++ch)
            out.getWritePointer (ch)[i] *= g;
    }

    // 4. decay: exp envelope over [fadeInSamps, lEff), reaching ~-60 dB by the cut point. With no
    //    fade-in this is the same rate as the old decaySamps = decayFraction*len; a fade-in just
    //    compresses the decay into the shorter remaining span rather than lengthening the kernel.
    //    Accumulated incrementally (g *= step) — identical math to a per-sample pow(), far cheaper.
    if (! bp.decayOff && bp.decayFraction < 1.0f)
    {
        const double decSpan = juce::jmax (1.0, (double) (lEff - fadeInSamps));
        const double step    = std::pow (10.0, -3.0 / decSpan);        // per-sample ratio -> -60 dB at lEff
        double g = 1.0;
        for (int i = fadeInSamps; i < lEff; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
                out.getWritePointer (ch)[i] *= (float) g;
            g *= step;
        }
    }
    lEff = juce::jlimit (1, len, lEff);

    // 4. tail-taper: raised-cosine 1 -> 0 over the last taperSamps before lEff
    //    (de-click). (k+1)/taperSamps so the final kernel sample lands exactly on 0.
    const int taperSamps = juce::jlimit (0, lEff, (int) std::round (bp.taperMs * 0.001 * irSampleRate));
    for (int k = 0; k < taperSamps; ++k)
    {
        const int   i = lEff - taperSamps + k;
        const float x = (float) (k + 1) / (float) taperSamps;                              // ..1
        const float g = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * x);       // ..0
        for (int ch = 0; ch < numCh; ++ch)
            out.getWritePointer (ch)[i] *= g;
    }

    // 5. truncate to the effective length
    if (lEff < len)
        out.setSize (numCh, lEff, true, true, true);

    // 5b. damping: progressive HF rolloff over the tail (air absorption). Crossfade the kernel
    //     toward a first-order low-passed copy, the blend ramping 0 (onset) -> 1 (tail end), with
    //     the cutoff falling as Damp rises. Placed before auto-level so that, when Norm IR is on,
    //     loudness stays put as Damp changes (only the spectral balance shifts). 0 = off (skipped).
    if (bp.dampAmt > 0.001f)
    {
        const float  amt = juce::jlimit (0.0f, 1.0f, bp.dampAmt);
        const double fc  = juce::jmin (18000.0 * std::pow (1000.0 / 18000.0, (double) amt),   // 18 kHz -> ~1 kHz
                                       irSampleRate * 0.45);                                   // stay below Nyquist
        const int    dn  = out.getNumSamples();

        juce::AudioBuffer<float> lp;
        lp.makeCopyOf (out);
        {
            juce::dsp::ProcessSpec ds { irSampleRate, (juce::uint32) juce::jmax (1, dn), (juce::uint32) numCh };
            juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                           juce::dsp::IIR::Coefficients<float>> lpf;
            lpf.prepare (ds);
            *lpf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (irSampleRate, fc);
            lpf.reset();
            juce::dsp::AudioBlock<float> lb (lp);
            juce::dsp::ProcessContextReplacing<float> lctx (lb);
            lpf.process (lctx);
        }

        const float denom = (float) juce::jmax (1, dn - 1);
        for (int ch = 0; ch < numCh; ++ch)
        {
            float*       d = out.getWritePointer (ch);
            const float* l = lp.getReadPointer (ch);
            for (int i = 0; i < dn; ++i)
            {
                const float g = (float) i / denom;        // 0 at onset -> 1 at the tail end
                d[i] = d[i] * (1.0f - g) + l[i] * g;
            }
        }
    }

    // 6. auto-level: one gain for all channels (stereo balance preserved) so the
    //    loudest channel has unit energy. A true IR (a decaying impulse, energy ~1)
    //    is barely touched; dense full-scale audio used as an IR — which would
    //    otherwise convolve 30..45 dB hot — comes out at a musical level.
    if (bp.autoLevel)
    {
        double maxEnergy = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* h = out.getReadPointer (ch);
            double e = 0.0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                e += (double) h[i] * (double) h[i];
            maxEnergy = juce::jmax (maxEnergy, e);
        }

        const double l2 = std::sqrt (maxEnergy);
        if (l2 > 1.0e-6)        // leave silent kernels alone
            out.applyGain ((float) (1.0 / l2));
    }

    // 6b. pre-IR filter baked into the kernel (when the filter targets the IR). Placed
    //     after auto-level so the result matches runtime input-filtering by linearity
    //     (conv is LTI: filter(in) * k == in * filter(k), and the auto-level gain is the
    //     same either way). 2nd-order HP/LP with shared Q — same coeffs + the same flat-extreme
    //     skip the audio thread uses at runtime, so baked and runtime filtering match exactly.
    if (bp.filterIR && (bp.inHPHz > 21.0f || bp.inLPHz < 19900.0f))
    {
        juce::dsp::ProcessSpec ks { irSampleRate,
                                    (juce::uint32) juce::jmax (1, out.getNumSamples()),
                                    (juce::uint32) out.getNumChannels() };
        juce::dsp::AudioBlock<float> kb (out);
        juce::dsp::ProcessContextReplacing<float> ctx (kb);

        if (bp.inHPHz > 21.0f)
        {
            juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                           juce::dsp::IIR::Coefficients<float>> hp;
            hp.prepare (ks);
            *hp.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (irSampleRate, bp.inHPHz, bp.inFilterQ);
            hp.reset();
            hp.process (ctx);
        }
        if (bp.inLPHz < 19900.0f)
        {
            juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                           juce::dsp::IIR::Coefficients<float>> lp;
            lp.prepare (ks);
            *lp.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (irSampleRate, bp.inLPHz, bp.inFilterQ);
            lp.reset();
            lp.process (ctx);
        }
    }

    return out;
}

double ConvolutionEngine::maxFadeInMs (int rawNumSamples, double irSampleRate, const IRBakeParams& bp)
{
    if (rawNumSamples <= 0 || irSampleRate <= 0.0)
        return 0.0;

    // mirror bake()'s length pipeline: trim (with the reverse-coordinate mirror) -> stretch ->
    // decay cut. The fade-in max is that (decay-cut) length minus kMinTailMs, so the ramp adapts
    // to the IR AND shrinks with the decay cut, always leaving a short tail.
    float s = juce::jlimit (0.0f, 1.0f, bp.startFrac);
    float e = juce::jlimit (0.0f, 1.0f, bp.endFrac);
    if (bp.reverse) { const float ms = 1.0f - e, me = 1.0f - s; s = ms; e = me; }
    const int first = juce::jlimit (0, rawNumSamples - 1, (int) std::floor ((double) s * (double) rawNumSamples));
    int       last  = juce::jlimit (0, rawNumSamples,     (int) std::ceil  ((double) e * (double) rawNumSamples));
    if (last <= first) last = first + 1;
    int n = last - first;

    if (! juce::approximatelyEqual (bp.stretch, 1.0f) && bp.stretch > 0.0f && n > 1)
        n = juce::jlimit (1, 1 << 24, (int) std::lround ((double) n * (double) bp.stretch));

    int lEff = n;
    if (! bp.decayOff && bp.decayFraction < 1.0f)
        lEff = juce::jlimit (1, n, (int) std::ceil ((double) bp.decayFraction * (double) n));

    const int minTailSamps = juce::jmax (1, (int) std::round (kMinTailMs * 0.001 * irSampleRate));
    const int maxFadeSamps = juce::jmax (0, lEff - minTailSamps);
    return (double) maxFadeSamps / irSampleRate * 1000.0;
}

// max-channel L2 (amplitude) of a baked kernel — the same loudness measure auto-level uses,
// so the ratio of two kernels' L2s is the convolution-loudness ratio between them. Drives
// the swap-continuity makeup (see the class comment).
static float kernelL2 (const juce::AudioBuffer<float>& k) noexcept
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
    return (float) std::sqrt (maxE);
}

void ConvolutionEngine::loadInto (int engineIndex, const juce::AudioBuffer<float>& baked,
                                  double irSampleRate, float bakedL2)
{
    slotL2[engineIndex] = bakedL2;

    juce::AudioBuffer<float> copy;
    copy.makeCopyOf (baked);
    engineAt (engineIndex).loadImpulseResponse (std::move (copy),
                                                irSampleRate,
                                                baked.getNumChannels() > 1 ? juce::dsp::Convolution::Stereo::yes
                                                                           : juce::dsp::Convolution::Stereo::no,
                                                juce::dsp::Convolution::Trim::no,
                                                juce::dsp::Convolution::Normalise::no);
}

// loudness-continuity makeup: what the incoming kernel's output must be scaled by to sit
// at the audible kernel's loudness. Deliberately UNclamped to the Wet Comp ±18 dB range —
// continuity comes first; the comp follower re-clamps over its own ramp — but bounded to
// ±36 dB against pathological kernels.
static float swapMakeup (float audibleL2, float newL2) noexcept
{
    if (audibleL2 < 1.0e-6f || newL2 < 1.0e-6f)
        return 1.0f;
    return juce::jlimit (1.0f / 64.0f, 64.0f, audibleL2 / newL2);
}

int ConvolutionEngine::loadIR (const juce::AudioBuffer<float>& raw, double irSampleRate,
                               const IRBakeParams& bp, juce::AudioBuffer<float>& outBaked,
                               bool levelMatch)
{
    const double seconds  = (irSampleRate > 0.0) ? (double) raw.getNumSamples() / irSampleRate : 0.0;
    const int    newClass = seconds >= kThresholdSeconds ? 1 : 0;

    const auto baked = bake (raw, irSampleRate, bp);
    outBaked.makeCopyOf (baked);
    if (baked.getNumSamples() == 0)
        return latencySamples;     // bad bake: keep the current state untouched

    // Every new file warm-up-transitions in from a NON-audible engine: the sibling slot
    // when the audible engine is already in the right class, slot A when entering a class.
    // (`active` can trail `targetEngine` while a previous swap is in flight — keying off
    // the audible engine keeps ping-pong correct either way.)
    const int  activeNow = active.load();
    const bool audible   = loaded.load();
    const int  newIdx    = (audible && (activeNow >> 1) == newClass) ? (activeNow ^ 1)
                                                                     : newClass * 2;

    const float audL2 = slotL2[activeNow];
    const float newL2 = kernelL2 (baked);
    loadInto (newIdx, baked, irSampleRate, newL2);

    haveKernel             = true;
    targetEngine           = newIdx;
    lastLoadUsedTransition = true;

    pendingSize.store (expectedKernelSize (baked.getNumSamples(), irSampleRate, prepSampleRate));
    pendingGain.store ((levelMatch && audible) ? swapMakeup (audL2, newL2) : 1.0f);
    pendingTarget.store (newIdx);
    transitionGen.fetch_add (1);

    latencySamples = computeLatency (targetEngine);
    return latencySamples;
}

void ConvolutionEngine::rebake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                                const IRBakeParams& bp, juce::AudioBuffer<float>& outBaked,
                                bool levelMatch)
{
    if (raw.getNumSamples() == 0 || ! haveKernel)
        return;

    const auto baked = bake (raw, irSampleRate, bp);
    if (baked.getNumSamples() == 0)
        return;
    outBaked.makeCopyOf (baked);

    const int   activeNow = active.load();
    const bool  audible   = loaded.load();
    const float audL2     = slotL2[activeNow];
    const float newL2     = kernelL2 (baked);

    // A warm-up still in flight (or the very first load still warming): replace the
    // warming slot's kernel and re-arm — the expected size and makeup have changed.
    // (Racing the flip by a block loads into the just-audible engine instead: that
    // falls back to JUCE's internal in-place crossfade, a bounded one-step blip.)
    if (! audible || activeNow != targetEngine)
    {
        loadInto (targetEngine, baked, irSampleRate, newL2);
        pendingSize.store (expectedKernelSize (baked.getNumSamples(), irSampleRate, prepSampleRate));
        pendingGain.store ((levelMatch && audible) ? swapMakeup (audL2, newL2) : 1.0f);
        pendingTarget.store (targetEngine);
        transitionGen.fetch_add (1);
        lastLoadUsedTransition = true;
        return;
    }

    const float makeup  = levelMatch ? swapMakeup (audL2, newL2) : 1.0f;
    const bool  bigStep = makeup > 1.122f || makeup < 0.891f;   // ~±1 dB loudness step

    if (bigStep)
    {
        // Level-stepping rebake (e.g. the Norm IR toggle): warm the sibling slot up and
        // crossfade over at matched loudness, so the swap is inaudible no matter how long
        // JUCE's background load takes. The makeup hands off to Wet Comp at the flip.
        const int newIdx = activeNow ^ 1;
        loadInto (newIdx, baked, irSampleRate, newL2);
        targetEngine           = newIdx;
        lastLoadUsedTransition = true;

        pendingSize.store (expectedKernelSize (baked.getNumSamples(), irSampleRate, prepSampleRate));
        pendingGain.store (makeup);
        pendingTarget.store (newIdx);
        transitionGen.fetch_add (1);
        return;
    }

    // Small tweak (a knob-drag commit): reload the audible engine in place — JUCE's
    // internal crossfade keeps it instant-feeling — and publish the sub-dB makeup
    // immediately. That's mistimed by the async install, but inaudible at this size.
    loadInto (targetEngine, baked, irSampleRate, newL2);
    lastLoadUsedTransition = false;
    if (! juce::approximatelyEqual (makeup, 1.0f))
        publishSwapScale (makeup);
}

void ConvolutionEngine::process (juce::dsp::AudioBlock<float> block)
{
    const int gen = transitionGen.load (std::memory_order_acquire);
    if (gen != lastGenSeen)
    {
        lastGenSeen = gen;
        transTarget = pendingTarget.load();
        transGain   = pendingGain.load();
        // A transition is only meaningful towards an engine that isn't already the
        // live one; a re-arm aimed at the audible engine is a seamless kernel swap.
        phase = (transTarget >= 0 && (transTarget != active.load() || ! loaded.load()))
                  ? Phase::waiting : Phase::idle;
    }

    const bool haveWet = loaded.load();

    if (phase == Phase::idle)
    {
        if (! haveWet)
        {
            block.clear();
            return;
        }
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        engineAt (active.load()).process (ctx);
        return;
    }

    // --- transition in flight: old engine (or silence) stays audible while the
    //     target engine warms up on the scratch buffer ---
    const int numCh = (int) block.getNumChannels();
    const int n     = (int) block.getNumSamples();

    if (n > scratch.getNumSamples() || numCh > scratch.getNumChannels())
    {
        // host delivered more than it promised in prepare(); skip warming this block
        if (! haveWet) { block.clear(); return; }
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        engineAt (active.load()).process (ctx);
        return;
    }

    auto scratchBlock = juce::dsp::AudioBlock<float> (scratch)
                            .getSubsetChannelBlock (0, (size_t) numCh)
                            .getSubBlock (0, (size_t) n);
    scratchBlock.copyFrom (block);

    auto& target = engineAt (transTarget);

    if (haveWet)
    {
        // first load ever has active == transTarget; the engine must not run twice
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        engineAt (active.load()).process (ctx);
    }
    else
    {
        block.clear();
    }

    {
        juce::dsp::ProcessContextReplacing<float> sctx (scratchBlock);
        target.process (sctx);
    }

    // loudness-continuity makeup on the incoming engine: the output crossfade below then
    // morphs between two equal-loudness signals, so the swap itself is level-silent. The
    // gain covers the whole flip block; the processor takes it over (via the published
    // comp scale) from the next block on.
    if (! juce::approximatelyEqual (transGain, 1.0f))
        scratchBlock.multiplyBy (transGain);

    switch (phase)
    {
        case Phase::waiting:
            // the background build is installed by the audio thread inside process(),
            // so polling the size here (same thread) is race-free
            if (std::abs (target.getCurrentIRSize() - pendingSize.load()) <= 2)
            {
                phase      = Phase::settling;
                settleLeft = settleSamples;
            }
            break;

        case Phase::settling:                  // let JUCE's internal crossfade finish
            settleLeft -= n;
            if (settleLeft <= 0)
            {
                phase     = Phase::fading;
                xfadeLeft = xfadeSamples;
            }
            break;

        case Phase::fading:                    // crossfade output old -> new
        {
            const float wStart = (float) (xfadeSamples - xfadeLeft) / (float) xfadeSamples;
            const float wStep  = 1.0f / (float) xfadeSamples;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = block.getChannelPointer ((size_t) ch);
                auto* nw  = scratchBlock.getChannelPointer ((size_t) ch);
                float w   = wStart;
                for (int i = 0; i < n; ++i)
                {
                    w = juce::jmin (1.0f, w + wStep);
                    out[i] += (nw[i] - out[i]) * w;
                }
            }
            xfadeLeft -= n;
            if (xfadeLeft <= 0)
            {
                active.store (transTarget);
                loaded.store (true);
                // hand the makeup off to the processor's Wet Comp: consumed before the
                // NEXT block's process(), exactly when this engine stops applying it
                if (! juce::approximatelyEqual (transGain, 1.0f))
                    publishSwapScale (transGain);
                phase = Phase::idle;
            }
            break;
        }

        case Phase::idle:
            break;
    }
}
