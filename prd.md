# PRD: Convo

## Goal
- A simple, MIDI-free **convolution audio effect** (insert): drop an impulse response, hear your signal convolved with it.
- Derived by reusing the DSP core of the closed-source *Convsyn* plugin, stripped of all synth machinery (MIDI, ADSR, transposition).
- Shipped as a standalone open-source (MIT) product; coexists with Convsyn but lives in its own public repo with clean history.

## Features
Real-time, automatable controls (act on the live signal):
- **Dry** — dry level. −60…+6 dB, default −12 dB (−60 = silence).
- **Wet** — convolved (wet) level. −60…+6 dB, default −12 dB.
- **IR Gain** — gain of the IR, in series with Wet (a trim on the convolved signal; equivalent by linearity to scaling the kernel, applied real-time at the output). −60…+6 dB, default 0 dB.
- **Output** — master trim. −60…+12 dB, default 0 dB.
- **Tone** — single-knob tilt EQ on the wet, pivot ~700 Hz. −100…+100 %, default 0 (flat); left = darker, right = brighter.
- **Input HP** — first-order high-pass (low cut) on the IR input, pre-convolution. 20…2000 Hz, default 20 Hz (flat). 6 dB/oct.
- **Input LP** — first-order low-pass (high cut) on the IR input, pre-convolution. 200…20000 Hz, default 20000 Hz (flat). 6 dB/oct.
- **Pre-Delay** — wet-only delay line, post-convolution. 0…500 ms, default 0 ms. Not reported as plugin latency.
- **Width** — M/S stereo width on the wet. 0…200 %, default 100 %.
- **Duck** — amount the wet is ducked under the live dry envelope. 0…100 %, default 0 (off).
- **Duck Release** — ducking recovery time. 20…1000 ms, default 200 ms.
- **Bypass** — click-free; wired to the host bypass (`getBypassParameter`).

IR-bake controls (recompute the windowed IR on the message thread; not automation-grade):
- **Fade-In** — raised-cosine onset ramp on the IR. 0…10000 ms, default 0 ms; the ramp length is capped at 80% of the IR so the longest fade still leaves a fifth of the IR at full level.
- **Decay** — exponential decay imposed on the IR tail; truncates at −60 dB. 50 ms…10 s, top = **Off** (use IR as recorded), default Off.
- **Tail-Taper** — raised-cosine ramp to zero over the last N ms before the truncation point (de-click). 0…500 ms, default 10 ms.
- **Reverse** — reverse the IR before windowing (reverse-reverb). On/off, default off.

Level, routing & safety controls:
- **Raw IR Level** — bake-time. Off (default) = auto-level (kernel scaled to unit energy); on = the IR's recorded level unscaled, for calibrated IRs (can convolve very hot on dense full-scale material). On/off, default off.
- **Mid/Side** — bake-time routing. Convolve mid-with-mid and side-with-side: re-bakes the kernel to M/S (ch0 = mid, ch1 = side) and M/S-encodes the input around the convolution. Wants a stereo IR — a mono IR has no side, so it collapses to mono. On/off, default off.
- **Bass Mono** — Mid/Side only: high-pass the side at this crossover so content below it collapses to mono (lows stay in the mid). 20…500 Hz, default 20 Hz (off). 6 dB/oct.
- **Filter IR** — pre-IR filter target. Off (default) = filter the input at runtime (automatable); on = bake the In HP/In LP into the kernel (after auto-level, so the wet result is identical by linearity), shown in the IR display and cheaper at runtime. On/off, default off.
- **Wet Comp** — adaptive wet gain compensation (real-time): tracks the dry-input loudness and trims the wet to match, held while the input is quiet so reverb tails ring out. On/off, default on.
- **Clip Guard** — transparent soft-clip ceiling on the output (does nothing below ~−2.5 dBFS). On/off, default on.

## Behaviour
- **IR loading:** drag-drop or file chooser; WAV/AIFF/FLAC/OGG (MP3 deferred — see Roadmap).
- **Level policy:** all level shaping lives in `bake()` + `SoftClip.h`. JUCE-side normalization stays off (`Convolution::Normalise::no`); instead the kernel is **auto-levelled** to unit energy at bake time (defeated by **Raw IR Level**). On the audio thread, **Wet Comp** adaptively matches the wet to the dry loudness and the **Clip Guard** soft-clips the output — both defeatable.
- **Adaptive convolution engine:** two persistent `juce::dsp::Convolution` engines — `Latency{0}` and `Latency{512}`. The engine is chosen at **file load** by raw IR length (< 1.5 s → zero-latency; ≥ 1.5 s → 512-sample latency), and stays fixed until the next file load. Bake knobs never change the engine or latency.
- **Variable reported latency:** report 0 (short engine) or 512 (long engine) via `setLatencySamples`; delay the dry tap by the same amount so dry/wet stay aligned inside the plugin.
- **IR bake pipeline (message thread, on file / reverse / fade-in / decay / taper / Raw-IR / Filter-IR / Mid-Side change):** `raw → reverse? → fade-in → decay (×exp, truncate @ −60 dB) → tail-taper → auto-level? → pre-IR filter? (if targeting the IR) → M/S encode? → loadImpulseResponse`. Audio thread stays a pure convolution. The Mid-Side and Filter-IR toggles change audio routing, so they re-bake **and** arm the load fade to mask the swap.
- **Wet signal chain (audio thread):** `input HP/LP (when filtering input) → M/S encode? (+ side high-pass for Bass Mono) → convolution → M/S decode? → tone (tilt) → width (M/S) → pre-delay`, then mixed as `wet × IR Gain × Wet Comp × Wet × duck`, summed with `dry × Dry`, then × Output, then the (gain-blended) Clip Guard soft-clip. The input HP/LP filters the wet source only — the dry tap is never filtered.
- **Click masking:** a short (~15 ms) output fade on every IR load and on routing toggles (Mid-Side, Filter-IR) masks the kernel swap, engine switch, and dry-delay jump. Filters reset on re-engage and the Clip Guard is gain-blended, so toggles stay click-free.
- **Metering:** IN and OUT level meters (OUT flags clipping at the ceiling).
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
