# Pastel Soundfont Player

[![Build](https://github.com/Paree24/pastel-soundfont-player/actions/workflows/build.yml/badge.svg)](https://github.com/Paree24/pastel-soundfont-player/actions/workflows/build.yml)

> **Disclaimer:** this project is vibe-coded for personal use. It is provided
> as-is, without warranty of any kind. The author is not responsible for
> anything — Use at your own risk.

A simple SF2/SFZ player plugin (VST3 + Standalone) with a flat pastel UI.
Load a soundfont and play — ADSR, filter, LFO and all effects default to
**off**, so libraries sound exactly as authored until you engage something.
Master defaults to −6 dB headroom.

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

Prerequisites on all platforms: CMake ≥ 3.22, git (JUCE 8.0.6 is fetched
automatically on first configure, ~100 MB), and a C++17 compiler.
Build outputs (VST3 + Standalone) land under
`build/PastelPlayer_artefacts/Release/`.

### Linux (tested)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Install the VST3 with:

```sh
cp -r "build/PastelPlayer_artefacts/Release/VST3/Pastel Soundfont Player.vst3" ~/.vst3/
```

### Windows

Install Visual Studio 2022 (any edition, with the “Desktop development
with C++” workload), CMake (via the installer or `winget install Kitware.CMake`),
Ninja (`winget install Ninja-build.Ninja`) and git. Then in
“x64 Native Tools Command Prompt” (or PowerShell):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build
```

Copy `build\PastelPlayer_artefacts\Release\VST3\Pastel Soundfont Player.vst3`
to `C:\Program Files\Common Files\VST3\` and rescan plugins in your DAW.
(Alternatively use `-G "Visual Studio 17 2022" -A x64` and build the
generated solution instead of Ninja.)

### macOS

Install Xcode (or the Command Line Tools: `xcode-select --install`),
CMake (`brew install cmake ninja`) and git. Then:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

Copy `build/PastelPlayer_artefacts/Release/VST3/Pastel Soundfont Player.vst3`
to `~/Library/Audio/Plug-Ins/VST3/` (all users:
`/Library/Audio/Plug-Ins/VST3/`) and rescan plugins in your DAW.
Note: the plugin is unsigned — on first run macOS may block it; allow it
in System Settings → Privacy & Security, or remove the quarantine flag:

```sh
xattr -dr com.apple.quarantine \
  ~/Library/Audio/Plug-Ins/VST3/"Pastel Soundfont Player.vst3"
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
