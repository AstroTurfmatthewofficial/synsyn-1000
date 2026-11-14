# SynSyn 1000 (formerly VST Forge Synth): developer notes

Copyright (C) 2026 DoubleDouble Instrumentations, AGPL-3.0-or-later (see LICENSE, NOTICE, THIRD_PARTY_NOTICES.txt, SOURCE_OFFER.txt).

## Build

```bash
# Toolchain (Debian 13 / Ubuntu): build-essential cmake ninja-build pkg-config libasound2-dev
# libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev
# libxcursor-dev libxcomposite-dev libxext-dev libcurl4-openssl-dev libgl1-mesa-dev lv2-dev ladspa-sdk
# Validation tools: sordi lilv-utils (lv2_validate is in lv2-dev; it skips paths with spaces, so copy the .ttl files to a plain dir)
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2        # keep -j low: LTO links need ~1+ GB each

# Tests
xvfb-run -a build/VSTForgeRenderTest_artefacts/Release/VSTForgeRenderTest renders
xvfb-run -a tools/pluginval --strictness-level 10 --validate-in-process \
    --validate "build/VSTForgeSynth_artefacts/Release/VST3/SynSyn 1000.vst3"
xvfb-run -a tools/pluginval --strictness-level 10 --validate-in-process \
    --validate "build/VSTForgeSynth_artefacts/Release/LV2/SynSyn 1000.lv2"
LV2_PATH=build/VSTForgeSynth_artefacts/Release/LV2 lv2info urn:doubledouble:synsyn1000
python3 packaging/make_third_party_notices.py --windows   # regenerate THIRD_PARTY_NOTICES.txt
```

pluginval v1.0.4 (Linux binary from GitHub releases) lives in `tools/`. It isn't committed; download it again from
https://github.com/Tracktion/pluginval/releases.

M5 fix: pluginval's VST3 "Plugin state restoration" test (which also randomises the program parameter) exposed a JUCE quirk. `AudioParameterBool` keeps a raw float such as 0.75, and the APVTS treats that as already equal to a restored 1, so it skips the restore. `setStateInformation` now pushes the restored value into any parameter whose normalised value is off by more than 1e-4. Differences at float-ulp level are untouched, so M1–M4 sessions still render bit-identically.

Factory preset workflow (only needed when changing the factory bank):
```bash
python3 presets/tune_levels.py      # regenerate sparse presets + tune master_gain (peak ~ -4.5 dBFS)
build/VSTForgeRenderTest_artefacts/Release/VSTForgeRenderTest --normalise-presets presets/factory   # -> full state
cmake --build build -j3             # re-embeds presets/factory/**/*.vfpreset (BinaryData)
```

## MIDI input (M6)
- **Standalone:** JUCE's desktop Standalone wrapper opens no MIDI input devices by default (`autoOpenMidiDevices` is mobile-only). `Source/standalone/StandaloneMidi.*` does it instead: it's created by `createPluginFilter()` (plugin target only, since it needs `juce_audio_plugin_client`) when `wrapperType == wrapperType_Standalone`. It attaches to `StandalonePluginHolder::getInstance()->deviceManager` once the holder owns the processor.
  - It enables all available inputs at startup and on hot-plug (`MidiDeviceListConnection` plus a 1 s poll), and closes vanished ones.
  - A device the user unticks in Options › Audio/MIDI Settings is stored by name under `vstforgeMidiDisabledInputs` in `~/.config/SynSyn 1000.settings` (was `VST Forge Synth.settings` before 0.6.1) and isn't re-enabled automatically. Ticking it again removes it from that list.
  - It publishes the open-input count and names to the processor (`setStandaloneMidiInputs`).
- **Plugins:** `IS_SYNTH`/`NEEDS_MIDI_INPUT` are on, so VST3 has an event input bus and LV2 has an Atom MIDI input. Host MIDI reaches `processBlock`, where the on-screen keyboard is merged into it.
- **MIDI LED** (`vf::gui::MidiLed` in `Components.*`, in the title bar):
  - The processor counts external channel messages (before the on-screen keyboard merge) in `std::atomic<uint32_t> midiActivityCount`, and sets `externalMidiSeen` for any message. Both use relaxed atomics, with no locks or allocation.
  - `isMidiInputConnected()` means "≥ 1 open input device" in the Standalone, and "host delivered MIDI at least once" (latched) in a plugin.
  - The LED polls at 30 Hz: dark, steady red, or a 90 ms bright flash when the counter changes.
- **Troubleshooting** on the target machine: `aconnect -l` / `amidi -l` should list the device. In the Standalone, Options › Audio/MIDI Settings shows the inputs ticked, and the LED is red. If notes are doubled because a second port (e.g. a PipeWire bridge or Midi Through) forwards the same keyboard, untick the duplicate; that choice is remembered.

## Presets (M5)
- **User presets:** `~/.local/share/SynSyn1000/Presets/<Category>/<Name>.vfpreset`. `$XDG_DATA_HOME/SynSyn1000/Presets` is used if set, and the env var `SYNSYN1000_PRESET_DIR` (or the old `VSTFORGE_PRESET_DIR`) overrides both (the tests use a temp dir). On other OSes it's JUCE's `userApplicationDataDirectory/SynSyn1000/Presets`.
- **Migration (0.6.1):** on construction, if no override is set, `migrateLegacyUserPresets()` copies `…/VSTForgeSynth/Presets` to `…/SynSyn1000/Presets` when the old one exists and the new one does not (`File::copyDirectoryTo`; the old folder is never touched).
- **Factory presets:** source files in `presets/factory/<Category>/*.vfpreset`, embedded at build time (`juce_add_binary_data`, namespace `VSTForgeFactoryPresets`). They are parsed once into memory, so nothing needs to exist on disk at runtime.
- **File format** (UTF-8 XML; all 68 parameters are written, as plain values like the APVTS session state):
  ```xml
  <SynSyn1000Preset format="1" name="Warm Strings" category="Pad" author="DoubleDouble Instrumentations" version="0.6.1">
    <SynSyn1000>
      <PARAM id="amp_attack" value="0.6"/> ...
    </SynSyn1000>
  </SynSyn1000Preset>
  ```
  - Pre-0.6.1 files use `<VSTForgePreset>`/`<VSTForgeSynth>`; the loader accepts old and new tags in any combination. Saves write the new tags. Session state likewise accepts a `<VSTForgeSynth>` root and writes `<SynSyn1000>`.
  - On load, missing IDs take their defaults, unknown IDs are ignored, NaN/Inf values take the default, and other values are clamped to the parameter range.
  - The category must be one of Bass, Lead, Pad, Pluck, Keys, FX or User; anything else becomes User.
  - Names are 1–32 characters without `/ \ : * ? " < > |`. The file name is the name, made legal.
- **List order:** 00 Init, 01–24 factory (sorted by category, then name), then U1… user presets (same sort). Host programs are 0 = Init and 1–24 = factory.
- **Session state:** the `presetName`, `presetCategory`, `presetKind` (init/factory/user/none), `presetFile` and `presetModified` properties sit on the state tree. Restoring them does no file I/O; a restored user preset is resolved by path at the next rescan (when the editor opens).

## Artefacts (Release)
- VST3: `build/VSTForgeSynth_artefacts/Release/VST3/SynSyn 1000.vst3`, which goes in `~/.vst3/`
- LV2: `build/VSTForgeSynth_artefacts/Release/LV2/SynSyn 1000.lv2`, which goes in `~/.lv2/`
- Standalone: `build/VSTForgeSynth_artefacts/Release/Standalone/SynSyn 1000`

## Layout
```
CMakeLists.txt          JUCE via add_subdirectory(external/JUCE); VSTForgeCore INTERFACE lib shared by plugin + tests
Source/Parameters.*     parameter IDs + layout (APVTS), raw-value handles for the audio thread
Source/dsp/Oscillator.h band-limited oscillator (PolyBLEP/PolyBLAMP), 5 ms waveform crossfade, allocation-free
Source/dsp/Filter.h     TPT state-variable filter (LP/HP/BP) + analog magnitude for the UI curve
Source/dsp/Lfo.h        LFO shapes/targets/divisions; stateless evaluation from a position (S&H = hash of cycle index)
Source/dsp/Effects.h    FX: DelayLine (cubic), BypassRamp, Chorus, StereoDelay, ReverbFx, FxChain
Source/dsp/SafetyLimiter.h  output peak limiter (-1 dBFS ceiling, NaN/Inf scrub)
Source/dsp/SynthVoice.* voice = 3 osc -> SVF (cutoff + keytrack + filter env) -> amp ADSR; VoiceParams snapshot per block
Source/PluginProcessor.* synth, MIDI merge (on-screen keyboard/wheels), master gain, state
Source/presets/PresetManager.*  preset files, factory bank (BinaryData), list/filter, load/save/rename/delete, programs, modified flag
Source/PluginEntry.cpp  createPluginFilter() (plugin target only); attaches the Standalone MIDI helper
Source/standalone/StandaloneMidi.*  Standalone-only MIDI auto-connect (plugin target only, M6)
Source/gui/ForgeLookAndFeel.*  colours + drawing primitives (LCD, cells, faders, popup menus)
Source/gui/Components.*  SectionPanel, Strip, LcdParam, ParamFader, Envelope/Waveform/Filter/LFO/Fx displays
Source/gui/Sections.*    sections: Volume, Oscillator N, Envelope (generic ADSR), Filter, LFO N, Chorus, Delay, Reverb
Source/gui/PresetBrowser.*  Memory section, Preset Browser overlay, Save As/Rename/confirm dialogs (PresetUi glue)
Source/gui/KeyboardSection.*  keyboard strip: Pitch/Mod wheels, octave arrows, range overview
Source/gui/PluginEditor.*  title bar + strips + keyboard; uniform scaling of a fixed design size
tests/RenderTest.cpp    offline render + checks, writes renders/*.wav, m5_editor.png, m5_browser.png, m5_saveas.png, m6_editor.png
                        `--normalise-presets <dir>` / `--tune-presets <dir>` (factory bank workflow)
                        `--dump-reference` regenerates tests/reference (only do this deliberately!)
tests/reference/        reference renders (*.f32 raw stereo float32) + m2/m3_session.state; v0.4.0 (m4_session) is the newest baseline
presets/                factory bank sources (.vfpreset), generator + level tuner
dist/                   packaged builds (not in git): vst-forge-synth-m<N>-linux-x86_64.tar.gz
```

## Decisions
- **GUI reference: the SynprezFM app.** The layout follows its black background and horizontal strips of titled sections with teal outlines. Values appear in pale-green LCD readouts with labels beside them, active values are orange, and the full-width keyboard has octave arrows and a Pitch/Mod column on the left.
  - To add a module, subclass `SectionPanel`, then add it to a `Strip` in `PluginEditor.cpp`, or add a new `Strip`. The design height grows by one strip automatically.
  - The editor lays out at a 1200 px design width (1200×760 since M5) and scales uniformly with an `AffineTransform`. The aspect ratio is fixed, and the window can be resized from 60 % to 250 %.
  - `LcdParam` is the standard control. It uses `juce::ParameterAttachment`, so host automation and gestures work. Drag or use the mouse wheel to change a value, Shift gives fine control, double-click resets it, and clicking a choice parameter opens a menu.
- **Audio thread:**
  - `ScopedNoDenormals` is used.
  - Parameters are read through cached atomics once per block.
  - The audio path allocates no memory. All voices are created up front, and `MidiBuffer` additions follow the usual JUCE practice.
  - Master gain and per-voice velocity level are smoothed.
  - Waveform changes apply at block boundaries.
- **Signal flow per voice:** (osc1 + osc2 + osc3) × level, then the SVF, then × amp env × velocity. The voices are summed, then master gain, then **chorus → delay → reverb**, then the safety limiter.
- **Filter:**
  - The cutoff is the base cutoff × 2^(keytrack·(note−60)/12 + envAmt·7·filterEnv), clamped to 20 Hz … min(20 kHz, 0.45·fs).
  - Coefficients are recomputed every 8 samples with `tan()`.
  - The TPT structure is stable under any modulation because k = 1/Q > 0.
  - Band-pass uses the k·v1 output, which gives unity peak gain.
  - Resonance 0 is Butterworth, so the default open filter is flat in the audio band.
- **Limiter:** a feedback peak limiter with instant gain reduction to a 0.891 (−1 dBFS) ceiling and an 80 ms release. It's a safety net, not a mastering limiter. The demo is rendered at −10 dB so the limiter never engages.
- **Click-free waveform switching:** the phase is continuous and the old and new waveforms crossfade over 5 ms. Turning an oscillator on or off ramps its level over 10 ms. Oscillators that are off keep their phase running.
- **State:** `stateVersion` and `editorWidth` are stored as properties on the APVTS tree. On load, parameters missing from the saved state are reset to their defaults, which is how older sessions migrate.
- **LFOs (M3):**
  - `Lfo::evaluate(shape, position, seed)` is stateless. The integer part of the position is the cycle index and the fraction is the phase. Sample & hold is a hash of the cycle index.
  - In Free mode, every voice evaluates the shared position (block-start position + offset × increment), so no buffers are shared.
  - In Retrig mode, each voice keeps its own position, which resets at note-on, and gets a per-note S&H seed.
  - The processor resolves the rate per block from Hz, or from sync as (BPM/60)/beats. The tempo is the host BPM, or 120.
  - In Free mode with sync and host playback, the position is locked to `ppq / beats`.
  - Each voice smooths value×depth×fade×amount, plus |depth×fade×amount|, with a 2 ms one-pole.
  - Destinations are summed: cutoff in octaves, resonance additive (clamped 0..1), pitch in semitones at control rate, and gains multiplied per sample.
  - When no LFO is active, the voice takes the unmodulated code path, which is bit-identical to v0.2.0. LFO seeds come from their own RNG, so oscillator start phases are unchanged.
- **Effects (M4), in `dsp/Effects.h`:**
  - Every effect has a `BypassRamp` (30 ms linear). When fully off, its process call is skipped, so it costs no CPU. When turned back on, its buffers are cleared first, so there are no stale tails.
  - The ramp crossfades between the input and the processed output, so turning an effect off fades its wet signal (including its tail) over 30 ms.
  - Dry/wet law: dry = min(1, 2(1−mix)) and wet = min(1, 2·mix), so mix 0.5 gives full dry plus full wet.
  - `DelayLine` is a power-of-two circular buffer with 4-point cubic Hermite reads, sized in `prepare()` from the sample rate (maximum delay plus margin). `prepareToPlay` handles sample-rate changes by re-preparing the effects.
  - Chorus: 4 taps (L: 0° and 180°, R: 90° and 270° of the LFO), with smoothed rate and depth.
  - Delay: per-sample one-pole time smoothing (about 0.2 s), feedback clamped to 0.95, and a one-pole low-pass on the feedback signal. The loop has a `feedbackSaturate` stage that is identity below |x| = 1 and soft-saturates to ±2 above it.
  - Ping-pong: the mono input feeds the left line, and each line feeds back into the other.
  - Synced time = beats × 60000 / BPM, using the same BPM source as the LFOs. The value is clamped to the 4 s line.
  - Reverb: a pre-delay line (up to 250 ms) feeds `juce::Reverb` (Freeverb) at wet 0.5 and dry 0. Dry and wet are mixed outside the reverb, so the ramp and mix law match the other effects. `setParameters` is called only when a value changes.
  - `FxChain::process` processes in chunks of the prepared maximum block size, so oversized host blocks are safe. A mono bus is processed as stereo internally and then summed.
  - `AudioProcessor::reset()` clears the effects and the limiter.
- **Presets (M5):**
  - `PresetManager` is owned by the processor. All file I/O (scan, save, rename, delete, loading a user preset) happens on the message thread from the UI. Factory, Init and host-program loads use in-memory data only.
  - Applying a preset sets each parameter with `setValueNotifyingHost`, skipping values that are already equal.
  - Log and skewed ranges aren't exactly invertible in float. On load, the normalised value is chosen by a small ulp search so that the plain value the APVTS ends up holding equals the file value; this makes save → load → save byte-identical. Values set through JUCE's API can still land a few ulps off a pristine default, which is why the state tests use a 1e-6 relative tolerance (the round trip itself is 68/68 bit-exact).
  - **Preset change on the audio thread:**
    - The PresetManager's `beforeApply` hook increments an atomic counter. The next `processBlock` sees it.
    - If any voice is active or the last output peak was above −120 dBFS, a 10 ms linear fade starts. During it, the voice, LFO, effect and master-gain parameters stay frozen at the previous block's snapshot, so the old sound fades unchanged.
    - Note on/off events are moved to a pre-allocated `deferredMidi` buffer.
    - At the end of the fade: `allNotesOff (0, false)` plus `effects.reset()`. The deferred events are replayed at the start of the next block.
    - When nothing is sounding, voices and effects are reset immediately with no fade.
  - **Modified flag:** normalised parameter values are compared with a snapshot taken after each load or save. A sticky flag carries "modified" across a session restore.
  - **UI overlays** are children of the editor's main panel (not separate windows), so they scale with the editor and work in every host. Dialog deletion is deferred (`retired`) because dialogs close from their own button callbacks.
- **Compatibility tests:** `tests/reference` holds renders made with v0.2.0. The current build must reproduce them exactly. If a change is meant to alter old sessions on purpose, regenerate them with `--dump-reference` and document why.
- **Headroom:** each voice is scaled by 0.2 times velocity, and master gain defaults to −6 dB. A 4-note chord peaks around −10 dBFS.
- **Extensibility:**
  - `VoiceParams` is the per-block snapshot. Filter, filter envelope and LFO fields go there.
  - Add oscillator slots as more `Oscillator` members in `SynthVoice`.
  - Parameter IDs are namespaced (`osc1_*`, `amp_*`, `master_*`) and carry `parameterVersion`. Once released, they must not be renamed.
- `VSTForgeCore` is an INTERFACE library, so the plugin and the console test each compile the same sources against their own JUCE configuration.

## Platform / compatibility
- Build box: Debian 13 (trixie), **glibc 2.41**, gcc 14.2, CMake 3.31.6.
- Primary target machine: Ubuntu 26.04 (glibc 2.43), so the glibc 2.38 / GLIBCXX_3.4.32 requirement is satisfied.
- (Earlier note, for other machines:) Binaries built here require the GLIBC/GLIBCXX symbol versions listed below. If the target Ubuntu is older, rebuild in an older container, such as an Ubuntu 22.04 or 24.04 image, or on the target machine.
- Current Release binaries need **GLIBC_2.38, GLIBCXX_3.4.32 and CXXABI_1.3.15**. They should run on Ubuntu 24.04 (glibc 2.39) and newer, but not on 22.04 (glibc 2.35, GLIBCXX_3.4.30). Runtime libraries: libc, libm, libstdc++, libgcc_s, libfontconfig and libfreetype, plus libasound for the Standalone build.

## Known issues
- The LV2 TTL's `doap:mbox` resolves to a relative URI (JUCE writes COMPANY_EMAIL without `mailto:`). This is cosmetic.
- The M6 package is `dist/vst-forge-synth-m6-linux-x86_64.tar.gz`.
- **MIDI (M6):**
  - The Standalone auto-enables *every* MIDI input, including the MiniLab 3's DIN THRU, MCU/HUI and ALV ports and the system Midi Through port. Whatever those send also plays the synth. Untick any you don't want in Options › Audio/MIDI Settings; the choice is remembered by device name.
  - Hot-plug and auto-enable could not be exercised with a real or virtual ALSA device on the build box (no `/dev/snd`). The Standalone was smoke-tested under xvfb, and the LED and counter logic is unit-tested.
  - In a plugin, the LED can only show that the host has delivered MIDI, not that a device is connected. It stays lit (latched) for the life of the instance once MIDI has arrived, even if the keyboard is later unplugged or the track is un-armed. Before the first event it's dark, even when the host's routing is correct.
- The M5 package is `dist/vst-forge-synth-m5-linux-x86_64.tar.gz`. M4 is `dist/vst-forge-synth-m4-linux-x86_64.tar.gz`, and M3 is `dist/vst-forge-synth-m3-linux-x86_64.tar.gz`. All use the same VST3/, LV2/, Standalone/ layout.
- **Presets:**
  - Loading a preset while notes sound cuts them with a 10 ms fade, by design. Held keys must be re-struck to sound with the new preset.
  - Host program changes through the VST3 program parameter can arrive on the audio thread. They load from memory (no I/O), but setting 68 parameters and posting a UI change message there isn't strictly real-time safe.
  - Only the factory bank is exposed as host programs; user presets are browsed in the plug-in UI. Under LV2, JUCE also exports the programs as LV2 presets.
  - Rename keeps the category; to move a preset to another category, use Save As and then Delete.
  - Text entry in the dialogs depends on the host giving the plug-in window keyboard focus. Most Linux hosts do; if one doesn't, click into the name field first.
  - Open Folder uses the desktop's default file manager (`xdg-open`).
  - Factory levels are tuned for the test phrases (−4.5 dBFS peak). Bass and Lead presets are levelled for single-note lines, so dense full-velocity chords drive the limiter (about 2–8 dB of gain reduction in the test's 6-note velocity-127 chord). Pad, Keys, Pluck and FX stay at 0–1.8 dB.
- **Effects:**
  - The reverb is JUCE's Freeverb. It's serviceable but a bit metallic and dense-early at large sizes; a better algorithm (FDN) is a candidate for a later milestone.
  - Changing the delay time glides the pitch over about 0.2 s instead of crossfading, which is intentional, tape-style behaviour. Synced divisions longer than 4 s (4/1 or 2/1 at slow tempos) are clamped to 4 s.
  - Turning an effect off fades its wet signal and tail out over 30 ms rather than letting the tail ring on. Tails do ring on after note-off while the effect stays on.
  - At extreme settings (95 % feedback, 20 kHz high-cut, reverb at size 1 and mix 1), the signal before the limiter runs well above 0 dBFS. The feedback saturator and the safety limiter keep the output bounded at −1 dBFS, but it will sound limited.
  - On a mono output bus the stereo effects are folded down to mono, so chorus and ping-pong lose their width.
- **LFOs:**
  - Changing a destination while it's modulating hands off instantly, which can step once (for example, an amp tremolo stops abruptly).
  - Pitch modulation updates every 8 samples, which is inaudible for vibrato but makes very fast (40 Hz) S&H pitch slightly stepped.
  - In Free sync mode without host playback (standalone), the LFO runs at the synced rate from an arbitrary phase.
  - The mod wheel applies only to LFO 1 depth.
  - The Rate readout keeps showing the Hz value while Sync is on; the division in the Div readout is what's used.
- The M2 package is `dist/vst-forge-synth-m2-linux-x86_64.tar.gz` (VST3/, LV2/, Standalone/), with the same layout as m1.
- `lv2lint` isn't packaged for Debian 13, so it was skipped. I used `lv2_validate` (sord) and `lv2info` (lilv) instead. `lv2_validate` reports strict-schema errors (12 in M1, 44 in M2, 79 in M3, 108 in M4, 157 in M5, because the count grows with the parameter count) in JUCE's generated TTL: parameters that use `lv2:portProperty` on non-ports, `doap` domain, and an undefined `Program` property. All of them come from JUCE's LV2 exporter rather than from this project, and lilv and hosts load the plugin fine.
- JUCE's LV2 wrapper always exposes a latency output port, so lilv reports "Has latency: yes" even though the latency is 0.
- The safety limiter has no lookahead, so heavy overdrive (3 oscillators, high resonance, +6 dB) can produce audible gain pumping or distortion on transients. Lower the gain for clean results.
- Filter coefficients update every 8 samples. Very fast envelope sweeps can show slight zippering at high resonance.
- The resonance tops out at Q 20. There is no self-oscillation and no saturation or drive stage yet, since a ladder filter or drive is planned.
- Oscillators that are off still run their phase accumulators, which costs a little CPU. That's the trade-off for click-free re-enabling.
- On this box, `apt` fetches failed with HTTP 500 errors from the egress proxy, so the packages were downloaded with curl from deb.debian.org. Signatures and hashes were verified, and the packages were installed with `apt-get install --no-download`.

## Next milestones (proposed)
1. **Voice:** unison and spread, glide and portamento, filter drive, ladder-filter option.
2. **Mod matrix:** more sources (velocity, aftertouch, mod envelope) and multiple destinations per LFO, plus an effects-parameter destination.
3. **Master section:** a lookahead master limiter and an optional FDN reverb.
4. **Presets+:** user presets as host programs (optional), preset tags/favourites, import/export of banks.
(Done: M1 foundation; M2 oscillators 2 and 3 plus filter; M3 LFOs; M4 effects; M5 presets; M6 Standalone USB MIDI + MIDI LED. v0.5.0 completes the first-version feature set.)

## Windows build (0.6.1 attempt: blocked)
**Status:** no Windows binary yet. JUCE 8 cannot be built with MinGW.
- Installed on the box (Debian 13 packages, fetched with curl + SHA-256 checked against the signed InRelease/Packages because apt via the proxy returns HTTP 500, then `apt-get install --no-download`): `g++-mingw-w64-x86-64-posix` (GCC 14.2, + gcc/binutils/mingw-w64 headers), `wine` / `wine64` / `libwine` 10.0. About 186 MB of .debs.
- Attempt: CMake toolchain for `x86_64-w64-mingw32-g++-posix` with `-static -static-libgcc -static-libstdc++` (kept outside the repository, with the build logs).
- Result: `juce_core/system/juce_TargetPlatform.h` → `#error "MinGW is not supported. Please use an alternative compiler."` JUCE's BREAKING_CHANGES.md (8.0.1): MinGW support removed; "Cross-compiling for Windows from Linux is not supported, and there is no workaround". A throw-away probe with the `#error` removed (and `JUCE_64BIT` defined) still fails in JUCE's Direct2D renderer (`ID2D1DeviceContext3`, `ID2D1SpriteBatch` missing from MinGW-w64 12 headers) and UI Automation code (newer UIA interfaces), besides missing `<cstring>` includes. Porting JUCE 8's Windows graphics layer to MinGW is not a reasonable patch.
- Wine works: the official pluginval 1.0.4 Windows build (`tools/win/pluginval.exe`, not committed) runs under Wine 10 (`WINEPREFIX=<prefix> xvfb-run -a wine tools/win/pluginval.exe --help`), so a Windows VST3 built elsewhere can be validated here.
- Options (needs the project owner's decision):
  1. **Build natively with MSVC** (free Visual Studio Build Tools) on a Windows machine, or on a free GitHub Actions `windows-latest` runner once the AGPL source is published. Supported by JUCE; recommended.
  2. **clang-cl + lld + xwin on the box**: xwin downloads Microsoft's CRT and Windows SDK, which requires accepting Microsoft's licence; LLVM is a further large install. Works in practice for many JUCE users, but JUCE calls Linux-to-Windows cross-compiling unsupported.
  3. Patch JUCE for MinGW: not recommended (large, unsupported, unverifiable GUI code).
- CMakeLists.txt already handles Windows: `WIN32` → VST3 + Standalone (no LV2); `CMAKE_CROSSCOMPILING` → `VST3_AUTO_MANIFEST FALSE`. With MSVC: `cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64` then `cmake --build build-win --config Release`.

