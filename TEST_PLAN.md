# Test plan

_Project type: audio DSP plugin. Runner: `tests/` (headless C++, hand-rolled). Run: `make -C tests && ./tests/runtests`._

## Coverage today
- **Unit:** `tests/runtests.cpp` — 12 tests / 43 assertions, all passing (Phase 1).
- **Integration:** pluginval 1.0.4, strictness 5 (`--skip-gui-tests`) — SUCCESS. GUI tests not run (no xvfb).

## Recent activity
- Source/ConvolutionEngine.* — bake() exposed static (test seam)
- Source/PluginProcessor.cpp — wet chain (tone/width/duck), not yet unit-covered (see Phase 2)

## Pure math / identity
- [x] `bake()` reverse mirrors samples exactly — `runtests.cpp::test_bake_reverse` _(source: ConvolutionEngine.cpp:32)_
- [x] `IRLibrary::isSupported` accepts wav/WAV/aiff/flac/mp3, rejects txt/png — `::test_issupported` _(source: IRLibrary.cpp:37)_

## Time-domain DSP (analytic prediction)
- [x] `bake()` fade-in raised cosine: g(0)=0, g(N/2)=0.5, g(N)=1 — `::test_bake_fadein` _(source: ConvolutionEngine.cpp:41)_
- [x] `bake()` decay envelope: −30 dB at decaySamps/2, 0 dB at start — `::test_bake_decay_envelope` _(source: :59)_
- [x] `bake()` decay truncation ≈ −60 dB point (±2 samp), shortens IR — `::test_bake_decay_truncation` _(source: :67,81)_
- [x] `bake()` tail-taper de-click: last sample ≈ 0, pre-taper untouched — `::test_bake_taper_declick` _(source: :71)_
- [x] `bake()` decayOff keeps full length — `::test_bake_decayoff_length` _(source: :51)_
- [ ] Ducking release time-constant: env → 1/e after release_ms — _(source: PluginProcessor.cpp:310; **Phase 2**: needs wet-chain extraction or full-processor harness)_

## Filter response (sine-correlation)
- [x] Tilt @ tone=0 flat (±0.3 dB @ 100/700/5k); @ +12 dB pivot ≈0, LF <−8, HF >+8, sign-symmetric — `::test_tilt_response` _(source: PluginProcessor.cpp:235; tests the recipe in isolation)_
- [ ] Tilt verified **on the wet bus** through the processor — _(Phase 2: full-processor harness)_

## Static fixtures (input → output)
- [x] M/S width: w=1 identity, w=0 mono-collapse, w=2 doubles side — `::test_ms_width` _(source: PluginProcessor.cpp:256; recipe in isolation)_
- [ ] Width applied **on the wet bus** through the processor — _(Phase 2)_

## State machines / engine selection
- [x] `loadIR` selects engine by length, boundary at 1.5 s, latency 0/512 — `::test_engine_selection` _(source: ConvolutionEngine.cpp:109-110)_
- [x] `process()` unloaded → silence — `::test_process_silence_unloaded` _(source: :135)_

## Latency
- [x] `getLatencySamples()` 0 (short) / 512 (long) matches selection — `::test_engine_selection` _(source: :110)_
- [ ] [integration] delta IR → output == input delayed by reported latency (warm-up for async load) — _(Phase 2: async)_

## CPU / stability
- [x] Convolution NaN/finite + bounded smoke, 2 s noise, delta IR — `::test_conv_nan_smoke` _(source: ConvolutionEngine.cpp:131)_
- [ ] **Full-chain** NaN smoke through `processBlock` (tone/width/duck/bypass/smoothers/delay lines) per param extreme — _(Phase 2: needs juce_audio_processors + GUI link, or wet-chain extraction)_
- [ ] Run-to-run determinism of `processBlock` (catches uninit duckEnv/delay/smoothers) — _(Phase 2)_
- [ ] Allocation audit: 0 allocs in steady-state `processBlock` — _(Phase 2)_

## Property-based / fuzzing
- [ ] Random param sweeps × white noise: |out| ≤ 4 ∧ finite, always — _(Phase 2)_

## Skipped
- `Source/PluginEditor.cpp`, `Source/ConvoLookAndFeel.cpp` — GUI rendering; no unit tests (annotate with `// @no-test GUI rendering` or keep here)
- `IRLibrary::loadFile`, `getDisplayName` — file IO / trivial; needs a committed WAV fixture (low priority)

## Phase 2 blocker — note
The wet-chain DSP (tilt/width/duck/pre-delay) is written inline inside `processBlock`, so it can only be exercised end-to-end with a live (async) convolution and the heavy `juce_audio_processors`+GUI link. The clean unlock is to extract a `WetChain` core (header-only, like the rest of the DSP) that the processor composes — then it unit-tests headless exactly like `bake()`. Recommended before investing in Phase 2.

## CI suggestion (recommend only — do not edit .github/ from this skill)
Add a `Unit tests` job to `.github/workflows/build.yml`:
```yaml
- name: Unit tests
  run: make -C tests && ./tests/runtests
```
