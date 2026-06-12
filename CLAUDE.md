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
- Use `juce::SmoothedValue` for all audio-rate gain/param changes (dry, wet, output, tone, width, duck, bypass).
- No IR normalization (`Convolution::Normalise::no`) — preserve recorded IR level.

## Code Style
- JUCE conventions: camelCase members/methods, PascalCase classes.
- Explicit `juce::` prefix; no `using namespace juce;` in headers.
- RAII; `std::unique_ptr`/JUCE smart pointers over raw pointers.

## What NOT To Do
- Don't reintroduce MIDI, ADSR, or IR transposition — those belong to Convsyn.
- Don't put DSP logic in the editor.
- Don't add third-party dependencies without asking.
- Don't normalize the IR or auto-gain — the design is deliberately "honest level."
- Don't let bake knobs trigger engine/latency changes — only a new file does.
