#pragma once

#include <JuceHeader.h>

/** Shaping applied to the raw IR before it becomes the convolution kernel.
    All of these are "bake" controls — changing one re-windows the IR on the
    message thread; none of them touch the audio thread directly. */
struct IRBakeParams
{
    float fadeInMs = 0.0f;     // raised-cosine onset ramp
    float decaySeconds = 0.0f; // exponential tail decay (RT60-ish); ignored if decayOff
    bool  decayOff = true;     // true => use the IR's recorded decay unchanged
    float taperMs = 10.0f;     // raised-cosine de-click ramp at the (truncated) end
    bool  reverse = false;     // reverse the IR before windowing

    bool operator== (const IRBakeParams& o) const noexcept
    {
        return juce::approximatelyEqual (fadeInMs, o.fadeInMs)
            && juce::approximatelyEqual (decaySeconds, o.decaySeconds)
            && decayOff == o.decayOff
            && juce::approximatelyEqual (taperMs, o.taperMs)
            && reverse == o.reverse;
    }
    bool operator!= (const IRBakeParams& o) const noexcept { return ! (*this == o); }
};

/**
    Owns two persistent juce::dsp::Convolution engines and switches between them
    adaptively based on IR length:

      - short IRs (< kThresholdSeconds)  -> zero-latency engine   (Latency{0})
      - long  IRs (>= kThresholdSeconds) -> low-CPU engine        (Latency{kLongLatency})

    The engine (and therefore the reported latency) is chosen only when a NEW raw IR
    is loaded; re-baking with new shaping params reloads the kernel into whichever
    engine is already active, so latency never changes mid-session from a knob.

    All loadIR / rebake calls are MESSAGE THREAD (they allocate + window the IR).
    process() is the only audio-thread method.
*/
class ConvolutionEngine
{
public:
    ConvolutionEngine() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** A new raw IR was loaded: pick the engine by length, bake, and load.
        Returns the reported latency in samples (0 or kLongLatency).
        `outBaked` receives the windowed IR for display. */
    int loadIR (const juce::AudioBuffer<float>& raw, double irSampleRate,
                const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked);

    /** A bake param changed: re-window the same raw IR and reload it into the
        currently-active engine. Latency is unchanged. */
    void rebake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                 const IRBakeParams& bake, juce::AudioBuffer<float>& outBaked);

    /** Audio thread: convolve `block` in place through the active engine.
        Clears the block (wet silence) when no IR is loaded. */
    void process (juce::dsp::AudioBlock<float> block);

    int  getLatencySamples() const noexcept { return latencySamples; }
    bool hasIR() const noexcept { return loaded.load(); }

    /** Window the raw IR into the convolution kernel: reverse -> fade-in -> decay
        (+truncate at -60 dB) -> tail-taper. Pure function of its arguments; exposed
        static so the bake pipeline can be unit-tested without a live convolution. */
    static juce::AudioBuffer<float> bake (const juce::AudioBuffer<float>& raw, double irSampleRate,
                                          const IRBakeParams& bp);

    static constexpr int    kLongLatency    = 512;   // samples, long-IR engine head latency
    static constexpr double kThresholdSeconds = 1.5; // raw-length boundary between engines

private:
    void loadIntoActive (const juce::AudioBuffer<float>& baked, double irSampleRate);

    juce::dsp::Convolution shortEngine { juce::dsp::Convolution::Latency { 0 } };
    juce::dsp::Convolution longEngine  { juce::dsp::Convolution::Latency { kLongLatency } };

    std::atomic<int>  active { 0 };       // 0 = short engine, 1 = long engine (audio thread reads)
    std::atomic<bool> loaded { false };
    int    latencySamples = 0;            // message thread
    double prepSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvolutionEngine)
};
