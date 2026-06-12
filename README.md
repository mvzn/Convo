# Convo

A simple, MIDI-free **convolution audio effect**. Drop an impulse response, hear your
signal convolved with it. Open-source (MIT).

Convo is the stripped-down sibling of *Convsyn*: same convolution/IR core, none of the
synth machinery (no MIDI, no ADSR, no transposition) — just a focused convolver with
musical shaping controls.

## Controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Dry | −60…+6 dB | 0 dB | Dry (unprocessed) level |
| Wet | −60…+6 dB | 0 dB | Convolved level |
| Output | −60…+12 dB | 0 dB | Master output trim |
| Tone | −100…+100 % | 0 | Tilt EQ on the wet (left darker, right brighter) |
| Pre-Delay | 0…500 ms | 0 ms | Delays the wet only |
| Width | 0…200 % | 100 % | M/S stereo width of the wet |
| Duck | 0…100 % | 0 | Ducks the wet under the live dry envelope |
| Duck Release | 20…1000 ms | 200 ms | Ducking recovery time |
| Fade-In | 0…1000 ms | 0 ms | Onset ramp baked into the IR |
| Decay | 50 ms…Off | Off | Exponential decay imposed on the IR tail |
| Tail-Taper | 0…500 ms | 10 ms | De-click ramp at the IR's end |
| Reverse | on/off | off | Reverse the IR (reverse-reverb) |
| Bypass | on/off | off | Click-free, host-wired bypass |

## How it works

- **Adaptive engine:** short IRs (< 1.5 s) use a zero-latency convolution; long IRs (≥ 1.5 s)
  use a 512-sample-latency engine that's cheaper on long tails. The choice is made when you
  load a file, and the plugin reports its latency to the host accordingly.
- **IR shaping bakes into the kernel:** Reverse, Fade-In, Decay, and Tail-Taper are applied to
  the impulse response on the message thread; the audio thread stays a pure convolution.
- **No normalization:** the IR's recorded level is preserved. Use Output and the OUT meter to
  manage gain — the loudness is honest.
- The IR display shows the **processed** IR, so the shaping knobs are visible in the waveform.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 8.0.6 is fetched automatically (pinned). The VST3 is built on all platforms; AU on macOS.

## License

MIT — see [LICENSE](LICENSE).
