# SynSyn 1000

A 16-voice, 3-oscillator subtractive synthesizer by DoubleDouble Instrumentations,
built with JUCE 8. Plug-in formats: VST3 and LV2 (Linux), plus a Standalone app.
Formerly developed under the working name "VST Forge Synth" (0.1.0 - 0.6.0).

- Changes: [CHANGELOG.md](CHANGELOG.md)
- Developer notes (architecture, presets, tests, Windows status): [NOTES.md](NOTES.md)

## Features
- 3 band-limited oscillators (5 waveforms), 16 voices
- Resonant low-pass / high-pass / band-pass filter with its own ADSR, amp ADSR
- 2 LFOs with assignable targets and tempo sync; mod wheel scales LFO 1
- Chorus, delay and reverb
- Preset browser with 24 factory presets (Bass, Lead, Pad, Pluck, Keys, FX) and user presets

## Building (Linux)

Requirements (Debian 13 / Ubuntu 24.04 or newer):

```bash
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxcomposite-dev libxext-dev libcurl4-openssl-dev libgl1-mesa-dev lv2-dev ladspa-sdk
```

JUCE is not included in this repository. Fetch JUCE 8.0.15 (unmodified) into `external/JUCE`:

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE
```

(Alternatively, as a submodule: `git submodule add https://github.com/juce-framework/JUCE.git external/JUCE`
then `git -C external/JUCE checkout 8.0.15`.)

Configure and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Artefacts (in `build/VSTForgeSynth_artefacts/Release/`):

| Format | File | Install to |
|---|---|---|
| VST3 | `VST3/SynSyn 1000.vst3` | `~/.vst3/` |
| LV2 | `LV2/SynSyn 1000.lv2` | `~/.lv2/` |
| Standalone | `Standalone/SynSyn 1000` | anywhere |

Tests (offline renders and checks; needs `xvfb` for the editor snapshots):

```bash
xvfb-run -a build/VSTForgeRenderTest_artefacts/Release/VSTForgeRenderTest renders
```

Windows: not yet available. JUCE 8 does not support MinGW; a Windows build needs MSVC
or clang-cl (see NOTES.md).

## Licence

Copyright (C) 2026 DoubleDouble Instrumentations.

SynSyn 1000 is free software: you can redistribute it and/or modify it under the terms of
the GNU Affero General Public License as published by the Free Software Foundation, either
version 3 of the License, or (at your option) any later version (`AGPL-3.0-or-later`).
It comes with ABSOLUTELY NO WARRANTY. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
Where the corresponding source is published: [SOURCE_OFFER.txt](SOURCE_OFFER.txt).

## Credits
- **JUCE 8** (Raw Material Software / JUCE), used under the AGPLv3 option of its dual licence.
- **VST 3 SDK** by Steinberg Media Technologies GmbH, MIT licence; Steinberg's copyright and
  licence notice is reproduced in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
- **LV2** (lv2, serd, sord, sratom, lilv), ISC licence.
- Further components bundled by JUCE (zlib, libpng, IJG jpeglib, HarfBuzz, SheenBidi, FLAC,
  Ogg Vorbis) and their licences: [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
- The "VST Compatible" logo in `resources/` is Steinberg's, included unaltered; it is not
  covered by the AGPL and its use is subject to Steinberg's VST usage guidelines.

VST is a registered trademark of Steinberg Media Technologies GmbH.
