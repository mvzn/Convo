# Convo

## What This Is
A simple, MIDI-free convolution audio effect: drop an impulse response and hear your signal convolved with it. Open-source (MIT) sibling of the closed-source Convsyn synth, built by reusing Convsyn's convolution/IR core without the synth machinery.

## Coding Principles
- Build the simplest thing that satisfies the PRD. Prefer `juce::dsp` building blocks over custom DSP.
- One requirement at a time; don't add features not in `prd.md`.
- Make the editor deletable without affecting audio.

## Tech Stack
- **Framework:** JUCE 8.0.6 (via CMake `FetchContent`, pinned)
- **Language:** C++17
- **Build system:** CMake (≥ 3.22)
- **Plugin formats:** VST3 (all platforms), AU (macOS only)
- **JUCE modules:** juce_audio_utils, juce_dsp, juce_gui_extra

## Architecture Preferences
- Keep the processor/editor split clean; they communicate only through APVTS (+ a few atomic getters for meters and the baked-IR generation counter).
- Each DSP concern is a `juce::dsp` member the processor composes; `ConvolutionEngine` is the one custom class (owns the two convolution engines, adaptive selection, and IR baking).
- Define all parameters in one `createParameterLayout()`.
- Implement `getStateInformation`/`setStateInformation` (APVTS state + IR file path).

## DSP Constraints
- No allocations or locks in `processBlock()` — pre-allocate in `prepareToPlay()`.
- IR decode and baking (windowing + `loadImpulseResponse`) happen on the **message thread** only, via the timer hand-off; the audio thread never allocates.
- Engine selection and `setLatencySamples` happen on the message thread at file-load time only.
- Use `juce::SmoothedValue` for all audio-rate gain/param changes (dry, wet, IR Gain, output, tone, width, duck, bypass, wet comp, input HP/LP cutoffs).
- The pre-IR input filter (first-order HP/LP, 6 dB/oct) sits on the wet source before convolution; the dry tap is never filtered. Mid/Side is a bake param: `bake()` re-encodes the kernel to M/S, and the audio thread encodes/decodes M/S around the convolution only when the live kernel is M/S (published via the `msActive` atomic). Toggling Mid/Side re-bakes and is masked by the load fade.
- IR level: auto-level by default (kernel scaled in `bake()` to unity energy, one gain across channels), with the "Raw IR" parameter restoring the recorded level for calibrated IRs. JUCE-side normalization stays off (`Convolution::Normalise::no`) — all level policy lives in `bake()`.
- A defeatable soft-clip ceiling (`SoftClip.h`, transparent below −2.5 dBFS) guards the final output.
- Adaptive **Wet Comp** (default on, defeatable): a per-block RMS of dry-input vs wet, applied as a smoothed (~0.25 s) gain on the wet, clamped to ±18 dB and frozen while the input is quiet so tails ring out. Audio-thread, allocation-free.

## Code Style
- JUCE conventions: camelCase members/methods, PascalCase classes.
- Explicit `juce::` prefix; no `using namespace juce;` in headers.
- RAII; `std::unique_ptr`/JUCE smart pointers over raw pointers.

## What NOT To Do
- Don't reintroduce MIDI, ADSR, or IR transposition — those belong to Convsyn.
- Don't put DSP logic in the editor.
- Don't add third-party dependencies without asking.
- Keep level policy explicit and defeatable. The automatic level stages are auto-level (in `bake()`) and the clip guard (`SoftClip.h`); the adaptive Wet Comp is the only signal-dependent gain on the audio thread. All three are user-defeatable ("Raw IR" / "Clip Guard" / "Wet Comp"). Don't add new automatic level policy beyond these — the explicit user gains (Dry/Wet/IR Gain/Output) aside.
- Don't let bake knobs trigger engine/latency changes — only a new file does.
