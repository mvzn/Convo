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
    float decaySeconds = 0.0f; // exponential tail decay (RT60-ish); ignored if decayOff
    bool  decayOff = true;     // true => use the IR's recorded decay unchanged
    float taperMs = 10.0f;     // raised-cosine de-click ramp at the (truncated) end
    bool  reverse = false;     // reverse the IR before windowing
    bool  autoLevel = true;    // scale the kernel to unity energy (max-channel L2 = 1);
                               // false = "Raw": the IR's recorded level, unscaled
    bool  filterIR = false;    // bake the pre-IR HP/LP into the kernel instead of filtering
                               // the input at runtime (cutoffs below; only used when true)
    float inHPHz = 20.0f;      // first-order high-pass cutoff baked into the kernel
    float inLPHz = 20000.0f;   // first-order low-pass  cutoff baked into the kernel
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
            && juce::approximatelyEqual (decaySeconds, o.decaySeconds)
            && decayOff == o.decayOff
            && juce::approximatelyEqual (taperMs, o.taperMs)
            && juce::approximatelyEqual (dampAmt, o.dampAmt)
            && reverse == o.reverse
            && autoLevel == o.autoLevel
            && filterIR == o.filterIR
            // cutoffs only affect the bake when the filter targets the IR, so changing
            // them in input-filter mode must not trigger a needless re-bake
            && (! filterIR || (juce::approximatelyEqual (inHPHz, o.inHPHz)
                            && juce::approximatelyEqual (inLPHz, o.inLPHz)));
    }
    bool operator!= (const IRBakeParams& o) const noexcept { return ! (*this == o); }
};

/**
    Owns two persistent juce::dsp::Convolution engines and switches between them
    adaptively based on IR length:

      - short IRs (< kThresholdSeconds)  -> zero-latency engine
      - long  IRs (>= kThresholdSeconds) -> low-CPU engine (head latency depends on
        both kLongLatency and the host block size — see longEngineLatencyForBlockSize()).

    juce::dsp::Convolution loads kernels ASYNCHRONOUSLY (a background thread builds
    the FFT segments, the audio thread installs them mid-process). Reloading the
    already-audible engine is therefore seamless — JUCE crossfades old -> new kernel
    internally. But switching engines (or the very first load into a virgin engine)
    would briefly route audio through a unit-impulse kernel, i.e. raw pass-through.

    To avoid that, engine switches go through a warm-up transition:
      1. the message thread loads the kernel into the target engine and publishes a
         transition request (target index + expected kernel size);
      2. the audio thread keeps outputting the OLD engine (or silence if nothing was
         ever live) while also feeding the target engine into a scratch buffer;
      3. once the target reports the expected kernel size, it settles through JUCE's
         internal crossfade window, then the output crossfades old -> new and the
         target becomes active.

    loadIR / rebake are MESSAGE THREAD (they allocate + window the IR).
    process() is the only audio-thread method.
*/
class ConvolutionEngine
{
public:
    ConvolutionEngine() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** A new raw IR was loaded: pick the engine by length, bake, load, and (if the
        engine choice changed) start a warm-up transition. Returns the latency in
        samples of the engine that will be audible. `outBaked` receives the windowed
        IR for display. */
    int loadIR (const juce::AudioBuffer<float>& raw, double irSampleRate,
                const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked);

    /** A bake param changed: re-window the same raw IR and reload it into the
        audible (or transitioning-to) engine. Latency is unchanged. */
    void rebake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                 const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked);

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

    /** The real head latency of the long engine for a given host block size.
        Mirrors juce::dsp::Convolution (8.0.6): nextPowerOfTwo (max (blockSize,
        requested latency)). The unit test cross-checks this against the live
        engine so a JUCE upgrade that changes the rule fails loudly. */
    static int longEngineLatencyForBlockSize (int blockSize) noexcept
    {
        return juce::nextPowerOfTwo (juce::jmax (blockSize, kLongLatency));
    }

    /** Test canary only: the latency JUCE itself reports for the long engine. */
    int getJuceReportedLongLatency() const { return longEngine.getLatency(); }

    static constexpr int    kLongLatency      = 512; // requested long-engine latency, samples
    static constexpr double kThresholdSeconds = 1.5; // raw-length boundary between engines

private:
    juce::dsp::Convolution&       engineAt (int index)       noexcept { return index == 1 ? longEngine : shortEngine; }
    const juce::dsp::Convolution& engineAt (int index) const noexcept { return index == 1 ? longEngine : shortEngine; }

    void loadInto (int engineIndex, const juce::AudioBuffer<float>& baked, double irSampleRate);
    int  computeLatency (int engineIndex) const noexcept;

    /** Kernel size after JUCE resamples it to the engine rate (mirrors
        juce_Convolution.cpp's resampleImpulseResponse length). */
    static int expectedKernelSize (int bakedLen, double irSampleRate, double engineSampleRate) noexcept;

    juce::dsp::Convolution shortEngine { juce::dsp::Convolution::Latency { 0 } };
    juce::dsp::Convolution longEngine  { juce::dsp::Convolution::Latency { kLongLatency } };

    // --- message-thread bookkeeping ---
    int    targetEngine   = 0;                 // engine that is (or will become) audible
    bool   engineUsed[2]  = { false, false };  // has each engine ever received a kernel?
    bool   haveKernel     = false;
    int    latencySamples = 0;
    double prepSampleRate = 48000.0;
    int    maxBlockSize   = 0;

    // --- message -> audio transition request (audio thread never writes these) ---
    std::atomic<int> pendingTarget { -1 };     // engine index to warm up
    std::atomic<int> pendingSize   { 0 };      // expected resampled kernel size
    std::atomic<int> transitionGen { 0 };      // bumped to (re)arm the audio thread

    // --- audio -> message state ---
    std::atomic<int>  active { 0 };            // audible engine (audio thread flips on completion)
    std::atomic<bool> loaded { false };        // a kernel is confirmed live

    // --- audio-thread-only transition state ---
    enum class Phase { idle, waiting, settling, fading };
    Phase phase           = Phase::idle;
    int   lastGenSeen     = 0;
    int   transTarget     = -1;
    int   settleLeft      = 0;
    int   xfadeLeft       = 0;
    int   settleSamples   = 0;                 // covers JUCE's internal install crossfade
    int   xfadeSamples    = 0;                 // our old -> new output crossfade
    juce::AudioBuffer<float> scratch;          // target engine warm-up buffer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvolutionEngine)
};
