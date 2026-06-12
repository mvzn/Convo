#pragma once

#include <JuceHeader.h>
#include "IRLibrary.h"
#include "ConvolutionEngine.h"

#include <atomic>

/**
    Convo — a simple, MIDI-free convolution effect.

    Stereo (or mono) insert: the input is convolved with a dropped impulse response and
    mixed back against the dry signal. IR shaping (reverse / fade-in / decay / taper) is
    baked into the kernel on the message thread; the wet signal chain (tilt tone, stereo
    width, pre-delay, ducking) runs real-time on the audio thread. The convolution engine
    is chosen adaptively by IR length, with the dry path delayed to match its latency.
*/
class ConvoAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    ConvoAudioProcessor();
    ~ConvoAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;   // keep the double-precision overload visible

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return juce::jmax (1.0, (double) tailSeconds.load()); }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Convo API (message thread) ---
    bool loadIRFile (const juce::File& file);
    IRLibrary& getIRLibrary() noexcept { return irLibrary; }
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    float getInputLevel()  const noexcept { return inputLevel.load(); }
    float getOutputLevel() const noexcept { return outputLevel.load(); }

    // Baked (processed) IR for the editor's thumbnail. All accessed on the message thread.
    int getBakeGeneration() const noexcept { return bakeGeneration.load(); }
    const juce::AudioBuffer<float>& getBakedIR() const noexcept { return bakedIR; }
    double getBakedIRSampleRate() const noexcept { return bakedIRSampleRate; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr float kDecayOffMs = 10000.0f;   // top of the Decay range == "Off"

private:
    void timerCallback() override;                   // message thread: applies pending IR re-bakes
    IRBakeParams currentBakeParams() const;

    juce::AudioProcessorValueTreeState apvts;
    IRLibrary         irLibrary;
    ConvolutionEngine convolution;

    // raw parameter pointers
    std::atomic<float>* dryParam      = nullptr;
    std::atomic<float>* wetParam      = nullptr;
    std::atomic<float>* outputParam   = nullptr;
    std::atomic<float>* toneParam     = nullptr;
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* duckParam     = nullptr;
    std::atomic<float>* duckRelParam  = nullptr;
    std::atomic<float>* fadeInParam   = nullptr;
    std::atomic<float>* decayParam    = nullptr;
    std::atomic<float>* taperParam    = nullptr;
    std::atomic<float>* reverseParam  = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    // wet-chain DSP
    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    Duplicator lowShelf, highShelf;                  // tilt tone
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLine { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>   dryDelayLine  { 1 };

    // smoothed gains / controls (audio thread)
    juce::SmoothedValue<float> dryGainSm, wetGainSm, outputGainSm, toneSm, widthSm, duckSm, bypassSm, loadFade,
                               noIrSm;               // no-IR auto-bypass: dry at unity while nothing is live

    double currentSampleRate = 48000.0;
    int    maxPreDelaySamples = 1;
    int    maxDryDelaySamples = 1;
    int    currentDryDelay = -1;
    float  duckEnv = 0.0f;

    std::atomic<int>   dryDelaySamples { 0 };        // engine latency published on load (message thread)
    std::atomic<bool>  loadFadePending { false };    // trigger the click-masking output fade
    std::atomic<int>   bakeGeneration  { 0 };
    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> tailSeconds { 0.0f };         // baked IR length + max pre-delay

    // setStateInformation may run off the message thread (Cubase project load);
    // it only stashes the path here and the timer performs the actual load.
    juce::CriticalSection pendingIRPathLock;
    juce::String          pendingIRPath;
    std::atomic<bool>     pendingIRLoad { false };

    // message-thread bake bookkeeping
    IRBakeParams lastBaked;
    IRBakeParams lastSeenBakeParams;                 // debounce: rebake only once values settle
    juce::AudioBuffer<float> bakedIR;
    double bakedIRSampleRate = 48000.0;

    juce::AudioBuffer<float> inWork, wetWork;         // audio-thread work buffers

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessor)
};
