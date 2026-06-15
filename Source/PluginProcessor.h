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

    // --- presets (message thread) ---
    // A preset is the plugin's APVTS state (the same XML getStateInformation writes,
    // including the irPath property). Presets live as *.xml files in a Documents folder.
    static juce::File getPresetsFolder();                       // created if missing
    juce::Array<juce::File> getPresetFiles() const;             // sorted *.xml in the folder
    bool savePreset (const juce::String& presetName);           // write current state -> <name>.xml
    bool loadPreset (const juce::File& presetFile);             // restore state from a preset file

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
    std::atomic<float>* irGainParam   = nullptr;
    std::atomic<float>* outputParam   = nullptr;
    std::atomic<float>* toneParam     = nullptr;
    std::atomic<float>* inHPParam     = nullptr;   // pre-IR high-pass (low cut)
    std::atomic<float>* inLPParam     = nullptr;   // pre-IR low-pass  (high cut)
    std::atomic<float>* filterIRParam = nullptr;   // pre-IR filter target: input (off) or IR (on)
    std::atomic<float>* msParam       = nullptr;   // mid/side convolution mode
    std::atomic<float>* msBassParam   = nullptr;   // bass-mono crossover (side high-pass) in M/S mode
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* feedbackParam = nullptr;   // amount of convolved output fed back into the wet input
    std::atomic<float>* dampParam     = nullptr;   // low-pass cutoff in the feedback path
    std::atomic<float>* duckParam     = nullptr;
    std::atomic<float>* duckRelParam  = nullptr;
    std::atomic<float>* irStartParam  = nullptr;   // IR head trim (fraction of length)
    std::atomic<float>* irEndParam    = nullptr;   // IR tail trim (fraction of length)
    std::atomic<float>* fadeInParam   = nullptr;
    std::atomic<float>* decayParam    = nullptr;
    std::atomic<float>* taperParam    = nullptr;
    std::atomic<float>* stretchParam  = nullptr;
    std::atomic<float>* reverseParam  = nullptr;
    std::atomic<float>* rawLevelParam = nullptr;
    std::atomic<float>* clipGuardParam = nullptr;
    std::atomic<float>* wetCompParam  = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    // wet-chain DSP
    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    Duplicator lowShelf, highShelf;                  // tilt tone
    Duplicator inputHP, inputLP;                     // pre-IR input filter (first-order, 6 dB/oct)
    Duplicator sideHP;                               // bass-mono: high-passes the side in M/S mode
    Duplicator dampLP;                               // damping low-pass in the feedback path
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLine { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>   dryDelayLine  { 1 };

    // smoothed gains / controls (audio thread)
    juce::SmoothedValue<float> dryGainSm, wetGainSm, irGainSm, outputGainSm, toneSm, widthSm, duckSm, bypassSm, loadFade,
                               noIrSm,               // no-IR auto-bypass: dry at unity while nothing is live
                               wetCompSm,            // adaptive wet gain compensation (dry-referenced)
                               inHPSm, inLPSm,       // pre-IR filter cutoffs (smoothed, per-block coeff rebuild)
                               msBassSm,             // bass-mono crossover cutoff (smoothed)
                               feedbackSm,           // feedback amount (0..0.95), smoothed
                               dampSm,               // feedback-path low-pass cutoff (smoothed)
                               clipGuardSm;          // 0..1 blend so toggling the clip guard is click-free

    double currentSampleRate = 48000.0;
    int    maxPreDelaySamples = 1;
    int    maxDryDelaySamples = 1;
    int    currentDryDelay = -1;
    float  duckEnv = 0.0f;
    float  wetCompTarget = 1.0f;   // wet-comp ratio held across blocks (frozen while input is quiet)
    bool   prevMsEncode  = false;  // audio-thread edge detect: reset the side filter on M/S engage
    bool   prevFilterInput = true; // audio-thread edge detect: reset input filters when re-engaged
    // neutral-stage skip edge detect: reset the filter/delay on re-engage so the resumed block
    // starts clean (each stage is skipped while its control sits at a no-op value)
    bool   prevToneActive = true, prevInHpActive = true, prevInLpActive = true,
           prevBassActive = true, prevPreDelayActive = true, prevFeedbackActive = true;

    std::atomic<int>   dryDelaySamples { 0 };        // engine latency published on load (message thread)
    std::atomic<bool>  loadFadePending { false };    // trigger the click-masking output fade
    std::atomic<int>   bakeGeneration  { 0 };
    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> tailSeconds { 0.0f };         // baked IR length + max pre-delay
    std::atomic<bool>  msActive    { false };        // audio thread M/S-encodes iff the live kernel is M/S
    std::atomic<bool>  filterInput { true };         // runtime input filter on iff the kernel is unfiltered

    // setStateInformation may run off the message thread (Cubase project load);
    // it only stashes the path here and the timer performs the actual load.
    juce::CriticalSection pendingIRPathLock;
    juce::String          pendingIRPath;
    std::atomic<bool>     pendingIRLoad { false };

    // message-thread bake bookkeeping
    IRBakeParams lastBaked;
    IRBakeParams lastSeenBakeParams;                 // debounce: rebake only once values settle
    juce::AudioBuffer<float> bakedIR;                // FULL (untrimmed) IR for the display thumbnail
    juce::AudioBuffer<float> audioBakeScratch;       // trimmed kernel handed to the engine (not shown)
    double bakedIRSampleRate = 48000.0;

    juce::AudioBuffer<float> inWork, wetWork;         // audio-thread work buffers
    juce::AudioBuffer<float> feedbackState;           // previous block's damped wet, fed back next block

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessor)
};
