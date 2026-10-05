# Pastel Soundfont Player

A simple SF2/SFZ player plugin (VST3 + Standalone) with a flat pastel UI.
Load a soundfont and play — ADSR, filter, LFO and all effects default to
**off**, so libraries sound exactly as authored until you engage something.
Master defaults to −12 dB headroom (hot banks peak well above 0 dBFS raw).

## Features

- **Formats:** `.sf2` (via TinySoundFont) and `.sfz` (built-in parser:
  includes, defines, `#define`, UTF-16, extended opcodes — `sw_last`,
  `sw_down`, `sw_default`, `sw_label`, round-robin, `lorand/hirand`,
  release triggers, `off_by/off_mode/off_time`, choke groups, `locc/hicc`,
  `gain_cc/volume_oncc/cutoff_cc`, `ampeg_*`, region filters, `pitch_keytrack`…)
- **SFZ articulation handling modeled on [sfizz](https://github.com/sfztools/sfizz):**
  exact-switch matching, per-region round-robins, victim-declared `off_by`,
  live CC gates, per-CC last-wins `gain_cc` (no dB stacking), `sw_default`
  preselect, v² velocity law, linear-attack / exp(−9) envelopes
- **Sound:** UI ADSR (toggle), resonant LP/HP/BP filter 6–48 dB/oct + drive
  (toggle), filter LFO (toggle), chorus / phaser / flanger / distortion /
  saturation / reverb / delay (each toggled), transparent limiter,
  mono sum, full dotted/triplet arpeggiator
- **Browser:** folder shortcuts (persisted), fast indexed sample search,
  background loading with progress (old sound keeps playing until swap),
  audition keyboard with mapped/switch/active-switch highlights and
  articulation names
- **Tests:** `PastelHarness` console app, 148 checks incl. real-bank
  articulation audits (`PASTEL_ARTBANK`, `PASTEL_SCOPE`, `PASTEL_RENDER`)

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# VST3 + Standalone land under build/PastelPlayer_artefacts/Release/
```

Requires CMake, Ninja, a C++17 compiler, and internet access once for the
JUCE dependency (or point it at an existing checkout). Tested on Linux.

Install the VST3 with:

```sh
cp -r "build/PastelPlayer_artefacts/Release/VST3/Pastel Soundfont Player.vst3" ~/.vst3/
```

## License

GPLv3 — see [LICENSE](LICENSE). Same license as
[ERSA](https://github.com/Paree24) (whose filter/FX DSP character this
player reuses), so the two are fully license-compatible.

## Credits

- **sfizz** (ISC, [sfztools/sfizz](https://github.com/sfztools/sfizz)) —
  no sfizz code is included; its published source was used purely as a
  behavioral reference for SFZ semantics (switching, round-robins,
  off-groups, CC handling, envelopes). Thanks to the sfizz authors for
  documenting the format's edge cases in code.
- **ERSA** — filter (Zavalishin SVF), BBD chorus, 6-stage phaser, ModVerb,
  tape/tube saturation and damped-delay character ported from ERSA (GPLv3).
- [TinySoundFont](https://github.com/schellingb/TinySoundFont) (zlib) for
  SoundFont2 playback.
- [JUCE](https://juce.com/) (GPLv3 for open-source use) for the plugin
  framework.
- Lato font (SIL Open Font License 1.1).
