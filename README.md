# Convo

A simple **convolution audio effect**. Drop an impulse response, hear your
signal convolved with it(AGPLv3).

## Controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Dry | −60…+6 dB | 0 dB | Dry (unprocessed) level |
| Wet | −60…+6 dB | −60 dB | Convolved (wet) level |
| Mix | 0…100 % | 0 % | Equal-power dry/wet balance while Link/Mix is on |
| Link/Mix | on/off | off | Merges Dry and Wet into the single Mix knob; unlinking restores their values |
| IR Gain | −60…+6 dB | 0 dB | Gain of the IR, in series with Wet |
| Output | −60…+12 dB | 0 dB | Output trim — the drag line on the OUT meter (double-click resets) |
| Tone | −100…+100 % | 0 | Tilt EQ on the wet (left darker, right brighter) |
| In HP | 20…2000 Hz | 20 Hz | Pre-IR high-pass (low cut), 12 dB/oct, on the IR input |
| In LP | 200…20000 Hz | 20000 Hz | Pre-IR low-pass (high cut), 12 dB/oct, on the IR input |
| Filter Q | 0…100 % | 0 | Resonance shared by In HP/In LP (0 = flat) |
| Pre-Delay | 0…500 ms | 0 ms | Delays the wet only |
| Width | 0…200 % | 100 % | M/S stereo width of the wet |
| Bass Mono | 20…500 Hz | 20 Hz | Collapses the wet to mono below this frequency (20 = off) |
| Duck | 0…100 % | 0 | Ducks the wet under the live dry envelope |
| Duck Release | 20…1000 ms | 200 ms | Recovery time for Duck and Gate |
| Gate | 0…100 % | 0 | Gates the signal feeding the convolution (gated reverb; 0 = off) |
| Fade-In | 0…10 s | 0 ms | Onset ramp baked into the IR (a tick on the knob marks the usable max) |
| Decay | 0…100 % | Off | Exponential decay cut on the IR tail (0 = the IR's own decay) |
| Tail-Taper | 0…500 ms | 10 ms | De-click ramp at the IR's end |
| Stretch | 25…400 % | 100 % | Time-stretches the IR (shorter or longer than recorded) |
| Damp | 0…100 % | 0 | Progressive HF rolloff over the tail (air absorption) |
| Reverse | on/off | off | Reverse the IR (reverse-reverb) |
| Norm IR | on/off | off | Normalize the IR to unit energy (off = its recorded level) |
| Filter IR | on/off | off | Apply In HP/In LP to the IR instead of the input (same sound, shown in the display) |
| Wet Comp | on/off | on | Adaptive wet gain compensation (tracks dry loudness, tail-safe) |
| Ø (Polarity) | on/off | off | Inverts the wet signal |
| Bypass | on/off | off | Click-free, host-wired bypass |

## How it works

- **Zero latency, any IR:** short IRs (< 1.5 s) use a uniform zero-latency convolution; long
  IRs use a non-uniform engine that's much cheaper on long tails. The choice happens at file
  load, and the plugin always reports zero latency to the host.
- **IR shaping bakes into the kernel:** trim (Start/End handles), Reverse, Fade-In, Decay,
  Tail-Taper, Stretch, and Damp re-window the impulse response off the audio thread; the
  audio thread stays a pure convolution. Level-stepping changes (like Norm IR) crossfade in
  at matched loudness, so no toggle pops or pumps.
- **Pre-IR filter & Bass Mono:** the 12 dB/oct In HP/In LP (with shared resonance) shape the
  signal *before* it hits the IR — the dry path stays clean; **Filter IR** instead bakes them
  into the kernel (same sound, visible in the display). **Bass Mono** folds the wet below the
  crossover to mono while everything above stays untouched — works with any IR.
- **IR level:** the IR's recorded level is used as-is by default; **Norm IR** normalizes the
  kernel to unit energy so wildly different files land at a similar loudness.
- **Adaptive Wet Comp (default on):** trims the wet in real time so its loudness tracks the dry
  input, holding the gain while the input is quiet so reverb tails ring out instead of pumping.
- **Always-on clip guard:** a soft-clip ceiling on the output, transparent below −2.5 dBFS.
- The IR display shows the **processed** IR (heights on a perceptual scale), with the wet path's
  EQ response overlaid as a 20 Hz–20 kHz log curve and a dotted marker at the Bass-Mono
  crossover. **Play** auditions the IR itself (Baked or Raw); right-click reveals the file;
  **Presets** save and recall the full plugin state.

## Build

```sh
cmake -B build          # defaults to Release
cmake --build build
```

JUCE 8.0.6 is fetched automatically (pinned).

## Code signing

Free code signing on Windows provided by [SignPath.io](https://signpath.io),
certificate by [SignPath Foundation](https://signpath.org).

Releases include a `SHA256SUMS` file signed with the mvzn release key
([`packaging/mvzn-release-key.asc`](packaging/mvzn-release-key.asc)) — verify a download with
`gpg --verify SHA256SUMS.asc SHA256SUMS && sha256sum -c SHA256SUMS`.

## License

**GNU AGPLv3** — © 2026 mvzn. See [LICENSE](LICENSE).

Convo is built with [JUCE](https://juce.com), used under its AGPLv3 option. JUCE is
dual-licensed (AGPLv3 or commercial); distributing Convo as free open source under the
AGPLv3 is what keeps it compliant without a paid JUCE licence. Any work that links this
code is therefore also bound by the AGPLv3.
