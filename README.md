# Convo

A simple, MIDI-free **convolution audio effect**. Drop an impulse response, hear your
signal convolved with it. Free and open source (AGPLv3).

Convo is the stripped-down sibling of *Convsyn*: same convolution/IR core, none of the
synth machinery (no MIDI, no ADSR, no transposition) — just a focused convolver with
musical shaping controls.

## Controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Dry | −60…+6 dB | −12 dB | Dry (unprocessed) level |
| Wet | −60…+6 dB | −12 dB | Convolved (wet) level |
| IR Gain | −60…+6 dB | 0 dB | Gain of the IR, in series with Wet |
| Output | −60…+12 dB | 0 dB | Master output trim |
| Tone | −100…+100 % | 0 | Tilt EQ on the wet (left darker, right brighter) |
| In HP | 20…2000 Hz | 20 Hz | Pre-IR high-pass (low cut), 6 dB/oct, on the IR input |
| In LP | 200…20000 Hz | 20000 Hz | Pre-IR low-pass (high cut), 6 dB/oct, on the IR input |
| Pre-Delay | 0…500 ms | 0 ms | Delays the wet only |
| Width | 0…200 % | 100 % | M/S stereo width of the wet |
| Duck | 0…100 % | 0 | Ducks the wet under the live dry envelope |
| Duck Release | 20…1000 ms | 200 ms | Ducking recovery time |
| Fade-In | 0…10000 ms | 0 ms | Onset ramp baked into the IR (capped at 80% of the IR length) |
| Decay | 50 ms…Off | Off | Exponential decay imposed on the IR tail |
| Tail-Taper | 0…500 ms | 10 ms | De-click ramp at the IR's end |
| Bass Mono | 20…500 Hz | 20 Hz | Mid/Side only: high-pass the side so lows go mono (20 = off) |
| Reverse | on/off | off | Reverse the IR (reverse-reverb) |
| Raw IR | on/off | off | Use the IR's recorded level (disables auto-level) |
| Filter IR | on/off | off | Apply In HP/In LP to the IR instead of the input (same sound, shown in the display) |
| Wet Comp | on/off | on | Adaptive wet gain compensation (tracks dry loudness, tail-safe) |
| Mid/Side | on/off | off | Convolve mid-with-mid and side-with-side (re-bakes the IR as M/S) |
| Clip Guard | on/off | on | Soft-clip ceiling on the output (transparent below −2.5 dBFS) |
| Bypass | on/off | off | Click-free, host-wired bypass |

## How it works

- **Adaptive engine:** short IRs (< 1.5 s) use a zero-latency convolution; long IRs (≥ 1.5 s)
  use a 512-sample-latency engine that's cheaper on long tails. The choice is made when you
  load a file, and the plugin reports its latency to the host accordingly.
- **IR shaping bakes into the kernel:** Reverse, Fade-In, Decay, and Tail-Taper are applied to
  the impulse response on the message thread; the audio thread stays a pure convolution.
- **Pre-IR filter, Mid/Side & Bass Mono:** a 6 dB/oct high-pass and low-pass shape the signal
  *before* it hits the IR (the dry path stays clean); **Filter IR** instead bakes them into the
  kernel (same sound, visible in the display). Mid/Side mode re-bakes the IR as mid/side and
  convolves mid-with-mid, side-with-side (wants a stereo IR); **Bass Mono** high-passes the side
  so lows below the crossover collapse to mono. Routing toggles are click-masked by the load fade.
- **Auto-level (default):** the IR kernel is normalized to unit energy when it's baked, so very
  different IRs land at similar loudness. **Raw IR** turns this off to use the recorded level for
  calibrated IRs.
- **Adaptive Wet Comp (default on):** trims the wet in real time so its loudness tracks the dry
  input, holding the gain while the input is quiet so reverb tails ring out instead of pumping.
- **Clip Guard (default on):** a soft-clip ceiling on the output that's transparent below −2.5 dBFS.
- The IR display shows the **processed** IR, so the shaping knobs are visible in the waveform. Overlaid on it is the wet path's EQ response (Tone tilt + In HP/In LP) drawn as a 20 Hz–20 kHz log curve, plus a dotted vertical marker at the Bass-Mono crossover.

## Build

```sh
cmake -B build          # defaults to Release
cmake --build build
```

JUCE 8.0.6 is fetched automatically (pinned). The VST3 is built on all platforms; AU on macOS,
and `COPY_PLUGIN_AFTER_BUILD` installs it to your user plugin folder.

**Use Release for real use.** A fresh configure defaults to Release; a Debug build is *much*
slower and stutters/drops out on long (multi-second) IRs — it's for debugging only. For a
debuggable but real-time-capable build use `-DCMAKE_BUILD_TYPE=RelWithDebInfo`; for full Debug,
`-DCMAKE_BUILD_TYPE=Debug` (don't leave that one installed).

## License

**GNU AGPLv3** — © 2026 mvzn. See [LICENSE](LICENSE).

Convo is built with [JUCE](https://juce.com), used under its AGPLv3 option. JUCE is
dual-licensed (AGPLv3 or commercial); distributing Convo as free open source under the
AGPLv3 is what keeps it compliant without a paid JUCE licence. Any work that links this
code is therefore also bound by the AGPLv3.
