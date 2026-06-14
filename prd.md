# PRD: Convo

## Goal
- A simple, MIDI-free **convolution audio effect** (insert): drop an impulse response, hear your signal convolved with it.
- Derived by reusing the DSP core of the closed-source *Convsyn* plugin, stripped of all synth machinery (MIDI, ADSR, transposition).
- Shipped as a standalone open-source (MIT) product; coexists with Convsyn but lives in its own public repo with clean history.

## Features
Real-time, automatable controls (act on the live signal):
- **Dry** — dry level. −60…+6 dB, default 0 dB (−60 = silence).
- **Wet** — convolved level. −60…+6 dB, default 0 dB.
- **Output** — master trim. −60…+12 dB, default 0 dB.
- **Tone** — single-knob tilt EQ on the wet, pivot ~700 Hz. −100…+100 %, default 0 (flat); left = darker, right = brighter.
- **Pre-Delay** — wet-only delay line, post-convolution. 0…500 ms, default 0 ms. Not reported as plugin latency.
- **Width** — M/S stereo width on the wet. 0…200 %, default 100 %.
- **Duck** — amount the wet is ducked under the live dry envelope. 0…100 %, default 0 (off).
- **Duck Release** — ducking recovery time. 20…1000 ms, default 200 ms.
- **Bypass** — click-free; wired to the host bypass (`getBypassParameter`).

IR-bake controls (recompute the windowed IR on the message thread; not automation-grade):
- **Fade-In** — raised-cosine onset ramp on the IR. 0…1000 ms, default 0 ms.
- **Decay** — exponential decay imposed on the IR tail; truncates at −60 dB. 50 ms…10 s, top = **Off** (use IR as recorded), default Off.
- **Tail-Taper** — raised-cosine ramp to zero over the last N ms before the truncation point (de-click). 0…500 ms, default 10 ms.
- **Reverse** — reverse the IR before windowing (reverse-reverb). On/off, default off.

## Behaviour
- **IR loading:** drag-drop or file chooser; WAV/AIFF/FLAC/OGG (MP3 deferred — see Roadmap). No normalization — raw IR gain preserved (`Convolution::Normalise::no`); the Output knob and the OUT meter handle level/clip.
- **Adaptive convolution engine:** two persistent `juce::dsp::Convolution` engines — `Latency{0}` and `Latency{512}`. The engine is chosen at **file load** by raw IR length (< 1.5 s → zero-latency; ≥ 1.5 s → 512-sample latency), and stays fixed until the next file load. Bake knobs never change the engine or latency.
- **Variable reported latency:** report 0 (short engine) or 512 (long engine) via `setLatencySamples`; delay the dry tap by the same amount so dry/wet stay aligned inside the plugin.
- **IR bake pipeline (message thread, on file / reverse / fade-in / decay / taper change):** `raw → reverse? → fade-in → decay (×exp, truncate @ −60 dB) → tail-taper → loadImpulseResponse`. Audio thread stays a pure convolution.
- **Wet signal chain (audio thread):** `convolution → tone (tilt) → width (M/S) → pre-delay → ducking gain × Wet`, summed with `dry × Dry`, then × Output.
- **Click masking:** a short (~15 ms) output fade on every IR load masks the kernel swap, engine switch, and dry-delay jump.
- **Metering:** IN and OUT level meters (OUT flags clipping, since there is no normalization).
- **GUI:** the IR display shows the **processed (baked)** IR — fade-in/decay/taper/reverse are visible in the waveform, redrawn on each bake.
- **State:** APVTS save/restore plus the IR file path; reloads the IR (and re-bakes) on session recall.
- **Buses:** mono or stereo; input channel set must equal output. No allocations or locks on the audio thread.

## Roadmap (post-first-demo)
- **MP3 → WAV import converter:** the demo build ships without an MP3 decoder (`JUCE_USE_MP3AUDIOFORMAT` off, licensing), so MP3 is not advertised in the drop zone or file chooser. Next version: accept a dropped/chosen MP3, decode-and-convert it to WAV on load (or pull in a decoder), then feed the converted buffer to the existing IR pipeline.

## Out of Scope (for now)
- MIDI of any kind, ADSR gating, IR transposition/pitch (those stay in Convsyn).
- Multisample / per-note IR sets; IR library browser; built-in IR pack.
- True dual-engine crossfade (v1 masks engine switches with an output fade, not a parallel run).
- `NonUniform` partitioning, oversampling, multiband, modulation/LFO, tempo-sync pre-delay.
- Preset system, A/B compare, undo/redo, custom skinned UI.
