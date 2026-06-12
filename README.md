# Obsidian v0.2 — Wavetable Synthesizer (VST3)

A JUCE-based wavetable synth built around the same architecture as Serum.

## Features

**Oscillators**

- Two wavetable oscillators with frame morphing and FFT-built anti-aliasing
  mipmaps (9 band-limited levels per frame)
- Three tables: Basic (sine→tri→saw→square→growl), Pulse (PWM sweep), and
  **User** — load any WAV and it’s sliced into 2048-sample frames, Serum-style
- **Warp modes**: Sync, Bend, Mirror, and FM-from-Osc-B (osc A)
- Semi + fine tuning per oscillator
- Sub oscillator (-1/-2 oct sine) and white noise
- Unison up to 7 voices with detune and stereo width

**Filter**

- SVF lowpass / bandpass / highpass, or 24 dB ladder
- Pre-filter drive (tanh), resonance, Env 2 amount, LFO 1 amount

**Modulation**

- Env 1 (amp) + Env 2 (assignable), full ADSR each
- 2 LFOs: sine / triangle / saw / square / S&H, free Hz or host-tempo-synced
  (1/1 … 1/16, triplets, dotted)
- **8-slot mod matrix**: Env 2, LFO 1/2, velocity, mod wheel, note, random →
  morphs, levels, pitch, cutoff, resonance, pan, unison detune, warp amounts
- Glide/portamento, pitch bend with adjustable range

**FX rack** (in series): PHAT density macro → Womp amp-movement → bit/sample crush → distortion → chorus → phaser → stereo delay → reverb → stereo width → compressor, each with its own on/off and controls

**Other**

- 8-voice polyphony, full DAW state recall
- Preset save/load (.obsn files) and Init patch
- Resizable Serum-inspired single-page UI with top mode strip, left mod rail, oscillator displays, filter curve, LFO/ENV editors, FX grid, PHAT macro, and keyboard footer

## Building

### Prerequisites

- CMake 3.22+, Git (JUCE is fetched automatically on first configure)
- **Windows:** Visual Studio 2022 · **macOS:** Xcode ·
  **Linux:** GCC/Clang plus:
  `sudo apt install libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype6-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev`

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The VST3 auto-installs to your system plugin folder
(`C:\Program Files\Common Files\VST3\`, `~/Library/Audio/Plug-Ins/VST3/`,
or `~/.vst3/`). A Standalone app is built too, under
`build/Obsidian_artefacts/Release/Standalone/`.

## Project layout

```
CMakeLists.txt              Build config (fetches JUCE 8)
Source/
  Wavetable.h               Wavetable + FFT mipmap builder + WAV import + oscillator
  Modulation.h              LFO + mod matrix source/destination definitions
  SynthVoice.h              Voice: oscs, warps, FM, sub/noise, glide, filters, matrix
  FXChain.h                 Global FX rack
  PluginProcessor.h/.cpp    ~90 parameters, synth engine, audio callback
  PluginEditor.h/.cpp       Serum-style neon UI, preset + wavetable file handling
Presets/                    Starter bass presets: womp, reese, growl, sub, lead
```

## v0.3 PHAT pass

This build pushes the project closer to the supplied Serum-style reference:

- New 1215 x 983 default layout, top mode strip, preset screen, left mod rail, bottom keyboard footer, and denser FX rack.
- New **PHAT** macro in the header for one-knob bass density and soft clipping.
- New **Womp** FX for animated bassline movement.
- New **Crush** FX for bit-depth and sample-rate reduction.
- New **Width** FX for mid/side widening after ambience.
- Added five starter `.obsn` presets in `Presets/` for immediate wonky bass testing.

## Honest gap analysis vs Serum

Still missing, roughly in order of effort:

1. **Visual wavetable editor** — drawing waveforms, FFT bin editing, the 3D view
1. **Drawable LFO curves** with grid snapping (current LFOs are fixed shapes)
1. **Drag-and-drop mod assignment** from source to knob
1. **More warp modes** (Serum has ~14), FM/AM/RM between oscillators as a mode set
1. **Per-FX modulation** and reorderable FX chain
1. **Factory content** — Serum ships ~150 wavetables and hundreds of presets
1. **Oversampling** options (2x–8x) for the warp/FM paths
1. Granular extras: chaos oscillators, alternate unison stack modes, MPE

The engine architecture (mipmapped tables, control-rate matrix, per-voice DSP)
is built so each of these can be added without rewrites.

## License note

JUCE is dual-licensed (GPLv3 or commercial). GPL release = free; closed-source
distribution requires a JUCE license. “Serum” is a trademark of Xfer Records —
don’t ship under a confusingly similar name.
## Monster Glow-Up Pass

This build has been expanded from the original PHAT pass into a heavier bass-first wavetable instrument:

- 9 wavetable slots: Basic, Pulse, Growl, Formant, Metal, Reese, Acid, Organ, plus User WAV import.
- Expanded oscillator warp modes: Off, Sync, Bend, Mirror, Fold, Asym, Quant, and Osc B FM for Osc A.
- Bass sub oscillator now has selectable Sine, Triangle, and Square shapes.
- FX rack expansion: Hyper stereo micro-delay, 2-band bass/air EQ, and a final soft-ceiling limiter in addition to Womp, Crush, Distortion, Chorus, Phaser, Delay, Reverb, Width, and Compressor.
- UI glow pass: animated bloom field, bigger default canvas, extra FX rack modules, sub waveform selector, and a more aggressive neon status strip.
- Preset folder now includes 30 new bass patches on top of the original five.

The intent is Serum-style breadth rather than a cosmetic-only update: more oscillator spectra, more phase-warp behaviours, bigger bass utility FX, and a usable preset bank for immediate testing.

## Next-level synth pass

This build includes a broader wavetable-synth feature pass:

- 17 wavetable slots, including new Chaos, Vowel, Razor, Glass, Monster, Dirty, Phase and Alien factory tables plus User import.
- Larger warp menus, with FM, AM and RM from Osc B into Osc A.
- Additional phase warps: Phase Bend, Squeeze, Window, Fractal, Flip, Wrap and Crush.
- Warp Quality selector with 1x, 2x, 4x and 8x oscillator oversampling for cleaner aggressive warp/cross-mod tones.
- Drawable-style LFO shapes, Draw 1 and Draw 2, with Draw A and Draw B curve controls and display preview handles.
- 15 extra factory presets using the new tables, warp modes and drawable LFO shapes.

See `NEXT_LEVEL_SYNTH_PASS.md` for the patch notes.


## Serum 2 Parity Pass

This build now includes a third main oscillator, expanded oscillator type selectors, a richer sub/noise source section, dual-filter routing, additional modulation parameters, and new FX rack modules. See `SERUM2_PARITY_PASS.md` for the implementation details and notes.
