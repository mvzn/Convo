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

IRBakeParams plainBake()   // no shaping: identity windowing
{
    IRBakeParams bp;
    bp.fadeInMs = 0.0f; bp.decayOff = true; bp.decaySeconds = 0.0f;
    bp.taperMs = 0.0f;  bp.reverse = false;
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
// -------------------------------------------------------------------------
void test_issupported()
{
    std::printf ("\n== IRLibrary::isSupported ==\n");
    IRLibrary lib;
    expectTrue ( lib.isSupported (juce::File ("/x/a.wav")),  ".wav supported");
    expectTrue ( lib.isSupported (juce::File ("/x/a.WAV")),  ".WAV supported (case-insensitive)");
    expectTrue ( lib.isSupported (juce::File ("/x/a.aiff")), ".aiff supported");
    expectTrue ( lib.isSupported (juce::File ("/x/a.flac")), ".flac supported");
    expectTrue ( lib.isSupported (juce::File ("/x/a.mp3")),  ".mp3 supported");
    expectTrue (!lib.isSupported (juce::File ("/x/a.txt")),  ".txt rejected");
    expectTrue (!lib.isSupported (juce::File ("/x/a.png")),  ".png rejected");
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

} // namespace

int main()
{
    std::printf ("Convo headless tests\n====================\n");

    test_bake_reverse();
    test_bake_fadein();
    test_bake_decay_envelope();
    test_bake_decay_truncation();
    test_bake_taper_declick();
    test_bake_decayoff_length();
    test_engine_selection();
    test_process_silence_unloaded();
    test_tilt_response();
    test_ms_width();
    test_issupported();
    test_conv_nan_smoke();

    std::printf ("\n====================\n%d passed, %d failed\n", gPasses, gFails);
    return gFails == 0 ? 0 : 1;
}
