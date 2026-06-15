# Optimisation findings — processing side

Investigation only — **nothing here is implemented**. Each item lists the cost, a
recommended fix, the trade-off, and a rough impact/effort. UI-handling optimisations are
implemented separately on this branch (see the editor changes); this document is the DSP/
processing backlog.

Reference build to reason about: `Release` (`-O3` + JUCE recommended LTO). The audio thread
already runs under `juce::ScopedNoDenormals` and pre-allocates in `prepareToPlay`.

---

## 0. Ship/run Release, not Debug (process, not code)

The biggest single factor behind the reported long-IR stutter is that the **installed plugin
is a Debug build** (`CMAKE_BUILD_TYPE=Debug`, unoptimised, ~96 MB `.so`). An unoptimised JUCE
convolution of a multi-second IR cannot meet the real-time deadline. Verify any local install
is Release before chasing code-level wins.

- **Status: CONFIRMED** — a Release build (`-O3` + LTO, ~7.4 MB) eliminates the 9 s-IR stutter
  (verified by the user). The headless probe also showed the engine itself is correct (a 9 s
  delta IR goes live, passes through at exactly the reported latency, finite over noise), so
  there is no DSP bug here — it was purely build config. `CMakeLists.txt` now defaults a fresh
  configure to Release; `COPY_PLUGIN_AFTER_BUILD=TRUE` means rebuilding a Debug `build/` dir
  reinstalls the Debug plugin, so don't do that for real use.
- **Impact:** High. **Effort:** none (build config).

---

## 1. Long IRs: switch the long engine to non-uniform partitioning  ★ headline

`ConvolutionEngine` uses two uniform engines: `Convolution{Latency{0}}` (short) and
`Convolution{Latency{512}}` (long, IRs ≥ 1.5 s). Uniform partitioning of a long IR does FFT
work proportional to the **whole** IR length on **every** block — a 9 s IR (~432k taps) is
hundreds of partitions per 512-sample block. That is the real cost behind "9 s+ IRs are
stuttery", independent of build config.

- **Fix:** make the long engine `juce::dsp::Convolution::NonUniform { 512 }` (head block 512).
  Non-uniform partitioning keeps the 512-sample latency but grows partition sizes
  logarithmically down the tail, so cost scales with `log(IR length)` rather than linearly —
  the standard technique for long convolution reverbs.
- **Trade-off:** (a) the PRD currently lists `NonUniform` as out-of-scope — revisit that;
  (b) the warm-up size check (`expectedKernelSize` vs `getCurrentIRSize`, `ConvolutionEngine.cpp`)
  must be re-validated against how `NonUniform` reports its IR size; (c) slightly higher memory;
  (d) the headless transition tests need re-checking.
- **Impact:** High. **Effort:** Medium.

Alternative / complement: add a **third engine tier** for very long IRs with a larger latency
(bigger uniform partitions → less CPU, at the cost of latency). Cheaper to reason about than
`NonUniform` but worse latency; `NonUniform` is the better answer.

---

## 2. Gate the Wet-Comp RMS passes (and fuse meter passes)

`processBlock` runs, every block, separate O(n) passes:
- input peak via `getMagnitude` (meter),
- dry RMS via per-channel `getRMSLevel` (Wet Comp reference),
- wet RMS via per-channel `getRMSLevel` (Wet Comp),
- output peak via `getMagnitude` (meter).

- **Fix:** (a) compute the **dry/wet RMS only when Wet Comp is enabled** (`wetCompParam`);
  skip both passes otherwise. (b) Optionally fuse peak+sumSq into a single pass per buffer
  instead of calling `getMagnitude`/`getRMSLevel` separately.
- **Trade-off:** minor restructure; the fused-pass version is slightly less readable.
- **Impact:** Low–Medium. **Effort:** Low.

---

## 3. Skip neutral filter stages instead of always processing them

These run an O(n) pass (and rebuild coefficients) **every block even when neutral**:
- **Tone** tilt shelves when `tone ≈ 0` (flat),
- **Input HP** when `inHP ≈ 20 Hz` and **Input LP** when `inLP ≈ 20 kHz` (effectively flat),
- **Width** M/S loop when `width ≈ 100 %` (an identity reconstruction),
- **Pre-Delay** delay-line push/pop loop when `preDelay ≈ 0 ms` (a no-op delay).

- **Fix:** branch past each stage when its control sits at the neutral value.
- **Trade-off:** must **reset** the relevant filter / delay-line state when a stage
  re-engages, or the first re-engaged block clicks (the input filters already reset on the
  filter-target toggle; this would extend that pattern). Adds branches.
- **Impact:** Medium (each skipped stage removes a full-buffer pass). **Effort:** Low–Medium.

---

## 4. Per-sample mix loop — block-rate fast path when not smoothing

The final mix loop calls `getNextValue()` per sample for ~7 `SmoothedValue`s (dry, wet, IR
Gain, Wet Comp, output, clip-guard blend, duck/bypass/no-IR/fade), plus a per-sample duck
envelope and the soft clip.

- **Fix:** when a `SmoothedValue` reports `!isSmoothing()`, hoist its target out of the loop
  and apply it block-rate; only fall to the per-sample path while ramping. Optionally apply
  the static gains with `juce::FloatVectorOperations` (SIMD) and only soft-clip per sample.
- **Trade-off:** two code paths (smoothing vs steady) — more complexity for a modest,
  signal-dependent win; full vectorisation is hard because ducking + soft-clip are per-sample.
- **Impact:** Low–Medium. **Effort:** Medium.

---

## 5. Denormals in offline kernel filtering (`bake()`)

`bake()` runs on the message thread **without** `ScopedNoDenormals`. The Filter-IR offline
HP/LP pass (`ConvolutionEngine.cpp`) can leave denormal values in the kernel tail.

- **Fix:** wrap the offline filter loops in `juce::ScopedNoDenormals`.
- **Trade-off:** none meaningful (it is a one-time bake, so the audio-thread impact is nil —
  this is hygiene, not a hot-path win).
- **Impact:** Low. **Effort:** trivial.

---

## 6. Engine-selection threshold / latency, revisit after #1

`kThresholdSeconds = 1.5` and `kLongLatency = 512` are reasonable for uniform engines. If #1
lands, reconsider whether two tiers are still needed or whether a single `NonUniform` engine
(or a lower threshold) is simpler.

- **Impact:** Low (cleanup). **Effort:** Low. Depends on #1.

---

## Suggested order

1. Confirm Release fixes the stutter (#0) — likely resolves most of the complaint.
2. **#1 non-uniform partitioning** — the durable fix for long IRs.
3. Quick real-time savings: **#2** (gate RMS) and **#3** (skip neutral stages).
4. Hygiene: **#5**. Defer **#4**/**#6** unless profiling shows they matter.
