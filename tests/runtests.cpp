// Convo headless test runner — exercises the bake pipeline, engine selection and
// the isolated DSP math, without a live (async) convolution or any GUI.
//
// Build:  make -C tests
// Run:    ./tests/runtests
//
// Every test names its archetype (see TEST_PLAN.md). Tolerances carry a "because" reason.

#include <JuceHeader.h>
#include "../Source/ConvolutionEngine.h"
#include "../Source/IRLibrary.h"
#include "../Source/SoftClip.h"
#include "../Source/MidSide.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gPasses = 0;
int gFails  = 0;

void expectNear (double actual, double expected, double tol, const char* label)
{
    const double err = std::abs (actual - expected);
    if (err <= tol) { ++gPasses; std::printf ("  [PASS] %s  (%.6g ~= %.6g, err %.3g)\n", label, actual, expected, err); }
    else            { ++gFails;  std::printf ("  [FAIL] %s  (%.6g vs %.6g, err %.3g > tol %.3g)\n", label, actual, expected, err, tol); }
}

void expectTrue (bool cond, const char* label)
{
    if (cond) { ++gPasses; std::printf ("  [PASS] %s\n", label); }
    else      { ++gFails;  std::printf ("  [FAIL] %s\n", label); }
}

IRBakeParams plainBake()   // no shaping: identity windowing (raw level, so the
{                          // analytic envelope tests see unscaled samples)
    IRBakeParams bp;
    bp.fadeInMs = 0.0f; bp.decayOff = true; bp.decaySeconds = 0.0f;
    bp.taperMs = 0.0f;  bp.reverse = false; bp.autoLevel = false;
    return bp;
}

juce::AudioBuffer<float> dcBuffer (int numSamples, float value = 1.0f)
{
    juce::AudioBuffer<float> b (1, numSamples);
    for (int i = 0; i < numSamples; ++i) b.setSample (0, i, value);
    return b;
}

constexpr double kFs = 48000.0;

// =========================================================================
// bake() — reverse  (archetype: pure identity)
// =========================================================================
void test_bake_reverse()
{
    std::printf ("\n== bake: reverse ==\n");
    juce::AudioBuffer<float> raw (1, 8);
    for (int i = 0; i < 8; ++i) raw.setSample (0, i, (float) i);   // 0..7

    auto bp = plainBake(); bp.reverse = true;
    auto out = ConvolutionEngine::bake (raw, kFs, bp);

    expectTrue (out.getNumSamples() == 8, "reverse keeps length");
    bool ok = true;
    for (int i = 0; i < 8; ++i) ok = ok && juce::approximatelyEqual (out.getSample (0, i), (float) (7 - i));
    expectTrue (ok, "reverse mirrors samples exactly");
}

// =========================================================================
// bake() — fade-in raised cosine  (archetype: time-domain analytic)
// =========================================================================
void test_bake_fadein()
{
    std::printf ("\n== bake: fade-in (raised cosine) ==\n");
    auto raw = dcBuffer (2000, 1.0f);                  // DC, so out == envelope
    auto bp  = plainBake(); bp.fadeInMs = 10.0f;       // N = 480 @ 48k
    auto out = ConvolutionEngine::bake (raw, kFs, bp);

    const int N = (int) std::round (10.0 * 0.001 * kFs);   // 480
    // because: g(i) = 0.5 - 0.5*cos(pi*i/N); pure float formula
    expectNear (out.getSample (0, 0),     0.0, 1e-6, "fade-in g(0) = 0");
    expectNear (out.getSample (0, N / 2), 0.5, 2e-3, "fade-in g(N/2) ~= 0.5");
    expectNear (out.getSample (0, N),     1.0, 1e-6, "fade-in g(N) = 1 (DC past ramp)");
}

// =========================================================================
// bake() — fade-in capped at 80% of the sample length  (archetype: property)
// =========================================================================
void test_bake_fadein_cap()
{
    std::printf ("\n== bake: fade-in 80%% length cap ==\n");
    auto raw = dcBuffer (2000, 1.0f);
    auto bp  = plainBake(); bp.fadeInMs = 100000.0f;   // absurdly long -> must cap at 0.8 * n = 1600
    auto out = ConvolutionEngine::bake (raw, kFs, bp);

    expectNear (out.getSample (0, 0),    0.0, 1e-6, "fade-in still starts at 0");
    expectNear (out.getSample (0, 800),  0.5, 2e-3, "ramp hits 0.5 at 40% (half of the 1600-samp ramp)");
    expectNear (out.getSample (0, 1600), 1.0, 1e-6, "fade-in capped at 80% (sample at 80% is full level)");
    expectNear (out.getSample (0, 1999), 1.0, 1e-6, "the last 20% of the IR stays at full level");
}

// =========================================================================
// bake() — decay envelope  (archetype: time-domain analytic)
// =========================================================================
void test_bake_decay_envelope()
{
    std::printf ("\n== bake: decay envelope ==\n");
    auto raw = dcBuffer (12000, 1.0f);
    auto bp  = plainBake(); bp.decayOff = false; bp.decaySeconds = 0.1f;  // decaySamps = 4800

    auto out = ConvolutionEngine::bake (raw, kFs, bp);
    const double decaySamps = 0.1 * kFs;
    const int    half = (int) (decaySamps * 0.5);   // 2400 -> -30 dB

    const double dbHalf = juce::Decibels::gainToDecibels ((double) out.getSample (0, half));
    // because: g(t)=10^(-3t/decaySamps) is exact; float pow rounding ~1e-4
    expectNear (dbHalf, -30.0, 0.1, "decay at decaySamps/2 = -30 dB");
    expectNear (juce::Decibels::gainToDecibels ((double) out.getSample (0, 0)), 0.0, 1e-4, "decay starts at 0 dB");
}

// =========================================================================
// bake() — decay truncation length  (archetype: state / property)
// =========================================================================
void test_bake_decay_truncation()
{
    std::printf ("\n== bake: decay truncation ==\n");
    auto raw = dcBuffer (20000, 1.0f);
    auto bp  = plainBake(); bp.decayOff = false; bp.decaySeconds = 0.1f;  // -60 dB at t = 4800

    auto out = ConvolutionEngine::bake (raw, kFs, bp);
    // because: truncate at first t where 10^(-3t/4800) < 0.001  => t > 4800.
    // pow() rounding at the threshold makes this +-2 samples.
    expectTrue (std::abs (out.getNumSamples() - 4801) <= 2, "truncates near -60 dB point (~4801 samp)");
    expectTrue (out.getNumSamples() < 20000, "truncation actually shortened the IR");
}

// =========================================================================
// bake() — tail-taper de-click  (archetype: state / property)
// =========================================================================
void test_bake_taper_declick()
{
    std::printf ("\n== bake: tail-taper ==\n");
    auto raw = dcBuffer (2000, 1.0f);
    auto bp  = plainBake(); bp.taperMs = 5.0f;   // 240 samples
    auto out = ConvolutionEngine::bake (raw, kFs, bp);

    const int n = out.getNumSamples();
    expectTrue (n == 2000, "taper alone does not truncate (decayOff)");
    expectNear (out.getSample (0, n / 2), 1.0, 1e-4, "pre-taper region untouched (= 1)");
    expectTrue (std::abs (out.getSample (0, n - 1)) < 0.01f, "last sample ~= 0 (de-clicked)");
}

// =========================================================================
// bake() — decayOff keeps length  (archetype: property)
// =========================================================================
void test_bake_decayoff_length()
{
    std::printf ("\n== bake: decayOff ==\n");
    auto raw = dcBuffer (3000, 1.0f);
    auto out = ConvolutionEngine::bake (raw, kFs, plainBake());   // decayOff, no taper/fade
    expectTrue (out.getNumSamples() == 3000, "decayOff + no shaping keeps full length");
    expectNear (out.getSample (0, 1500), 1.0, 1e-6, "samples pass through unchanged");
}

// =========================================================================
// bake() — auto-level  (archetype: property / level policy)
// One gain for all channels: loudest channel lands at unit energy, stereo
// balance is preserved, a silent kernel is left untouched.
// =========================================================================
double channelL2 (const juce::AudioBuffer<float>& b, int ch)
{
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        e += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);
    return std::sqrt (e);
}

void test_bake_autolevel()
{
    std::printf ("\n== bake: auto-level ==\n");
    auto bp = plainBake(); bp.autoLevel = true;

    // stereo, ch0 twice as hot as ch1 -> ch0 lands at L2=1, ch1 at 0.5
    juce::AudioBuffer<float> raw (2, 9600);
    for (int i = 0; i < 9600; ++i) { raw.setSample (0, i, 0.5f); raw.setSample (1, i, 0.25f); }
    auto out = ConvolutionEngine::bake (raw, kFs, bp);
    expectNear (channelL2 (out, 0), 1.0, 1e-3, "loudest channel scaled to unit energy");
    expectNear (channelL2 (out, 1), 0.5, 1e-3, "stereo balance preserved (one shared gain)");

    // a true (delta-like) IR is just brought to exactly unit energy
    juce::AudioBuffer<float> delta (1, 4800); delta.clear(); delta.setSample (0, 0, 0.8f);
    auto outD = ConvolutionEngine::bake (delta, kFs, bp);
    expectNear (channelL2 (outD, 0), 1.0, 1e-4, "delta IR -> unit energy");

    // raw mode: untouched
    auto rawMode = plainBake();
    auto outR = ConvolutionEngine::bake (raw, kFs, rawMode);
    expectNear (outR.getSample (0, 100), 0.5, 1e-6, "raw mode leaves the level alone");

    // silent kernel: no gain explosion
    juce::AudioBuffer<float> silent (1, 4800); silent.clear();
    auto outS = ConvolutionEngine::bake (silent, kFs, bp);
    expectNear (outS.getMagnitude (0, 0, outS.getNumSamples()), 0.0, 1e-9, "silent IR stays silent");
}

// =========================================================================
// softClip — final-output safety ceiling  (archetype: pure function)
// =========================================================================
void test_softclip()
{
    std::printf ("\n== softClip ==\n");
    // because: float in/out — 0.7f isn't exactly 0.7, and tanh saturates to exactly
    // 1.0f for huge arguments, so the bounds are "never exceeds", not "never reaches"
    expectNear (convo::softClip (0.5f),  0.5,  1e-6, "transparent below the knee (+)");
    expectNear (convo::softClip (-0.7f), -0.7, 1e-6, "transparent below the knee (-)");
    expectNear (convo::softClip (convo::kClipKnee + 0.001f), convo::kClipKnee + 0.001, 1e-4,
                "C1-continuous just above the knee");
    expectTrue (convo::softClip (200.0f) <= 1.0f && convo::softClip (200.0f) > 0.99f,
                "huge input pinned at the 1.0 ceiling");
    expectTrue (convo::softClip (-50.0f) >= -1.0f && convo::softClip (-50.0f) < -0.99f,
                "negative ceiling symmetric");
    bool monotone = true;
    float prev = -2.0f;
    for (float x = -2.0f; x <= 2.0f; x += 0.01f)
    {
        const float y = convo::softClip (x);
        monotone = monotone && y >= convo::softClip (prev) - 1e-6f;
        prev = x;
    }
    expectTrue (monotone, "monotonic across the knee");
}

// =========================================================================
// engine + auto-level — hot dense "IR" comes out at a musical level
// (archetype: end-to-end level policy; this is the erokia blow-out scenario)
// =========================================================================
void test_autolevel_tames_hot_ir()
{
    std::printf ("\n== auto-level: hot dense IR ==\n");
    ConvolutionEngine eng;
    constexpr int blockLen = 512;
    juce::dsp::ProcessSpec spec { kFs, blockLen, 2 };
    eng.prepare (spec);

    // 1.2 s of full-scale noise as the "IR" — raw, this convolves ~+35 dB hot
    juce::Random irRng (99);
    juce::AudioBuffer<float> ir (2, (int) (1.2 * kFs));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < ir.getNumSamples(); ++i)
            ir.setSample (c, i, irRng.nextFloat() * 2.0f - 1.0f);

    IRBakeParams bp;                       // plugin defaults: autoLevel on
    juce::AudioBuffer<float> baked;
    eng.loadIR (ir, kFs, bp, baked);

    juce::AudioBuffer<float> buf (2, blockLen);
    juce::Random rng (7);
    int guard = 0;
    while (! eng.hasIR() && guard++ < 4000)
    {
        buf.clear();
        juce::dsp::AudioBlock<float> block (buf);
        eng.process (block);
        juce::Thread::sleep (1);
    }
    expectTrue (eng.hasIR(), "kernel went live");

    double peak = 0.0;
    for (int b = 0; b < (int) (2.0 * kFs / blockLen); ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockLen; ++i)
                buf.setSample (c, i, rng.nextFloat() * 2.0f - 1.0f);
        juce::dsp::AudioBlock<float> block (buf);
        eng.process (block);
        for (int c = 0; c < 2; ++c)
            peak = juce::jmax (peak, (double) buf.getMagnitude (c, 0, blockLen));
    }
    // because: unit-energy kernel * 0 dBFS noise ~ 0 dB RMS wet; crest stays modest.
    // raw level produced ~190x (+45 dB) here — anything < 4 proves the policy works.
    expectTrue (peak > 0.1,  "wet path is alive");
    expectTrue (peak < 4.0,  "auto-level keeps a full-scale dense IR musical (< +12 dB)");
    std::printf ("        (wet peak with 0 dBFS noise: %.2f)\n", peak);
}

// =========================================================================
// loadIR — adaptive engine selection + reported latency  (archetype: latency/state)
// =========================================================================
void test_engine_selection()
{
    std::printf ("\n== engine selection / latency ==\n");
    ConvolutionEngine eng;
    juce::dsp::ProcessSpec spec { kFs, 512, 1 };
    eng.prepare (spec);

    auto bp = plainBake();
    juce::AudioBuffer<float> baked;
    auto mk = [] (double sec) { return dcBuffer ((int) std::round (sec * kFs), 1.0f); };

    auto s1 = mk (1.0);  expectTrue (eng.loadIR (s1, kFs, bp, baked) == 0,   "1.0 s IR -> latency 0 (short engine)");
    auto s2 = mk (2.0);  expectTrue (eng.loadIR (s2, kFs, bp, baked) == 512, "2.0 s IR -> latency 512 (long engine)");
    auto sb = mk (1.5);  expectTrue (eng.loadIR (sb, kFs, bp, baked) == 512, "exactly 1.5 s (threshold) -> 512");
    auto su = dcBuffer ((int) std::round (1.5 * kFs) - 1, 1.0f);
    expectTrue (eng.loadIR (su, kFs, bp, baked) == 0, "just under 1.5 s -> 0");
    expectTrue (eng.getLatencySamples() == 0, "getLatencySamples() matches last selection");
}

// =========================================================================
// loadIR — real latency tracks the host block size  (archetype: latency)
// juce::dsp::Convolution's non-zero-latency engine actually reports
// nextPowerOfTwo (max (blockSize, requested)); Convo must publish that, not
// the requested constant. The canary cross-checks our mirrored formula
// against the live JUCE engine so a JUCE upgrade can't silently break it.
// =========================================================================
void test_engine_latency_tracks_blocksize()
{
    std::printf ("\n== engine latency vs block size ==\n");
    ConvolutionEngine eng;
    juce::dsp::ProcessSpec spec { kFs, 2048, 1 };
    eng.prepare (spec);

    expectTrue (ConvolutionEngine::longEngineLatencyForBlockSize (2048) == 2048,
                "formula: block 2048 -> latency 2048");
    expectTrue (ConvolutionEngine::longEngineLatencyForBlockSize (768) == 1024,
                "formula: block 768 -> latency 1024 (next pow2)");
    expectTrue (ConvolutionEngine::longEngineLatencyForBlockSize (128) == 512,
                "formula: small blocks -> requested 512");

    expectTrue (eng.getJuceReportedLongLatency() == 2048,
                "canary: JUCE engine agrees with the mirrored formula @ 2048");

    auto bp = plainBake();
    juce::AudioBuffer<float> baked;
    auto ir = dcBuffer ((int) std::round (2.0 * kFs), 1.0f);
    expectTrue (eng.loadIR (ir, kFs, bp, baked) == 2048,
                "2.0 s IR @ block 2048 -> published latency 2048");
}

// =========================================================================
// process() — warm-up transition: no pass-through leak  (archetype: state machine)
// The async kernel build means a freshly-targeted engine briefly holds JUCE's
// unit-impulse (pass-through) kernel. The transition must keep that inaudible:
// with a 0.5-scaled delta IR and DC input, any leak would read 1.0 while the
// legitimate wet output reads 0.5.
// =========================================================================
void test_transition_no_passthrough()
{
    std::printf ("\n== transition: no pass-through leak ==\n");
    ConvolutionEngine eng;
    constexpr int blockLen = 512;
    juce::dsp::ProcessSpec spec { kFs, blockLen, 1 };
    eng.prepare (spec);

    expectTrue (! eng.hasIR(), "fresh engine reports no IR");

    juce::AudioBuffer<float> ir (1, (int) (0.25 * kFs));
    ir.clear();
    ir.setSample (0, 0, 0.5f);                       // half-gain delta
    auto bp = plainBake();
    juce::AudioBuffer<float> baked;
    eng.loadIR (ir, kFs, bp, baked);

    juce::AudioBuffer<float> buf (1, blockLen);
    float maxAbs = 0.0f, lastBlockMax = 0.0f;
    bool wentLive = false;

    // pump up to ~6 s of blocks; the background build + settle + crossfade
    // completes in well under a second on any machine
    for (int blk = 0; blk < 560 && ! (wentLive && lastBlockMax > 0.45f); ++blk)
    {
        for (int i = 0; i < blockLen; ++i) buf.setSample (0, i, 1.0f);   // DC input
        juce::dsp::AudioBlock<float> block (buf);
        eng.process (block);

        lastBlockMax = buf.getMagnitude (0, 0, blockLen);
        maxAbs = juce::jmax (maxAbs, lastBlockMax);
        wentLive = wentLive || eng.hasIR();
        juce::Thread::sleep (2);
    }

    expectTrue (wentLive, "transition completed (hasIR true)");
    // because: a dirac leak outputs 1.0; legit wet is 0.5; crossfade stays within
    expectTrue (maxAbs < 0.55f, "no pass-through leak during warm-up (max < 0.55)");
    expectNear (lastBlockMax, 0.5, 0.02, "steady-state wet = 0.5 (kernel live)");
}

// =========================================================================
// process() — silence when no IR loaded  (archetype: state guard)
// =========================================================================
void test_process_silence_unloaded()
{
    std::printf ("\n== process: unloaded -> silence ==\n");
    ConvolutionEngine eng;
    juce::dsp::ProcessSpec spec { kFs, 512, 2 };
    eng.prepare (spec);

    juce::AudioBuffer<float> buf (2, 512);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) buf.setSample (ch, i, 1.0f);

    juce::dsp::AudioBlock<float> block (buf);
    eng.process (block);

    bool allZero = true;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) allZero = allZero && (buf.getSample (ch, i) == 0.0f);
    expectTrue (allZero, "unloaded engine outputs silence (block cleared)");
}

// -------------------------------------------------------------------------
// Tilt-EQ recipe, measured in isolation (the processor wires this onto the wet
// bus; here we test the formula). archetype: frequency-domain (sine-correlation)
// -------------------------------------------------------------------------
double measureTiltDb (double tiltDb, double freq)
{
    auto loC = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (kFs, 700.0f, 0.5f, juce::Decibels::decibelsToGain (-(float) tiltDb));
    auto hiC = juce::dsp::IIR::Coefficients<float>::makeHighShelf (kFs, 700.0f, 0.5f, juce::Decibels::decibelsToGain ( (float) tiltDb));
    juce::dsp::IIR::Filter<float> lo, hi;
    juce::dsp::ProcessSpec spec { kFs, 512, 1 };
    lo.prepare (spec); hi.prepare (spec);
    lo.coefficients = loC; hi.coefficients = hiC;
    lo.reset(); hi.reset();

    const int warm = (int) (kFs * 0.3), meas = (int) (kFs * 0.3), total = warm + meas;
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kFs;
    double inS = 0, inC = 0, oS = 0, oC = 0;
    for (int n = 0; n < total; ++n)
    {
        const float x = (float) std::sin (w * n);
        const float y = hi.processSample (lo.processSample (x));
        if (n >= warm) { const double s = std::sin (w * n), c = std::cos (w * n); inS += x*s; inC += x*c; oS += y*s; oC += y*c; }
    }
    return 20.0 * std::log10 (std::sqrt (oS*oS + oC*oC) / std::sqrt (inS*inS + inC*inC));
}

void test_tilt_response()
{
    std::printf ("\n== tilt tone (isolated recipe) ==\n");
    // tone = 0 -> flat
    for (double f : { 100.0, 700.0, 5000.0 })
        expectNear (measureTiltDb (0.0, f), 0.0, 0.3, ("tone=0 flat @ " + std::to_string ((int) f) + " Hz").c_str());

    // tone = +100% -> tiltDb = +12. Pivot ~0; LF darkened, HF brightened.
    // because: shelf Q=0.5 reaches its asymptote within ~2.5 octaves of 700 Hz;
    // direction + pivot are the invariants, magnitude floor is conservative.
    expectNear (measureTiltDb (12.0, 700.0), 0.0, 1.5, "tilt pivot ~0 dB at 700 Hz");
    expectTrue (measureTiltDb (12.0, 100.0)  < -8.0, "tilt darkens LF (<-8 dB @ 100 Hz)");
    expectTrue (measureTiltDb (12.0, 5000.0) >  8.0, "tilt brightens HF (>+8 dB @ 5 kHz)");
    // symmetry: negative tilt mirrors
    expectNear (measureTiltDb (-12.0, 5000.0), -measureTiltDb (12.0, 5000.0), 0.5, "tilt is symmetric in tone sign");
}

// -------------------------------------------------------------------------
// M/S width transform, in isolation  (archetype: static fixture / property)
// -------------------------------------------------------------------------
void msWidth (float& L, float& R, float w)
{
    const float mid = 0.5f * (L + R);
    const float side = 0.5f * (L - R) * w;
    L = mid + side; R = mid - side;
}

void test_ms_width()
{
    std::printf ("\n== M/S width ==\n");
    { float L = 0.3f, R = -0.7f; msWidth (L, R, 1.0f);
      expectNear (L,  0.3, 1e-6, "width=100% identity (L)"); expectNear (R, -0.7, 1e-6, "width=100% identity (R)"); }
    { float L = 1.0f, R = -1.0f; msWidth (L, R, 0.0f);       // pure side -> collapses to 0
      expectNear (L, 0.0, 1e-6, "width=0 collapses pure-side L"); expectNear (R, 0.0, 1e-6, "width=0 collapses pure-side R"); }
    { float L = 0.4f, R = 0.4f; msWidth (L, R, 0.0f);        // pure mid -> preserved
      expectNear (L, 0.4, 1e-6, "width=0 keeps mono content"); }
    { float L = 1.0f, R = -1.0f; msWidth (L, R, 2.0f);
      expectNear (L, 2.0, 1e-6, "width=200% doubles side (L)"); expectNear (R, -2.0, 1e-6, "width=200% doubles side (R)"); }
}

// -------------------------------------------------------------------------
// IRLibrary::isSupported  (archetype: pure identity / branches)
// isSupported is now derived from the registered decoders, so .mp3 acceptance
// follows JUCE_USE_MP3AUDIOFORMAT instead of being hard-coded.
// -------------------------------------------------------------------------
void test_issupported()
{
    std::printf ("\n== IRLibrary::isSupported ==\n");
    IRLibrary lib;
    expectTrue ( lib.isSupported (juce::File ("/x/a.wav")),  ".wav supported");
    expectTrue ( lib.isSupported (juce::File ("/x/a.WAV")),  ".WAV supported (case-insensitive)");
    expectTrue ( lib.isSupported (juce::File ("/x/a.aiff")), ".aiff supported");
    expectTrue ( lib.isSupported (juce::File ("/x/a.flac")), ".flac supported");
   #if JUCE_USE_MP3AUDIOFORMAT
    expectTrue ( lib.isSupported (juce::File ("/x/a.mp3")),  ".mp3 supported (decoder registered)");
   #else
    expectTrue (!lib.isSupported (juce::File ("/x/a.mp3")),  ".mp3 rejected (no decoder in this build)");
   #endif
    expectTrue (!lib.isSupported (juce::File ("/x/a.txt")),  ".txt rejected");
    expectTrue (!lib.isSupported (juce::File ("/x/a.png")),  ".png rejected");
}

// -------------------------------------------------------------------------
// IRLibrary decode cap  (archetype: resource bound / state)
// -------------------------------------------------------------------------
juce::File writeTestWav (const juce::String& name, int numChannels, double sampleRate, int numSamples)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();

    juce::WavAudioFormat wav;
    auto* stream = new juce::FileOutputStream (file);
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream, sampleRate, (unsigned int) numChannels, 16, {}, 0));
    if (writer == nullptr) { delete stream; return {}; }

    juce::AudioBuffer<float> buf (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buf.setSample (ch, i, 0.25f);
    writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
    return file;
}

void test_irlibrary_cap()
{
    std::printf ("\n== IRLibrary: decode cap ==\n");
    constexpr double sr = 8000.0;   // small rate keeps the test file tiny

    // 35 s file -> truncated to kMaxSeconds (30 s)
    const auto longFile = writeTestWav ("convo_test_35s.wav", 1, sr, (int) (35.0 * sr));
    IRLibrary lib;
    expectTrue (longFile.existsAsFile() && lib.loadFile (longFile), "35 s wav loads");
    expectTrue (lib.getIR().getNumSamples() == (int) (IRLibrary::kMaxSeconds * sr),
                "decode stops at kMaxSeconds (30 s)");
    expectTrue (lib.wasTruncated(), "truncation is flagged");
    expectTrue (lib.getDisplayName().contains ("truncated"), "display name mentions truncation");

    // short file -> untouched, not flagged
    const auto shortFile = writeTestWav ("convo_test_1s.wav", 1, sr, (int) sr);
    expectTrue (lib.loadFile (shortFile), "1 s wav loads");
    expectTrue (lib.getIR().getNumSamples() == (int) sr, "short file keeps full length");
    expectTrue (! lib.wasTruncated(), "no truncation flag on short file");

    // 4-channel file -> clamped to the 2 channels the convolution can use
    const auto multiFile = writeTestWav ("convo_test_4ch.wav", 4, sr, (int) sr);
    expectTrue (lib.loadFile (multiFile), "4-channel wav loads");
    expectTrue (lib.getIR().getNumChannels() == 2, "channels clamped to 2");

    longFile.deleteFile(); shortFile.deleteFile(); multiFile.deleteFile();
}

// -------------------------------------------------------------------------
// Convolution stability smoke (delta IR, warm-up implicit)  (archetype: CPU/stability)
// -------------------------------------------------------------------------
void test_conv_nan_smoke()
{
    std::printf ("\n== convolution NaN/finite smoke ==\n");
    ConvolutionEngine eng;
    juce::dsp::ProcessSpec spec { kFs, 512, 2 };
    eng.prepare (spec);

    juce::AudioBuffer<float> ir (2, 256); ir.clear();
    ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 1.0f);   // unit delta -> passthrough once live
    auto bp = plainBake(); juce::AudioBuffer<float> baked;
    eng.loadIR (ir, kFs, bp, baked);

    juce::Random rng (0x51A7);
    juce::AudioBuffer<float> buf (2, 512);
    bool finite = true; float maxAbs = 0.0f;
    const int blocks = (int) (2.0 * kFs / 512.0);   // ~2 s
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        juce::dsp::AudioBlock<float> block (buf);
        eng.process (block);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i)
        {
            const float v = buf.getSample (ch, i);
            finite = finite && std::isfinite (v);
            maxAbs = juce::jmax (maxAbs, std::abs (v));
        }
    }
    expectTrue (finite,         "convolution output finite over 2 s of noise (delta IR)");
    expectTrue (maxAbs < 8.0f,  "convolution output bounded (<8) with unit-delta IR");
}

// =========================================================================
// Mid/Side encode-decode round-trip  (archetype: pure identity / shared math)
// =========================================================================
void test_midside_roundtrip()
{
    std::printf ("\n== M/S encode/decode round-trip ==\n");
    constexpr int n = 64;
    std::vector<float> L (n), R (n), L0 (n), R0 (n);
    juce::Random rng (1234);
    for (int i = 0; i < n; ++i) { L[i] = L0[i] = rng.nextFloat() * 2.0f - 1.0f;
                                  R[i] = R0[i] = rng.nextFloat() * 2.0f - 1.0f; }

    convo::msEncode (L.data(), R.data(), n);
    expectNear (L[0], 0.5 * (L0[0] + R0[0]), 1e-6, "encode: mid = (L+R)/2");
    expectNear (R[0], 0.5 * (L0[0] - R0[0]), 1e-6, "encode: side = (L-R)/2");

    convo::msDecode (L.data(), R.data(), n);
    bool ok = true;
    for (int i = 0; i < n; ++i) ok = ok && std::abs (L[i] - L0[i]) < 1e-6f && std::abs (R[i] - R0[i]) < 1e-6f;
    expectTrue (ok, "encode -> decode is sample-exact (lossless round-trip)");
}

// =========================================================================
// bake() — mid/side kernel encoding  (archetype: property)
// =========================================================================
void test_bake_ms_kernel()
{
    std::printf ("\n== bake: mid/side kernel ==\n");
    auto bp = plainBake(); bp.msMode = true;

    // stereo IR (L delta 1.0, R delta 0.5) -> kernel ch0 = mid 0.75, ch1 = side 0.25
    juce::AudioBuffer<float> ir (2, 16); ir.clear();
    ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 0.5f);
    auto k = ConvolutionEngine::bake (ir, kFs, bp);
    expectTrue (k.getNumChannels() == 2, "M/S kernel is stereo");
    expectNear (k.getSample (0, 0), 0.75, 1e-6, "kernel mid  = (L+R)/2");
    expectNear (k.getSample (1, 0), 0.25, 1e-6, "kernel side = (L-R)/2");

    // mono IR has no side -> expands to [mid = mono, side = 0] (collapses to mono)
    juce::AudioBuffer<float> mono (1, 16); mono.clear(); mono.setSample (0, 0, 1.0f);
    auto km = ConvolutionEngine::bake (mono, kFs, bp);
    expectTrue (km.getNumChannels() == 2, "mono IR expands to stereo for M/S");
    expectNear (km.getSample (0, 0), 1.0, 1e-6, "mono M/S: mid = the mono IR");
    expectNear (km.getMagnitude (1, 0, km.getNumSamples()), 0.0, 1e-9, "mono M/S: side kernel = 0");
}

// =========================================================================
// processBlock routing: M/S convolution end-to-end via the live engine
// (archetype: integration). Uses the same convo::msEncode/Decode the audio
// thread runs, a delta IR so the result is closed-form.
// =========================================================================
void test_ms_routing_endtoend()
{
    std::printf ("\n== M/S routing (engine end-to-end) ==\n");
    ConvolutionEngine eng;
    constexpr int blockLen = 256;
    eng.prepare ({ kFs, (juce::uint32) blockLen, 2 });

    auto bp = plainBake(); bp.msMode = true;
    juce::AudioBuffer<float> ir (2, 32); ir.clear();
    ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 0.5f);    // M/S kernel: mid 0.75, side 0.25
    juce::AudioBuffer<float> baked; eng.loadIR (ir, kFs, bp, baked);

    juce::AudioBuffer<float> buf (2, blockLen);
    int guard = 0;
    while (! eng.hasIR() && guard++ < 4000)
    { buf.clear(); juce::dsp::AudioBlock<float> b (buf); eng.process (b); juce::Thread::sleep (1); }
    expectTrue (eng.hasIR(), "M/S kernel went live");

    // input L=0.4, R=0.1 -> M=0.25, S=0.15; conv (mid 0.75, side 0.25) -> M_out 0.1875, S_out 0.0375
    // decode -> L = 0.225, R = 0.15. Drive a few blocks so the (delta) convolution settles.
    for (int b = 0; b < 8; ++b)
    {
        for (int i = 0; i < blockLen; ++i) { buf.setSample (0, i, 0.4f); buf.setSample (1, i, 0.1f); }
        convo::msEncode (buf.getWritePointer (0), buf.getWritePointer (1), blockLen);   // as processBlock does
        juce::dsp::AudioBlock<float> blk (buf); eng.process (blk);
        convo::msDecode (buf.getWritePointer (0), buf.getWritePointer (1), blockLen);
    }
    expectNear (buf.getSample (0, blockLen - 1), 0.225, 2e-3, "routed L = mid*0.75 + side*0.25");
    expectNear (buf.getSample (1, blockLen - 1), 0.15,  2e-3, "routed R = mid*0.75 - side*0.25");
}

// =========================================================================
// bake() — pre-IR filter baked into the kernel (Filter-IR)  (archetype: property)
// =========================================================================
void test_bake_filter_ir()
{
    std::printf ("\n== bake: Filter-IR (kernel-baked HP/LP) ==\n");
    auto raw = dcBuffer (2000, 1.0f);                 // DC kernel

    auto kOff = ConvolutionEngine::bake (raw, kFs, plainBake());   // filterIR defaults off
    expectNear (kOff.getSample (0, 1500), 1.0, 1e-6, "Filter-IR off: kernel unchanged (DC stays)");

    auto bpOn = plainBake();
    bpOn.filterIR = true; bpOn.inHPHz = 1000.0f; bpOn.inLPHz = 20000.0f;
    auto kOn = ConvolutionEngine::bake (raw, kFs, bpOn);
    expectTrue (kOn.getSample (0, 0) > 0.5f, "Filter-IR on: HP passes the step onset (edge)");
    expectTrue (std::abs (kOn.getSample (0, 1500)) < 0.05f, "Filter-IR on: 1 kHz HP removes the kernel's DC");
}

// =========================================================================
// Bass-mono: a side high-pass collapses low frequencies to mono  (archetype: property)
// =========================================================================
void test_bass_mono()
{
    std::printf ("\n== bass-mono (side high-pass) ==\n");
    constexpr int n = 9600;                           // 0.2 s
    juce::AudioBuffer<float> buf (2, n);
    const double w = juce::MathConstants<double>::twoPi * 50.0 / kFs;   // 50 Hz, pure side
    for (int i = 0; i < n; ++i) { const float x = (float) std::sin (w * i);
                                  buf.setSample (0, i, x); buf.setSample (1, i, -x); }

    convo::msEncode (buf.getWritePointer (0), buf.getWritePointer (1), n);   // M = 0, S = x
    const double sideBefore = channelL2 (buf, 1);

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> hp;
    hp.prepare ({ kFs, (juce::uint32) n, 1 });
    *hp.state = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (kFs, 500.0f);
    hp.reset();
    juce::dsp::AudioBlock<float> blk (buf);
    auto side = blk.getSingleChannelBlock (1);
    juce::dsp::ProcessContextReplacing<float> ctx (side);
    hp.process (ctx);

    const double sideAfter = channelL2 (buf, 1);
    expectTrue (sideAfter < 0.5 * sideBefore, "a 500 Hz side high-pass attenuates a 50 Hz side tone (-> mono)");
    std::printf ("        (side L2 %.3f -> %.3f)\n", sideBefore, sideAfter);
}

} // namespace

int main()
{
    std::printf ("Convo headless tests\n====================\n");

    test_bake_reverse();
    test_bake_fadein();
    test_bake_fadein_cap();
    test_bake_decay_envelope();
    test_bake_decay_truncation();
    test_bake_taper_declick();
    test_bake_decayoff_length();
    test_bake_autolevel();
    test_softclip();
    test_autolevel_tames_hot_ir();
    test_engine_selection();
    test_engine_latency_tracks_blocksize();
    test_transition_no_passthrough();
    test_process_silence_unloaded();
    test_tilt_response();
    test_ms_width();
    test_midside_roundtrip();
    test_bake_ms_kernel();
    test_ms_routing_endtoend();
    test_bake_filter_ir();
    test_bass_mono();
    test_issupported();
    test_irlibrary_cap();
    test_conv_nan_smoke();

    std::printf ("\n====================\n%d passed, %d failed\n", gPasses, gFails);
    return gFails == 0 ? 0 : 1;
}
