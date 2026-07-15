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

#include <atomic>

/** Shaping applied to the raw IR before it becomes the convolution kernel.
    All of these are "bake" controls — changing one re-windows the IR on the
    message thread; none of them touch the audio thread directly. */
struct IRBakeParams
{
    float startFrac = 0.0f;    // trim the head: keep the IR from this fraction (0..1) of its length
    float endFrac = 1.0f;      // trim the tail: keep the IR up to this fraction (0..1) of its length
    float fadeInMs = 0.0f;     // raised-cosine onset ramp
    float decayFraction = 1.0f; // exponential tail decay: the -60 dB point as a fraction (0..1) of the
                                // post-trim/stretch length, so decay always starts from the baked length.
                                // 1.0 = no truncation; smaller = shorter tail. Ignored if decayOff
    bool  decayOff = true;     // true => use the IR's recorded decay unchanged
    float taperMs = 10.0f;     // raised-cosine de-click ramp at the (truncated) end
    bool  reverse = false;     // reverse the IR before windowing
    bool  autoLevel = true;    // scale the kernel to unity energy (max-channel L2 = 1);
                               // false = "Raw": the IR's recorded level, unscaled
    bool  filterIR = false;    // bake the pre-IR HP/LP into the kernel instead of filtering
                               // the input at runtime (cutoffs below; only used when true)
    float inHPHz = 20.0f;      // 2nd-order high-pass cutoff baked into the kernel
    float inLPHz = 20000.0f;   // 2nd-order low-pass  cutoff baked into the kernel
    float inFilterQ = 0.707f;  // shared resonance/Q for the baked HP/LP (matches the runtime filter)
    float stretch = 1.0f;      // time-stretch factor: resample the IR to this multiple of its
                               // length before windowing (1.0 = off; <1 shortens, >1 lengthens)
    float dampAmt = 0.0f;      // damping 0..1: progressive HF rolloff over the tail (air
                               // absorption). 0 = off; crossfades the kernel toward a low-passed
                               // copy, the blend ramping 0 (onset) -> 1 (tail end)

    bool operator== (const IRBakeParams& o) const noexcept
    {
        return juce::approximatelyEqual (startFrac, o.startFrac)
            && juce::approximatelyEqual (endFrac, o.endFrac)
            && juce::approximatelyEqual (fadeInMs, o.fadeInMs)
            && juce::approximatelyEqual (stretch, o.stretch)
            && juce::approximatelyEqual (decayFraction, o.decayFraction)
            && decayOff == o.decayOff
            && juce::approximatelyEqual (taperMs, o.taperMs)
            && juce::approximatelyEqual (dampAmt, o.dampAmt)
            && reverse == o.reverse
            && autoLevel == o.autoLevel
            && filterIR == o.filterIR
            // cutoffs only affect the bake when the filter targets the IR, so changing
            // them in input-filter mode must not trigger a needless re-bake
            && (! filterIR || (juce::approximatelyEqual (inHPHz, o.inHPHz)
                            && juce::approximatelyEqual (inLPHz, o.inLPHz)
                            && juce::approximatelyEqual (inFilterQ, o.inFilterQ)));
    }
    bool operator!= (const IRBakeParams& o) const noexcept { return ! (*this == o); }
};

/**
    Owns four persistent juce::dsp::Convolution engines — a ping-pong PAIR per length
    class — and switches classes adaptively based on IR length:

      - short IRs (< kThresholdSeconds)  -> zero-latency uniform engines (cheap for a
        handful of partitions)
      - long  IRs (>= kThresholdSeconds) -> zero-latency NON-UNIFORM partitioned engines
        (kHeadSize head). A small zero-latency head covers the IR onset while the bulk of
        the tail is convolved in large blocks — far cheaper than running the whole long IR
        through the zero-latency uniform engine. JUCE's NonUniform mode compensates the
        tail's internal latency, so the engine reports ZERO latency for any IR length.

    All engines are therefore zero-latency: the plugin reports no latency regardless of
    which engine is live, and the dry tap needs no alignment delay.

    juce::dsp::Convolution loads kernels ASYNCHRONOUSLY (a background thread builds
    the FFT segments, the audio thread installs them mid-process). Reloading the
    already-audible engine is therefore seamless — JUCE crossfades old -> new kernel
    internally — but the install moment is unobservable. Switching engines (or the very
    first load into a virgin engine) would briefly route audio through a unit-impulse
    kernel, i.e. raw pass-through.

    Engine switches — and any load that steps the kernel's loudness while Wet Comp wants
    continuity (levelMatch) — therefore go through a warm-up transition:
      1. the message thread loads the kernel into a NON-audible engine (the class pair's
         other slot) and publishes a transition request (target index + expected kernel
         size + the loudness-makeup gain old/new);
      2. the audio thread keeps outputting the OLD engine (or silence if nothing was
         ever live) while also feeding the target engine into a scratch buffer, scaled
         by the makeup gain so old and new sit at the same loudness;
      3. once the target reports the expected kernel size, it settles through JUCE's
         internal crossfade window, then the output crossfades old -> new (equal-loudness
         by construction) and the target becomes active. At that flip the makeup gain is
         published via consumeSwapCompScale() so the processor can fold it into its Wet
         Comp smoother the following block — the audible level is continuous through the
         whole swap, THEN the comp follower glides to the true post-swap match.

    Small loudness steps (< ~1 dB, e.g. a decay-knob drag commit) skip the transition and
    reload the audible engine directly (JUCE's internal crossfade, instant-feeling), with
    the tiny makeup published immediately.

    Known edge: two same-length rebakes in quick succession can fool the size poll (the
    warming engine already reports the expected size before the second kernel installs);
    the settle window covers the typical install time, so this is a bounded, rare blip.

    loadIR / rebake are MESSAGE THREAD (they allocate + window the IR).
    process() is the only audio-thread method.
*/
class ConvolutionEngine
{
public:
    ConvolutionEngine() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** A new raw IR was loaded: pick the engine class by length, bake, load into the
        class's non-audible slot, and start a warm-up transition. Returns the latency in
        samples of the engine that will be audible. `outBaked` receives the windowed
        IR for display. `levelMatch` (= Wet Comp on) keeps the audible loudness
        continuous across the swap (see class comment). */
    int loadIR (const juce::AudioBuffer<float>& raw, double irSampleRate,
                const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked,
                bool levelMatch);

    /** A bake param changed: re-window the same raw IR and reload it. Loudness steps
        beyond ~1 dB (with levelMatch) go through the sibling-slot transition; small
        tweaks reload the audible engine in place. Latency is unchanged. */
    void rebake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                 const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked,
                 bool levelMatch);

    /** Audio thread, once per block BEFORE process(): the pending multiplicative Wet
        Comp scale that keeps the audible wet loudness continuous across a kernel swap
        (1 = nothing pending). Consuming resets it. Published at a transition's output
        flip (so the engine-side makeup hands off to the comp smoother the block after
        the crossfade completes) or immediately for small in-place reloads. */
    float consumeSwapCompScale() noexcept { return swapCompScale.exchange (1.0f); }

    /** Message thread: did the most recent loadIR/rebake go through the warm-up
        transition (its own equal-loudness crossfade) rather than an in-place reload?
        Lets the processor skip the redundant load-fade click mask for those. */
    bool lastLoadTransitioned() const noexcept { return lastLoadUsedTransition; }

    /** Audio thread: convolve `block` in place through the active engine, running
        any pending warm-up transition. Clears the block (wet silence) until the
        first kernel is confirmed live. */
    void process (juce::dsp::AudioBlock<float> block);

    /** Latency of the audible (or transitioning-to) engine for the prepared block
        size. Message thread. */
    int  getLatencySamples() const noexcept { return latencySamples; }

    /** True once a kernel is confirmed live on the audio thread (wet output is
        meaningful). False on a fresh instance and during the very first load. */
    bool hasIR() const noexcept { return loaded.load(); }

    /** Window the raw IR into the convolution kernel: reverse -> fade-in -> decay
        (+truncate at -60 dB) -> tail-taper. Pure function of its arguments; exposed
        static so the bake pipeline can be unit-tested without a live convolution. */
    static juce::AudioBuffer<float> bake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                                          const IRBakeParams& bp);

    /** The longest usable fade-in (ms) for the given raw IR + bake params: the decay-cut
        length (after trim + stretch) minus kMinTailMs, so the ramp adapts to the IR, shrinks
        with the decay cut, and always leaves a short tail. Drives the editor's fade-in clamp
        (the param range caps the absolute max at 10 s); bake() applies its own cap internally.
        Returns 0 for an empty/invalid IR. */
    static double maxFadeInMs (int rawNumSamples, double irSampleRate, const IRBakeParams& bp);

    /** End of the direct-path spike at the head of a recorded IR, as a fraction (0..1)
        of the raw length — the natural snap target for the Start trim when the user wants
        a tail/reflections-only wet (no dry copy in the convolution). Returns -1 when no
        distinct head spike exists (dense material, swells, silence, click-only IRs), in
        which case snapping is disabled. Pure function of its arguments; message thread. */
    static float detectOnsetEndFrac (const juce::AudioBuffer<float>& raw, double sampleRate);

    static constexpr float kMinTailMs = 25.0f;   // min IR kept after a fade-in (never fully swallowed)

    /** Test canary only: the latency JUCE reports for the long (non-uniform) engines.
        Must stay 0 — NonUniform compensates the tail latency. Cross-checked against the
        live engine so a JUCE upgrade that changes the rule fails loudly. */
    int getJuceReportedLongLatency() const { return longEngineA.getLatency(); }

    static constexpr int    kHeadSize         = 1024; // non-uniform head partition, samples
    static constexpr double kThresholdSeconds = 1.5;  // raw-length boundary between engines

private:
    // engine index 0..3 = class (short/long) * 2 + slot: { short A, short B, long A, long B }.
    // classOf = index >> 1; the sibling slot (same class, other engine) = index ^ 1.
    juce::dsp::Convolution&       engineAt (int index)       noexcept
    {
        return index == 0 ? shortEngineA : index == 1 ? shortEngineB
             : index == 2 ? longEngineA  : longEngineB;
    }
    const juce::dsp::Convolution& engineAt (int index) const noexcept
    {
        return const_cast<ConvolutionEngine*> (this)->engineAt (index);
    }

    void loadInto (int engineIndex, const juce::AudioBuffer<float>& baked,
                   double irSampleRate, float bakedL2);
    int  computeLatency (int engineIndex) const noexcept;

    /** Multiply `g` into the pending comp scale (CAS loop: the message thread publishes
        small in-place reload steps, the audio thread publishes at transition flips). */
    void publishSwapScale (float g) noexcept
    {
        float cur = swapCompScale.load();
        while (! swapCompScale.compare_exchange_weak (cur, cur * g)) {}
    }

    /** Kernel size after JUCE resamples it to the engine rate (mirrors
        juce_Convolution.cpp's resampleImpulseResponse length). */
    static int expectedKernelSize (int bakedLen, double irSampleRate, double engineSampleRate) noexcept;

    juce::dsp::Convolution shortEngineA { juce::dsp::Convolution::Latency   { 0 } };
    juce::dsp::Convolution shortEngineB { juce::dsp::Convolution::Latency   { 0 } };
    juce::dsp::Convolution longEngineA  { juce::dsp::Convolution::NonUniform { kHeadSize } };
    juce::dsp::Convolution longEngineB  { juce::dsp::Convolution::NonUniform { kHeadSize } };

    // --- message-thread bookkeeping ---
    int    targetEngine   = 0;                 // engine that is (or will become) audible
    float  slotL2[4]      = {};                // max-channel L2 (amplitude) of each slot's kernel
    bool   haveKernel     = false;
    bool   lastLoadUsedTransition = false;
    int    latencySamples = 0;
    double prepSampleRate = 48000.0;
    int    maxBlockSize   = 0;

    // --- message -> audio transition request (audio thread never writes these) ---
    std::atomic<int>   pendingTarget { -1 };   // engine index to warm up
    std::atomic<int>   pendingSize   { 0 };    // expected resampled kernel size
    std::atomic<float> pendingGain   { 1.0f }; // loudness makeup on the incoming engine's output
    std::atomic<int>   transitionGen { 0 };    // bumped to (re)arm the audio thread

    // --- audio -> message state ---
    std::atomic<int>  active { 0 };            // audible engine (audio thread flips on completion)
    std::atomic<bool> loaded { false };        // a kernel is confirmed live

    // --- swap-continuity scale for the processor's Wet Comp (see consumeSwapCompScale) ---
    std::atomic<float> swapCompScale { 1.0f };

    // --- audio-thread-only transition state ---
    enum class Phase { idle, waiting, settling, fading };
    Phase phase           = Phase::idle;
    int   lastGenSeen     = 0;
    int   transTarget     = -1;
    float transGain       = 1.0f;              // latched pendingGain for the current transition
    int   settleLeft      = 0;
    int   xfadeLeft       = 0;
    int   settleSamples   = 0;                 // covers JUCE's internal install crossfade
    int   xfadeSamples    = 0;                 // our old -> new output crossfade
    juce::AudioBuffer<float> scratch;          // target engine warm-up buffer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvolutionEngine)
};
