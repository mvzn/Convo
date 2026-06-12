# Convo — Code Audit & Fix Plan

Date: 2026-06-12 · Scope: `Source/*`, `CMakeLists.txt`, `tests/*`, cross-checked against the
pinned JUCE 8.0.6 sources in `build/_deps/juce-src`.

Severity legend: 🔴 high (audible / crash / data-race in a DAW) · 🟡 medium · ⚪ low.

Items marked **⚠ DECISION** change the architecture or how the plugin sounds/behaves —
each has an explicit question with a recommendation and considerations. Everything else
is a safe, behavior-preserving fix that can be applied directly.

---

## 🔴 1. Reported latency is wrong when the host buffer exceeds 512 samples

**Where:** `Source/ConvolutionEngine.cpp:110`, `Source/PluginProcessor.cpp:133,188,379`

**Problem:** The long-IR engine is assumed to have a fixed latency of `kLongLatency`
(512). In JUCE 8.0.6, a non-zero-latency `dsp::Convolution`'s real head latency is
`nextPowerOfTwo(jmax(maximumBlockSize, 512))` (`MultichannelEngine` ctor,
`juce_Convolution.cpp:459`). At a host buffer of 1024/2048 (common; offline bounces
especially) the wet path is delayed 1024/2048 samples while Convo reports — and
dry-delays — only 512. Consequences: dry/wet comb filtering in the mix and wrong host
PDC. The dry delay line is capped at `kLongLatency + 1`, so it cannot compensate even
if it knew the real value.

**Best fix (no sound change at buffers ≤ 512; *corrects* sound above):**
1. Stop hard-coding 512. After `prepare()` and after each `loadIR()`, query the active
   engine's real latency via `juce::dsp::Convolution::getLatency()`.
2. Because the true value depends on the block size, re-derive and re-publish it in
   `prepareToPlay()` (call `setLatencySamples` + update `dryDelaySamples` there too —
   message thread, allowed).
3. Size `dryDelayLine` from `nextPowerOfTwo(jmax(samplesPerBlock, kLongLatency)) + 1`
   in `prepareToPlay()` instead of the constant.
4. Keep `kLongLatency` only as the *requested* latency passed to the engine ctor.

**Test to add:** prepare with `maximumBlockSize = 2048`, load a >1.5 s IR, feed a delta,
assert the wet impulse lands exactly `getLatencySamples()` after the dry one.

---

## 🔴 2. Un-convolved audio leaks through the wet path during IR loads

**Where:** `Source/ConvolutionEngine.cpp:104–129`, `Source/PluginProcessor.cpp:182–186`

**Problem:** `loadImpulseResponse` is asynchronous — JUCE builds the kernel on a
background thread and crossfades it in later. But `loadIR()` flips `active` to the new
engine and sets `loaded = true` *synchronously*. JUCE's kernel before any install is a
**unit impulse** (`makeImpulseBuffer()` → single 1.0 sample = pass-through). So on
first load, or when switching short↔long engines, raw input plays through the wet bus
at full wet gain (up to +6 dB) until the background build lands — far longer than the
15 ms `loadFade` for big IRs. The `loaded` guard only covers the never-loaded case.

**⚠ DECISION — the fix changes load-time behavior (what you hear while an IR bakes in):**

> **Q: What should the wet bus output between "file dropped" and "kernel actually live"?**
>
> **Option A (recommended): keep playing the *old* kernel until the new one is confirmed.**
> Flip `active` only after the target engine reports the new kernel
> (`Convolution::getCurrentIRSize()` matches the baked length, polled from the existing
> 30 Hz timer). JUCE then crossfades old→new internally.
> - *Pro:* seamless, no dropout, closest to "pro" plugin behavior.
> - *Con:* small architecture change — `active` flip becomes deferred/stateful
>   (a "pending engine" field + timer poll); first-ever load still needs Option B's
>   mute as there is no old kernel.
>
> **Option B (simplest): mute the wet bus until the install is confirmed.**
> Keep `loaded = false` (or a new `wetMuted` atomic) until the poll confirms, and let
> the existing `loadFade` ramp the wet back in.
> - *Pro:* tiny change, never lets the dirac pass-through escape.
> - *Con:* audible wet dropout on every re-bake/knob change (Decay/Fade-In/Taper/
>   Reverse would "gap" the reverb tail) — likely unacceptable for the bake knobs,
>   acceptable for file loads.
>
> **Recommendation:** A for engine switches and re-bakes, B's mute only for the very
> first load into a virgin engine. Considerations: Option A means latency reporting
> (issue 1) must also wait for the confirmed engine; both options need the poll to
> handle a *failed/empty* bake (keep old state, don't flip).

---

## 🔴 3. `setStateInformation` can run off the message thread and races the timer

**Where:** `Source/PluginProcessor.cpp:27,352–365,396–413`

**Problem:** Several VST3 hosts (Cubase/Nuendo project load, plugin scanners) call
`setState` from a loader thread. `setStateInformation` → `loadIRFile()` does file IO,
mutates `irLibrary`'s buffer, writes `apvts.state` (ValueTree — not thread-safe) and
calls `setLatencySamples` — while the 30 Hz timer (started in the *constructor*) may
concurrently read `irLibrary.getIR()` and rebake. Data race on the IR/baked buffers,
and a violation of the project's own "IR decode/bake on the message thread only" rule.

**Best fix (no sound change):** never load from `setStateInformation`. Stash the path:

```cpp
// setStateInformation (any thread):
pendingIRPath = path;            // juce::String guarded by a std::mutex, or a
pendingIRLoad.store (true);      //   CriticalSection-protected member

// timerCallback (message thread), before the rebake check:
if (pendingIRLoad.exchange (false))
{
    const juce::File f (takePendingPath());
    if (f.existsAsFile())
        loadIRFile (f);
}
```

`apvts.replaceState` is the documented-safe part and stays where it is. This also fixes
the related ordering hazard of `loadIRFile` running before `prepareToPlay` with a stale
spec (JUCE's factory re-installs kernels on `prepare`, but latency publication then
happens at the right time via fix 1).

---

## 🔴 4. Heap allocations in `processBlock` (violates the project's own rule)

**Where:** `Source/PluginProcessor.cpp:236–239`

**Problem:** `IIR::Coefficients<float>::makeLowShelf/makeHighShelf` each `new` a
ref-counted object every block (then copy-assign and free) — ~4 alloc/free pairs per
block on the audio thread. CLAUDE.md forbids exactly this.

**Best fix (bit-identical sound):** use the allocation-free array variants:

```cpp
const auto lo = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (
    currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain (-tiltDb));
const auto hi = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
    currentSampleRate, 700.0f, 0.5f, juce::Decibels::decibelsToGain ( tiltDb));
lowShelf.state->coefficients  = juce::Array<float> (lo.data(), (int) lo.size());
highShelf.state->coefficients = juce::Array<float> (hi.data(), (int) hi.size());
```

(`juce::Array` assignment reuses existing storage once capacity is established; if you
want it provably alloc-free, copy element-wise into the existing array since the size
never changes after `prepareToPlay`.)

---

## 🟡 5. No IR size cap + int64→int truncation when decoding

**Where:** `Source/IRLibrary.cpp:19–22`

**Problem:** `(int) reader->lengthInSamples` truncates a 64-bit length (a pathological
file wraps to garbage/negative, fed straight into `AudioBuffer(numChannels, n)`), and
there is no upper bound at all — an hour-long multichannel WAV allocates gigabytes and
then gets FFT'd. Memory blow-out / freeze.

**⚠ DECISION — a cap rejects (or truncates) files that currently "work":**

> **Q: Cap the IR length — at what value, and reject or truncate?**
>
> **Recommendation: truncate to 30 s (at the IR's sample rate) and surface
> "IR truncated to 30 s" in the editor label.** 30 s covers every practical reverb /
> cab / creative IR; truncation keeps a dropped *song* usable as a sound-design IR
> (in the spirit of "drop anything and hear it") instead of erroring.
> - *Considerations:* truncating changes the sound of >30 s sources vs. today (today
>   they load fully — when they don't kill the host). Rejecting is purer but less fun.
>   Whichever is chosen, also clamp channel count (e.g. first 2 channels) since the
>   convolution only ever uses two — decoding 16-ch files wastes memory and the
>   thumbnail misleadingly displays channels that aren't heard.

**Unconditional part (no decision needed):** read `lengthInSamples` into an `int64`,
validate `> 0` and `< cap` *before* the `(int)` cast; on failure return `false` so the
editor shows "Failed to load".

---

## 🟡 6. The plugin silences the track by default

**Where:** `Source/PluginProcessor.cpp:43` (Dry default −60 dB),
`Source/ConvolutionEngine.cpp:133–136` (wet cleared when no IR)

**Problem:** Factory defaults = Dry −60 dB + wet cleared with no IR ⇒ inserting Convo
mutes the channel until an IR is dropped. Dangerous default for an insert effect.

**⚠ DECISION — any fix changes the default sound and/or the mix model:**

> **Q: What should a freshly inserted Convo (no IR) output?**
>
> **Option A (recommended): pass dry at unity while no IR is loaded.** Implement as a
> "no-IR" state that overrides the mix: `dEff = 1, wEff = 0` (smoothed via the existing
> `bypassSm`-style crossfade) whenever `!convolution.hasIR()`.
> - *Pro:* insert-safe; the moment an IR lands, the user's actual Dry/Wet settings take
>   over. No parameter defaults change, so existing sessions are untouched.
> - *Con:* Dry knob appears to "do nothing" until an IR is loaded (mitigate: editor
>   already shows "No IR loaded").
>
> **Option B: change the Dry default from −60 dB to 0 dB.**
> - *Pro:* trivial one-liner.
> - *Con:* changes the out-of-box sound *with* an IR too — default becomes
>   dry+wet (100/100) instead of fully wet, which suits send-style use less and
>   contradicts the current "wet-focused" default. Also alters any session that was
>   saved at default values? (No — APVTS stores explicit values; only *new* instances
>   change.)
>
> **Option C: leave as-is, document "Convo is silent until an IR is loaded" in README.**
> - Defensible for a deliberately minimal plugin, but most hosts' users will read
>   silence as "plugin broken".
>
> **Recommendation:** A. Consideration: combine with issue 2's wet-mute logic — both
> want a well-defined "engine not ready" state machine (`NoIR → Loading → Live`), which
> is the one architectural addition worth making in this round.

---

## 🟡 7. Possible allocation in `processBlock` via the work buffers

**Where:** `Source/PluginProcessor.cpp:158–159,197,223`

**Problem:** `prepareToPlay` sizes `inWork`/`wetWork` with the raw `samplesPerBlock`
while the `ProcessSpec` is clamped with `jmax(1, …)` — inconsistent. If a host
preflights with `samplesPerBlock == 0`, or ever delivers a block larger than prepared,
the `setSize(..., avoidReallocating=true)` calls allocate on the audio thread.

**Best fix (no sound change):**
- In `prepareToPlay`: `const int maxSamples = juce::jmax (1, samplesPerBlock);` and use
  it for both buffers (consistent with the spec).
- In `processBlock`: clamp instead of growing —
  `const int n = juce::jmin (numSamples, inWork.getNumSamples());` and process `n`
  (oversized blocks are a host-contract violation; degrading gracefully beats
  allocating). Keep the `setSize(..., true)` calls only for *shrinking* views, or drop
  them entirely and pass explicit sample counts.

---

## 🟡 8. Knob drags re-bake the whole IR at 30 Hz (+ per-sample `pow`)

**Where:** `Source/PluginProcessor.cpp:352–365`, `Source/ConvolutionEngine.cpp:56–65`

**Problem:** Every parameter delta re-windows the entire IR and queues a full engine
rebuild — up to 30×/s while dragging Decay/Fade-In/Taper. The decay loop calls
`std::pow` per sample; for a long stereo IR that's millions of `pow` calls per tick on
the message thread. UI stutter / message-thread CPU blow-up. (JUCE's queue does
coalesce pending loads, so the background thread survives — the message thread is the
bottleneck.)

**Best fix (no sound change, slight UX change in update cadence):**
1. **Debounce:** in `timerCallback`, rebake only when `cur != lastBaked` **and**
   `cur == paramsSeenLastTick` (i.e. the value has been stable for one 33 ms tick).
   Dragging then re-bakes ~at rest points instead of continuously.
2. **Incremental decay multiply:** replace `std::pow(10, -3t/decaySamps)` with
   `g *= step` where `step = std::pow(10.0, -3.0 / decaySamps)` computed once —
   mathematically identical, ~100× cheaper. Keep the `-60 dB` truncation check on `g`.

If knob-drag responsiveness still matters later, the next step is baking on a worker
thread — **not recommended now**; it adds the threading complexity this project
deliberately avoids, and the debounce is sufficient.

---

## 🟡 9. Store-ordering race: unmasked click on IR load

**Where:** `Source/PluginProcessor.cpp:378–380` vs `:182–194`

**Problem:** `loadIRFile` stores `dryDelaySamples` *before* `loadFadePending`; the
audio thread checks the fade flag first and the delay second. Landing between the two
stores, it adopts the new dry-delay length (a discontinuity in the dry path) with the
masking fade still unarmed → one-block click.

**Best fix (no sound change):** reverse the store order so the flag is armed first —

```cpp
loadFadePending.store (true);          // arm the mask first
dryDelaySamples.store (lat);           // then publish the new delay
```

…and in `processBlock`, read `loadFadePending` *before* `dryDelaySamples` (already the
case). With release/acquire semantics on the two atomics (default `seq_cst` is fine),
the audio thread can no longer see the new delay without the fade armed. (Becomes moot
if issue 2's Option A defers all of this to a confirmed-install handshake — fold it in
there.)

---

## 🟡 10. `getTailLengthSeconds()` hard-coded to 10.0

**Where:** `Source/PluginProcessor.h:38`

**Problem:** The true tail is `bakedIR length + preDelay (≤ 0.5 s)` and, with no IR
cap today, can far exceed 10 s — hosts may truncate the reverb tail when rendering.

**Best fix (no sound change in normal playback; fixes renders):** publish an atomic
`tailSeconds` updated on the message thread at load/re-bake time:

```cpp
tailSeconds.store (bakedLengthSeconds + 0.5 /* max preDelay */);
// getTailLengthSeconds() returns juce::jmax (1.0, tailSeconds.load());
```

Pairs naturally with the cap from issue 5 (bounded above by cap + 0.5 s).

---

## ⚪ 11. Taper never actually reaches zero

**Where:** `Source/ConvolutionEngine.cpp:74`

`x = k / taperSamps` never reaches 1.0, so the final kernel sample is small but
non-zero. Fix: `x = (k + 1) / taperSamps`. *Technically* a sound change but below
audibility (last-sample residual ≈ −60 dB of an already-tapered tail); the tests'
`last sample ~= 0` tolerance already expects this intent. Safe to just do.

---

## ⚪ 12. Editor failure state is inconsistent

**Where:** `Source/PluginEditor.cpp:277–280`

On a failed load the label says "Failed to load: …" while the previous IR keeps
playing and the thumbnail still shows it. Fix: keep the error visible but append the
still-active IR, e.g. `"Failed: bad.wav — still using old.wav"`, and leave the
thumbnail untouched (it is truthful — that IR *is* what's playing).

---

## ⚪ 13. Format-support inconsistencies

**Where:** `Source/PluginEditor.cpp:140,287`, `Source/IRLibrary.cpp:37–42`

- Drop-zone hint omits `.flac`; the chooser filter and `isSupported` include it.
  Fix the string.
- `isSupported` claims `.mp3` even on builds where `JUCE_USE_MP3AUDIOFORMAT` is off
  (e.g. the headless test build). Fix: wrap the `.mp3` clause in
  `#if JUCE_USE_MP3AUDIOFORMAT`, or better, derive acceptance from
  `formatManager.findFormatForFileExtension()` so the answer can never drift from the
  registered decoders.

---

## ⚪ 14. Meters: raw block peaks, no ballistics

**Where:** `Source/PluginProcessor.cpp:217–220,334–337`, `Source/PluginEditor.cpp:252`

Per-block peak polled at 30 Hz flickers and misses peaks between polls. Fix (visual
only): keep a peak-hold in the processor —
`level = jmax (blockPeak, level * 0.85f)` per block — so the UI poll reads a decaying
peak. No audio path impact.

---

## ⚪ 15. Redundant `irPath` writes

**Where:** `Source/PluginProcessor.cpp:383,389–390`

The property is set in both `loadIRFile` and `getStateInformation`. Harmless; keep the
`getStateInformation` one (it re-validates `existsAsFile()`) and drop the other —
or vice versa — for a single source of truth.

---

## Verified non-issues (checked, fine as-is)

- Multichannel WAV decode: JUCE's `AudioFormatReader::read` zero-fills destination
  channels beyond the file's — no uninitialized data (suspicion checked in JUCE source).
- IR survives `prepareToPlay`: the convolution factory re-installs the kernel
  synchronously inside `prepare()`.
- `FileChooser` lambda lifetime: owned by a member `unique_ptr`; destruction cancels
  the callback.
- Bake windowing edge cases (zero-length fade/taper, fade ≥ IR length, decay+fade
  overlap) — all clamped correctly.
- Bypass crossfade math (`dEff/wEff/oEff`) reaches exact unity/mute at the rails, and
  the dry path stays latency-aligned under bypass.

---

## Fix log (2026-06-12)

All 15 issues fixed. Decisions taken: **issue 2** — keep the old kernel audible and
warm the target engine on a scratch buffer until its kernel is confirmed live, then
crossfade (`ConvolutionEngine` transition state machine); **issue 5** — truncate IRs
at 30 s (`IRLibrary::kMaxSeconds`) and clamp to 2 channels, with "(truncated to 30 s)"
shown in the editor; **issue 6** — no-IR state behaves like bypass (dry at unity,
`noIrSm` in the mix loop).

Notable implementation points:
- Issue 1: `ConvolutionEngine::longEngineLatencyForBlockSize()` mirrors JUCE 8.0.6's
  internal rule; a unit-test canary compares it against the live engine's
  `getLatency()` so a JUCE upgrade that changes the rule fails the suite.
- Issue 2: kernel install is detected on the audio thread (`getCurrentIRSize()` is
  polled from `process()`, the same thread that installs engines — race-free), then
  an 80 ms settle covers JUCE's internal crossfade before a 30 ms output crossfade.
- Issue 3: `setStateInformation` only stashes the IR path; `timerCallback` loads it.
- Issue 13 follow-up: the explicit `MP3AudioFormat` registration in `IRLibrary` was a
  duplicate (`registerBasicFormats()` already adds it under `JUCE_USE_MP3AUDIOFORMAT`)
  and asserted in debug builds — removed.
- Tests: 4 new test groups (latency-vs-blocksize + canary, transition leak,
  IRLibrary decode cap/channel clamp, conditional `.mp3` support); 61 pass.

---

## Suggested order of work

1. **Fixes with no behavior decisions** (apply directly):
   4 (alloc-free shelves) → 9 (store order) → 3 (deferred setState load) →
   1 (real latency) → 7 (work buffers) → 8 (debounce + incremental decay) →
   10 (tail length) → 11–15.
2. **Decisions needed first** (answer the ⚠ questions above): 2 (load window
   behavior), 5 (IR cap value & policy), 6 (no-IR default output).
   Note 2 + 6 share the proposed `NoIR → Loading → Live` state — decide them together.
3. **Tests to add with the fixes:** latency-at-2048-block delta test (issue 1),
   load-window leak test (issue 2, needs a poll-until-live helper), oversized-file
   rejection (issue 5), incremental-decay equivalence vs `pow` (issue 8).
