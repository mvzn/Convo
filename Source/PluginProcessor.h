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
    float getDuckGainReduction() const noexcept { return duckGR.load(); }   // live ducking GR (0..1) for the Duck-knob probe

    // Baked (processed) IR for the editor's thumbnail. All accessed on the message thread.
    int getBakeGeneration() const noexcept { return bakeGeneration.load(); }
    const juce::AudioBuffer<float>& getBakedIR() const noexcept { return bakedIR; }
    double getBakedIRSampleRate() const noexcept { return bakedIRSampleRate; }
    const juce::AudioBuffer<float>& getKernelIR() const noexcept { return audioBakeScratch; }  // trimmed+shaped kernel (the selection layer)

    // IR audition (call on the message thread): play the IR through the output, soloing it.
    // baked = the processed kernel (after trim/stretch/decay/etc.); !baked = the raw decoded IR.
    // Editor-only convenience — audio still runs without the editor.
    void startAudition (bool baked);
    void stopAudition();
    bool isAuditioning() const noexcept { return auditionActive.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr float kDecayMinFrac = 0.04f;   // shortest tail at Decay = 100% (fraction of the baked length)

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
    std::atomic<float>* msBassParam   = nullptr;   // bass-mono crossover (side high-pass), Hz; 20 Hz = off / disengaged
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* widthParam    = nullptr;
    std::atomic<float>* duckParam     = nullptr;
    std::atomic<float>* duckRelParam  = nullptr;
    std::atomic<float>* irStartParam  = nullptr;   // IR head trim (fraction of length)
    std::atomic<float>* irEndParam    = nullptr;   // IR tail trim (fraction of length)
    std::atomic<float>* fadeInParam   = nullptr;
    std::atomic<float>* decayParam    = nullptr;
    std::atomic<float>* taperParam    = nullptr;
    std::atomic<float>* stretchParam  = nullptr;
    std::atomic<float>* dampParam     = nullptr;
    std::atomic<float>* reverseParam  = nullptr;
    std::atomic<float>* irNormParam   = nullptr;
    std::atomic<float>* wetCompParam  = nullptr;
    std::atomic<float>* bypassParam   = nullptr;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    // wet-chain DSP
    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    Duplicator lowShelf, highShelf;                  // tilt tone
    Duplicator inputHP, inputLP;                     // pre-IR input filter (first-order, 6 dB/oct)
    Duplicator sideHP;                               // bass-mono: 2nd-order (Q=0.5, 12 dB/oct) side high-pass
                                                     // post-convolution. Mid stays unfiltered, so center
                                                     // content is phase-flat; Q=0.5 keeps the side phase clean
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLine { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>   dryDelayLine  { 1 };

    // smoothed gains / controls (audio thread)
    juce::SmoothedValue<float> dryGainSm, wetGainSm, irGainSm, outputGainSm, toneSm, widthSm, duckSm, bypassSm, loadFade,
                               noIrSm,               // no-IR auto-bypass: dry at unity while nothing is live
                               wetCompSm,            // adaptive wet gain compensation (dry-referenced)
                               inHPSm, inLPSm,       // pre-IR filter cutoffs (smoothed, per-block coeff rebuild)
                               msBassSm,             // bass-mono crossover cutoff (smoothed)
                               preDelaySm;           // pre-delay length in samples (per-sample setDelay so modulation glides)

    double currentSampleRate = 48000.0;
    int    maxPreDelaySamples = 1;
    int    maxDryDelaySamples = 1;
    int    currentDryDelay = -1;
    float  duckEnv = 0.0f;
    float  wetCompTarget = 1.0f;   // wet-comp ratio held across blocks (frozen while input is quiet)
    bool   prevFilterInput = true; // audio-thread edge detect: reset input filters when re-engaged
    // neutral-stage skip edge detect: reset the filter/delay on re-engage so the resumed block
    // starts clean (each stage is skipped while its control sits at a no-op value)
    bool   prevToneActive = true, prevInHpActive = true, prevInLpActive = true,
           prevBassActive = true, prevPreDelayActive = true;

    std::atomic<int>   dryDelaySamples { 0 };        // engine latency published on load (message thread)
    std::atomic<bool>  loadFadePending { false };    // trigger the click-masking output fade
    std::atomic<int>   bakeGeneration  { 0 };
    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> duckGR      { 0.0f };         // live ducking gain reduction (0..1), published for the editor probe
    std::atomic<float> tailSeconds { 0.0f };         // baked IR length + max pre-delay
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

    // IR audition playback: the message thread fills the inactive buffer and publishes its index
    // (double-buffered so a re-trigger never races the audio thread); the audio thread plays it
    // back resampled to the host rate, replacing the output. No allocation on the audio thread.
    juce::AudioBuffer<float> auditionBuf[2];
    double             auditionBufRate[2] = { 48000.0, 48000.0 };
    int                auditionWriteIdx = 0;          // message-thread only
    std::atomic<int>   auditionReadIdx { -1 };        // published buffer to play; -1 = stopped
    std::atomic<int>   auditionGen     { 0 };         // bumped per start/stop so the audio thread re-reads
    std::atomic<bool>  auditionActive  { false };     // audio-thread playing state (polled by the editor)
    int    auditionGenSeen = 0;                       // audio-thread local
    int    auditionPlayIdx = -1;                      // audio-thread local
    double auditionPos = 0.0;                         // audio-thread local read position (source samples)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvoAudioProcessor)
};
