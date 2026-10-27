# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Azimuter is a VST3 audio plugin (JUCE 8 / C++17) that helps a user align the
playback head azimuth of a cassette deck. It is loaded as an effect insert in
Reaper on a stereo bus carrying the deck's playback signal.

The product constraint that shapes everything else is **accessibility-first**:
the primary user is a blind audio engineer, and the plugin is their only
feedback channel during alignment. UI changes that work only by sight are
regressions, not improvements.

### How the plugin works

Azimuth misalignment is, physically, an inter-channel time delay between the
left and right gap halves. `azimuter_core` measures that delay directly via
**GCC-PHAT** (Generalised Cross-Correlation with Phase Transform): windowed
FFT of both channels, per-bin whitening of the cross-spectrum so loud
program-material transients don't dominate, time-averaging across frames,
inverse FFT, parabolic peak interpolation for sub-sample resolution. The
output is a signed delay in samples / microseconds — positive means the
right channel leads, negative means the left channel leads, zero is the
alignment target. This replaced an older HF-correlation analyser whose
reading was modulated by program-material spectral content (vocals, drums,
varying HF energy) to the point of being nearly unusable for fine alignment;
GCC-PHAT depends almost entirely on gap geometry and barely on the audio
content. It does **not** lock onto tape hiss: hiss on the left and right tracks
comes from different strips of tape and is unrelated, so in the gaps between
songs the delay reading is random (measured spread 130-155 µs, against 12-26 µs
on music). An earlier version of this file claimed otherwise.

### Zero delay is not correct azimuth

This is the thing to understand before changing anything in `Source/Azimuth/`.

A tilted head produces **two** effects, rigidly linked by the head geometry: an
inter-channel delay, and a loss of high frequencies *within each channel*. Only
the second identifies true azimuth, because a tape can carry a delay of its own
— printed by a duplicator with gap scatter, or inherited from a dub of a source
played back off-azimuth — and no head angle removes it.

Measured on the two tapes in `examples/`: correct azimuth sits at **+54 µs** on
one and **−99 µs** on the other, same deck. Aiming at zero cost 2–3.5 dB and
9–15 dB of the top octave respectively, permanently. There is no universal
target and no constant that could be shipped, so `AzimuthCalibrator` measures it
per tape from a sweep of the azimuth control, and everything user-facing is
driven by the **error against that target**, not by the raw delay.

`AzimuterAudioProcessor::getAlignmentErrorMicroseconds()` is what the readout,
the spoken buckets and the sonifier consume. `getCurrentDelayMicroseconds()` is
the raw reading and is only for diagnostics. Uncalibrated, the target is 0 and
the two coincide, which reproduces the pre-calibration behaviour exactly.

The delay estimate is a **two-stage lock**: a wide 1–18 kHz band gives an
unambiguous coarse answer, then the `cutoff`–18 kHz band refines it, searched
only within half a carrier period of the coarse result. That is the classical
"set a low tone first, then work up" procedure in one pass, and it is not
decorative — on one example tape the response nulls at 14 kHz and rises 2.5 dB
again beyond, a false peak wide enough to fool an ear.

The plugin surfaces the reading three ways, all designed to be usable without
sight:

1. **Numeric + qualitative readout** in the editor — alignment error in
   microseconds, plus a coarse magnitude bucket (Excellent < 5 µs / Good <
   15 µs / Fair < 30 µs / Poor < 60 µs / Severe < `kFullScaleDelayMicroseconds`
   / Off-scale) and a side cue ("left side" / "right side") from Fair upward,
   shown in the label. **While aligning, the only thing spoken is "Excellent
   alignment." on arrival** — `AlignmentAnnouncer` in its default
   `Mode::arrivalOnly`. That was a user decision: intermediate announcements
   talked over the tone they steer by. Calibration progress and results are
   still always spoken. Suppressed entirely while a calibration sweep runs.
2. **Continuous pitched sine** ("sonification") whose frequency rises
   exponentially as |delay| → 0: 200 Hz at full deflection, 2000 Hz at
   perfect alignment, equal deflection deltas = equal musical intervals.
   The user turns the screw and listens for the pitch to peak.
3. **Stereo direction cue + pulsing low tone**. The tone is constant-power
   panned by the *sign* of the delay (positive → right ear, negative →
   left), so as the user approaches alignment the tone migrates toward
   centre. When |delay| exceeds half-deflection (≈25 µs by default) a
   raised-cosine pulse modulator at ~3 Hz engages on top, the "you've
   rotated well past the lock point" cue. The carrier never goes fully
   silent during the pulse, so this state is unambiguously distinct from
   the signal-absent silence.

The plugin **always** replaces the cassette signal with the guidance tone —
there is no in-plugin on/off toggle for sonification. To silence the tone,
bypass the FX in the host: that achieves the same thing without making the
plugin a no-op while loaded. There is no "mix with tape" mode either —
during alignment the user monitors the deck output directly (or via an
armed Reaper track), so mixing the tone into the same signal they already
hear externally would just be a redundant echo.

## Build

CMake is the single source of truth (`cmake_minimum_required(VERSION 3.22)`;
the dev machine runs 4.4.2 from `C:\Program Files\CMake`). JUCE and Catch2 are
resolved by `azimuter_resolve_sdk()` in this order at configure time:

1. **Environment variable** — `JUCE_SDK_DIR` / `CATCH2_SDK_DIR`. Per-developer
   override; useful when testing against a different tag.
2. **Known install locations, first hit wins** — `C:\_tools\JUCE\` (the current
   install, JUCE 8.0.15), then `C:\sdk\JUCE\` and `C:\sdk\JUCE-8.0.4\` from this
   machine's previous layout. Catch2 resolves the same way (`C:\_tools\Catch2\`,
   `C:\sdk\Catch2\`, `C:\sdk\Catch2-3.5.4\`). SDKs live outside the project tree
   so multiple plugin projects share one copy instead of each downloading
   hundreds of MB into its own `build/_deps/`.
3. **FetchContent fallback** — if none is present (CI, fresh checkout on
   another machine), CMake clones JUCE 8.0.15 and Catch2 v3.5.4 into
   `build/_deps/`. Keep that tag equal to the local JUCE version.

**Catch2 is not currently installed locally**, so a configure with tests
enabled falls through to FetchContent and clones it on first run. Only JUCE
resolves locally today. If a configure unexpectedly starts cloning JUCE, the
search list above has gone stale again — that is the symptom to look for,
because the fallback is an unpatched tree (see the JUCE patch below).

Moving an SDK invalidates `build/CMakeCache.txt`, which stores the resolved
absolute path. Delete `build/` after a move; the stale cache otherwise keeps
pointing at a directory that no longer exists.

Whichever path resolves, the configure log prints it, e.g.
`Azimuter: using local JUCE at C:/sdk/JUCE`. To bump a JUCE version
machine-wide, drop the new release into `C:\sdk\JUCE\` (overwriting in
place), or set `JUCE_SDK_DIR` to override per-run.

Note: `C:\sdk\JUCE\` ships with `Projucer.exe` and `DemoRunner.exe` next to
the source. Those are auxiliary dev tools — there is no separately
"precompiled JUCE library". JUCE is always compiled from source into the
plugin build tree by `add_subdirectory(...)`.

The non-ASCII `č` in `COMPANY_NAME` needs handling on two fronts. MSVC gets
`/utf-8` in `CMakeLists.txt` — without it the compiler reads UTF-8 sources as
the active Windows code page and JUCE's generated `PluginDefines` header
carries a mojibake'd manufacturer string. The VST3 per-class Unicode path
needs the JUCE patch below.

**After upgrading *or moving* JUCE, re-run `patches\apply_juce_patches.bat`.**
The shared JUCE tree is not version-controlled, so overwriting it with a new
release — or installing it somewhere new — leaves Azimuter's local patches
unapplied. `juce_vst3_unicode_vendor.ps1` resolves the JUCE tree with the same
env-var-then-known-locations search CMake uses (keep the two lists in sync), or
takes an explicit path as its first argument. Currently there is one — a UTF-8 → UTF-16 fix
in `juce_VST3ModuleInfo.h` that makes the č in "Hermis Kasperavičius" survive
the per-class Unicode info path, so Reaper doesn't show mojibake. The script
is idempotent: re-running it on an already-patched tree is a no-op, and it
errors out clearly if JUCE's source has changed shape enough that the patch
needs a manual review.

That patch fixes what the *DLL exposes*. It does not fix `moduleinfo.json`,
which is written afterwards by a helper that loads the finished DLL, reads the
vendor back and re-encodes it — turning the č into a Windows-1257 byte in
`Factory Info` and into U+FFFD in the class entries, leaving a file that is not
even valid UTF-8. Hosts read that file during scanning precisely so they don't
have to load the DLL, and `Azimuter.iss` ships it inside the bundle, so the
mojibake surfaces anyway. `patches/fix_moduleinfo_vendor.cmake` runs as a
POST_BUILD step on `Azimuter_VST3` and substitutes the correct bytes back in.
It lives outside JUCE deliberately, so it survives SDK upgrades and moves.

Two things about that step are load-bearing:

- `AZIMUTER_COMPANY_NAME` in `CMakeLists.txt` is the single source of truth for
  the name, and it reaches the script **hex-encoded** via `string(HEX)`. Passing
  the raw name on a command line would run it through the same MSBuild
  response-file encoding that causes the bug.
- `COPY_PLUGIN_AFTER_BUILD` is **not** passed to `juce_add_plugin`. That would
  register the copy step before the repair, and the installed bundle would carry
  the broken file. `juce_enable_copy_plugin_step(Azimuter)` is called after the
  repair instead — POST_BUILD commands run in registration order.

To verify after a build:

```powershell
python -c "raw=open(r'build/Azimuter_artefacts/Release/VST3/Azimuter.vst3/Contents/Resources/moduleinfo.json','rb').read(); raw.decode('utf-8'); print(raw.count(b'Kasperavi\xc4\x8dius'))"
```

(3 is correct — one factory field plus one per exported class. Note the file is
JSON5, not strict JSON: it has trailing commas, so `json.loads` rejects it and
that is not a fault.)

### Toolchain

**Visual Studio 2026 Community 18.6+** at
`C:\Program Files\Microsoft Visual Studio\18\Community\`. `vswhere.exe`
detects it, CMake 4.3.2's default generator (`Visual Studio 18 2026`) picks
it up automatically, and no developer-environment shim is needed:

```powershell
# Configure (default generator = Visual Studio 18 2026, multi-config)
cmake -S . -B build

# Build a specific config (multi-config: pick at build time, not configure)
cmake --build build --config RelWithDebInfo
cmake --build build --config Release
cmake --build build --config Debug

# Build just one target
cmake --build build --target Azimuter_VST3 --config RelWithDebInfo
cmake --build build --target AzimuterTests --config RelWithDebInfo
```

If you ever explicitly need NMake or Ninja, the toolchain is on PATH via the
VS install — `vcvarsall.bat` lives at
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat`,
but the default VS generator is fine for everything we need.

### Wrapper scripts

Double-clickable `.bat` wrappers cover the common flows. Each configures on
first run, builds, then runs CTest — so a green run means the tests passed too:

- `build.bat` — `RelWithDebInfo` + tests. The day-to-day loop.
- `build_release.bat` — same for `Release`. Use when cutting a real release.
- `build_installer.bat` — wraps Inno Setup's `ISCC.exe` over `Azimuter.iss` to
  produce `dist\Azimuter-<version>-Setup.exe`. It does **not** rebuild the
  plugin; run `build_release.bat` first. Inno Setup 6+ is a separate one-time
  install, not part of the CMake/VS toolchain.

`Azimuter.iss` installs the bundle system-wide to `%CommonProgramFiles%\VST3`
(admin) and drops `readme.txt` + the uninstaller in `%ProgramFiles%\Azimuter\`
— deliberately *outside* the `.vst3` bundle, whose layout the host parses. Its
`AppId` GUID must never change once published, or upgrades install a second
copy side by side. The wizard uses stock Win32 controls because those are
screen-reader navigable; don't switch to custom-drawn pages.

The version string lives in three places and they must be bumped together:
`project(... VERSION 1.1)` in `CMakeLists.txt` (which feeds
`JucePlugin_VersionString` and the DLL's version resource), `MyAppVersion "1.1"`
in `Azimuter.iss`, and `Version: 1.1` in `readme.txt`. All three are currently
1.1. A mismatch is user-visible: the installer, the manual it installs and the
plugin's own version info would disagree.

### GitHub and CI

The repo is public at `github.com/hermis501/azimuter`. `.gitignore` keeps out
`build/`, `dist/`, `examples/` (the tape transfers), all audio files,
`raport.txt` and the `claude_*.bat` launchers. `README.md` is the GitHub front
page for developers; `readme.txt` stays the end-user manual that ships.

`.github/workflows/build.yml` builds and tests on `windows-latest` and
`macos-latest` (universal arm64 + x86_64, Xcode generator, deployment target
10.13). It clones JUCE at `JUCE_TAG` and applies
`juce_vst3_unicode_vendor.ps1` with an explicit path, then checks
moduleinfo.json for the vendor string. Pushing a tag `v*` publishes a
pre-release with the Inno Setup installer, a Windows zip and a macOS zip. The
macOS bundle is only ad-hoc signed, not notarised. Bump `JUCE_TAG` together
with the local JUCE.

### Release build (for shipping)

The VS generator is multi-config, so `Release` and `RelWithDebInfo` coexist
in the same `build/` tree. To produce a shippable bundle:

```powershell
cmake --build build --config Release
```

What you get under `build\Azimuter_artefacts\Release\VST3\Azimuter.vst3\`:

- No `.pdb` or `.ilk` files in the bundle (full optimisation, no debug info).
- Only the two distributable files inside the bundle —
  `Contents\x86_64-win\Azimuter.vst3` DLL and
  `Contents\Resources\moduleinfo.json` — ready to zip and ship.

Don't ship the plugin until it has been more carefully tested. Day-to-day
iteration uses the `RelWithDebInfo` config for debuggable symbols.

### CMake options

Pass with `-DNAME=VALUE` at configure time:

- `AZIMUTER_BUILD_TESTS` (default `ON`) — build the Catch2 test executable.
- `AZIMUTER_COPY_PLUGIN_AFTER_BUILD` (default `OFF`) — auto-install the VST3
  into the user VST3 folder after each build. Useful for live Reaper iteration.

### Where the artefacts land

```
build/
├── AzimuterTests.exe                                              # test binary
└── Azimuter_artefacts/RelWithDebInfo/VST3/Azimuter.vst3/          # plugin bundle (folder)
    └── Contents/x86_64-win/Azimuter.vst3                          # the actual VST3 DLL
```

Point Reaper at `build\Azimuter_artefacts\RelWithDebInfo\VST3` (Options →
Preferences → Plug-ins → VST → VST plug-in paths), or set
`AZIMUTER_COPY_PLUGIN_AFTER_BUILD=ON`. Copy the **entire `Azimuter.vst3`
folder** (the bundle), not just the inner DLL, when installing manually.

## Test

```powershell
# All tests via CTest — pass the same -C config that you built with
ctest --test-dir build --output-on-failure -C RelWithDebInfo

# Run the binary directly with Catch2 filters
build\RelWithDebInfo\AzimuterTests.exe
build\RelWithDebInfo\AzimuterTests.exe "[gcc-phat]"               # tag filter
build\RelWithDebInfo\AzimuterTests.exe "[sonifier]"
build\RelWithDebInfo\AzimuterTests.exe "Identical channels*"      # name filter
build\RelWithDebInfo\AzimuterTests.exe --list-tests
```

**Rule:** tests link `azimuter_core` only, never the JUCE plugin classes. When
you add new DSP, put the pure logic in `Source/Azimuth/`, add it to
`azimuter_core` in `CMakeLists.txt`, and write a test that exercises it
without instantiating an `AudioProcessor`.

## Architecture

The codebase is split into two layers so the DSP can be tested without
spinning up a plugin host:

```
Source/
├── Azimuth/                # Plugin-agnostic DSP. Belongs to azimuter_core.
│   ├── AlignmentReadout.h  # Quality buckets + when to speak them.
│   ├── AlignmentReadout.cpp
│   ├── AnalysisSnapshot.h  # Lock-free frame hand-off, audio → message thread.
│   ├── AzimuthCalibrator.h # Fits the tape's target delay from a sweep.
│   ├── AzimuthCalibrator.cpp
│   ├── DeflectionMap.h     # µs → signed normalised deflection (header-only).
│   ├── GccPhat.h           # Delay estimator: coarse wide-band lock, then
│   ├── GccPhat.cpp         #   HF refinement. Also feeds SpectralTilt and
│   │                       #   computes signal significance.
│   ├── SignalGate.h        # Silence vs tone, from significance (header-only).
│   ├── SpectralTilt.h      # Third-octave azimuth-loss observable.
│   ├── SpectralTilt.cpp
│   ├── Sonifier.h          # Pitched sine + stereo direction cue + pulse,
│   └── Sonifier.cpp        #   all driven by signed normalised delay.
├── PluginProcessor.{h,cpp} # juce::AudioProcessor wrapper, parameter tree,
│                           # RT-safe analysis + sonification loop.
├── PluginEditor.{h,cpp}    # juce::AudioProcessorEditor, accessibility
│                           # wiring, screen-reader announcements.
└── ScreenReaderSpeech.{h,cpp} # Speech via OSARA / UIA notification, not
                               # JUCE's private SAPI voice.

Tests/
├── AlignmentReadoutTests.cpp  # Bucket boundaries, median settling, dwell.
├── AzimuthCalibratorTests.cpp # Model recovery, gates, and real-tape fixtures.
├── DeflectionMapTests.cpp     # Fine/coarse breakpoints, clamping, symmetry.
├── GccPhatTests.cpp           # Catch2 v3, links azimuter_core only.
├── SignalGateTests.cpp        # Hysteresis and hold times of the silence gate.
├── SonifierTests.cpp          # Pitch / pulse / pan map, gate, buffer overwrite.
├── SpectralTiltTests.cpp      # Band layout, level independence, averaging.
└── data/                      # Real DR-1 azimuth sweeps, as calibration
                               # regressions. AZIMUTER_TEST_DATA_DIR points
                               # here. The export script and the source
                               # audio are not in the repo; regenerate from
                               # the transfers if the analysis chain changes.
```

CMake targets mirror this:

- **`azimuter_core`** — static library, contents of `Source/Azimuth/`. No JUCE
  dependency. Linked by both the plugin and the tests.
- **`Azimuter`** — the JUCE plugin (`juce_add_plugin`, VST3 format only).
  Depends on `azimuter_core` + `juce::juce_audio_utils` + `juce::juce_dsp`.
- **`AzimuterTests`** — Catch2 executable. Depends on `azimuter_core` +
  `Catch2::Catch2WithMain`. Discovered by CTest via `catch_discover_tests`.

### Parameters

Three, declared in `createParameterLayout()` (`PluginProcessor.cpp`), held in an
`AudioProcessorValueTreeState` and persisted as XML by `getStateInformation`:

| ID | Name | Range (skew) | Default |
|----|------|--------------|---------|
| `cutoff` | Analysis Cutoff | 1000–16000 Hz (0.5) | 6000 |
| `smoothing` | Smoothing | 0.01–2 s (0.4) | 0.25 |
| `sonifyGain` | Tone Level | 0–1 | 0.4 |

All three are read per block. `cutoff` and `smoothing` reach the analyser
through `GccPhat::setBandLowHz` / `setSmoothingSeconds`, which only recompute
scalars — `binLow` and the one-pole coefficient — and so are audio-thread safe.
`prepare()` remains the only place that sizes buffers; `analyse()` and both
live setters must stay allocation-free. (Until 2026-08 these two were baked in
at `prepareToPlay` and the sliders did nothing until the host re-prepared the
plugin. `smoothing` in particular is the user's only control over reading
wobble, which measures ~6 µs at the 0.25 s default — wider than the "Good
alignment" bucket — so leaving it inert was a real accessibility bug.)

The upper PHAT band edge is not a parameter: `prepareToPlay` fixes it at
`min(sampleRate * 0.45, 18000)` Hz.

### No absolute level thresholds

Every "is there anything to measure?" decision runs on
`GccPhat::signalSignificance()`, never on signal level. It is the coherence of
the averaged unit phasors over the coarse 1–18 kHz band, divided by the noise
floor of that averaging (`kNoiseFloorConstant` × √(Σw²)), so **1.0 means the two
channels share nothing**. Unit phasors make it level-independent; the
normalisation makes it independent of averaging. It has its own fixed 0.25 s
averaging, not the user's Smoothing.

Measured, not assumed: on uncorrelated noise it reads 1.02 at every smoothing
setting; shared music reads ~1.5–4; it survives a badly tilted head because the
1–6 kHz part of the coarse band keeps it up. **Don't move it to the fine band**:
far off azimuth the treble collapses, and a fine-band gate went quiet — and
rejected sweep edges — exactly when guidance mattered most. Don't move it to
1–4 kHz either: some dubbed tapes carry shared low-frequency noise in their
gaps, which kept a 1–4 kHz gate open with random delay readings.

Three consumers:

- **`SignalGate`** — the sonifier's silence state. On at ≥ 1.30 held 0.2 s,
  off below 1.15 held 1 s. Replaced `signalLevel() > 1e-3` (−60 dBFS), which
  could silence a quiet deck mid-song while letting the gaps through with the
  tone wandering at random. On the full example transfers: silent for
  0.00–0.04 % of music time, on for 2–8 % of gap time. Cost: ~1 s latency at
  each edge.
- **`AzimuthCalibrator::kMinSignificance`** (1.3) — per-observation gate.
  Replaced a raw phase-coherence gate of 0.15, which sat *below* the noise floor
  and accepted 100 % of gap samples.
- **`AzimuthCalibrator::kMaxLevelDropDb`** (25 dB) — at solve time, drops
  observations far below the sweep's *own median* level (fade-outs). Relative,
  so deck output level is irrelevant. Replaced an absolute −55 dB floor.

The fixtures in `Tests/data/` carry a `significance` column computed the same
way. After this change the jungenu calibration is unchanged (+49.6 µs) and
brunius moved 2 µs (−98.5 → −96.5), within its own ±4 µs uncertainty.

`azimuter::DeflectionMap` owns the µs → deflection mapping and is the single
source of truth for both the sonifier's full deflection and the readout's
"Off-scale" bucket (`AzimuterAudioProcessor::kFullScaleDelayMicroseconds`
aliases it). The map is deliberately **piecewise**, because the two cues it
feeds have different useful ranges: `pitchFloorMicroseconds` (25 µs) is where
the pitch glide bottoms out and the pulse begins, and `fullScaleMicroseconds`
(100 µs) is where the pulse saturates. Widening full scale therefore extends
the coarse range without diluting fine resolution — a plain linear map cannot
do both, and the earlier 50 µs linear version saturated inside the 55–75 µs
azimuth drift measured across one C-90 side, making 50 µs and 90 µs sound
identical. If you change either constant, `DeflectionMapTests.cpp` pins the
breakpoints and `readme.txt` quotes both numbers.

### Audio thread / UI thread boundary

`AzimuterAudioProcessor::processBlock` runs the analyser and sonifier on the
audio thread and publishes the latest signed delay (µs) through a
`std::atomic<float>`. The editor polls that atomic from a `juce::Timer`
(~20 Hz) and updates the visible/spoken readout.

`AzimuthCalibrator` is the exception to that pattern and is **message-thread
only** — it allocates and runs a least-squares grid search. The audio thread
publishes complete analysis frames through `azimuter::AnalysisSnapshot` (a small
lock-free ring; a torn read that mixed two frames would silently corrupt an
observation), and the editor's timer feeds them in via
`AzimuterAudioProcessor::pushCalibrationSample`. Never call anything on the
calibrator from `processBlock`.

Hard constraints on the audio thread (this includes everything else
`azimuter_core` exposes):

- No allocation, locks, logging, or filesystem access.
- No JUCE GUI calls.
- The sonifier owns its own carrier + pulse phase state and writes the buffer
  in-place via `getWritePointer` on every block. The plugin's output is always
  the guidance tone, never the cassette signal; silencing the tone is the
  host's job (FX bypass), not the plugin's.

## Accessibility constraints (read before touching the editor)

Because the plugin is the user's only sense of azimuth alignment, UI rules
are stricter than usual:

- Every interactive component must call `setTitle`, `setDescription`,
  `setHelpText`, and `setAccessible(true)` so it surfaces correctly to
  NVDA / Narrator / VoiceOver. Use the `configureAccessibleSlider` /
  `configureAccessibleLabel` helpers in the anonymous namespace at the top of
  `PluginEditor.cpp` — add a helper there rather than hand-rolling the four
  calls per control.
- NVDA reads only the accessibility **name** (`setTitle`); it ignores
  `setDescription` and `setHelpText`. The title must therefore be
  self-sufficient on its own — the helpers mirror it into all three fields only
  because JAWS sometimes reads description.
- **All speech goes through `ScreenReaderSpeech`** (`Source/ScreenReaderSpeech.{h,cpp}`),
  never `juce::AccessibilityHandler::postAnnouncement` directly. On Windows
  JUCE implements `postAnnouncement` with its *own SAPI voice*: a second
  synthesiser that ignores the user's screen-reader voice, rate, volume and
  stop-speech key. `ScreenReaderSpeech::speak` tries, in order: OSARA's
  `osara_outputMessage` (looked up through REAPER's `IReaperHostApplication`
  on the VST3 host context, via `VST3ClientExtensions::setIHostApplication`
  on the processor); then a UIA notification event raised on the editor's
  JUCE accessibility element (what OSARA itself uses); and only then JUCE's
  SAPI path. The UIA route casts JUCE's opaque `AccessibilityNativeHandle*` to
  `IRawElementProviderSimple*`, valid because that is its first COM base in
  `juce_AccessibilityElement_windows.h` — recheck after a JUCE upgrade.
- Speech fires **only** when the announcer says so. Never on every poll — it
  drowns the user.
- **The spoken quality must stay quiet unless something real has changed.**
  This took two attempts, and the history matters. The original scale (0.5 / 2 /
  5 / 15 µs) put three boundaries inside the reading's own wobble — ~6 µs at
  the default 0.25 s smoothing, ~2 µs at 2 s — and that wobble is
  *autocorrelated*, so it looks like real slow movement. A median plus a short
  hold was not enough: users heard "Excellent" begin and be cut off by the next
  announcement, constantly. Two fixes were needed together:
  1. **The bucket scale** (`kExcellentUs` etc. in `AlignmentReadout.h`) is set
     by what the error costs in treble, measured on a real sweep: Excellent
     < 5 µs (~0.02 dB), Good < 15 µs (< 0.2 dB), Fair < 30 µs (< 0.7 dB), Poor
     < 60 µs (< 3 dB). Excellent and Good are both wider than the wobble.
  2. **`AlignmentAnnouncer`**: ~1 s median; hysteresis (getting better uses the
     nominal edge, getting worse must clear `edge × 1.5 + 0.5 µs`); a hold of
     ~0.75 s for improvements but ~2 s for worsening (turning away is
     sustained, noise is not); and a hard **3 s minimum** between
     announcements. The side is only named from Fair upward.
  `AlignmentReadoutTests.cpp` simulates the reading as autocorrelated noise
  (white noise through the analyser's one-pole smoother) — independent samples
  would make the announcer look far better than it is. Holding still at the
  optimum at default smoothing: at most 2 announcements in two minutes across
  50 seeds (was 22). If you touch any of these constants, rerun those tests and
  mirror the scale in `readme.txt`.
- **Arrival-only speech.** `Mode::arrivalOnly` (the default, and what the
  editor uses) speaks only when the settled bucket becomes Excellent, and only
  again after it has left Excellent through the same hysteresis + 2 s hold and
  come back. The text is just `"Excellent alignment."` — no number, since
  inside 5 µs it is mostly meter wobble. `Mode::everyChange` keeps the full
  behaviour; most of `AlignmentReadoutTests.cpp` pins that mode deliberately,
  because it exercises the hysteresis engine both modes share. Don't
  reintroduce intermediate announcements without asking: the user removed them
  on purpose.
- An earlier version posted Excellent at SAPI `high` priority, which is part
  of why speech kept being interrupted. The 3 s gap means nothing needs to
  barge in. Note OSARA's own messages do interrupt current speech, as all
  OSARA output does.
- Do **not** "fix" that to `notifyAccessibilityEvent(valueChanged)`: NVDA does
  not announce value changes on a `juce::Label`, because static-text elements
  don't surface them the way sliders do. Explicit speech through
  `ScreenReaderSpeech` is what reaches NVDA, JAWS and Narrator.
- Text values must be self-describing when read aloud
  (`"Delay: -2.3 us (Good alignment, left side)"`), not icons or colour.
- The editor must remain fully keyboard-navigable. Keep
  `setWantsKeyboardFocus(true)` on the editor and on every focusable control.
  `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` is set on `juce_add_plugin` so Reaper
  passes keys through.

When you add a new measurement or control, also extend the spoken-text
bucket logic (`delayAsSpokenText` or a new equivalent) and consider whether
the sonifier should reflect the new state too. Blind users should never have
to mentally translate raw floats. The sonifier's three audible states
(silence / pulsing low tone / pitched glide) are part of the accessibility
contract — don't collapse them, and don't add a fourth state without making
sure it's unambiguous against the existing three. The stereo-pan direction
cue is layered *into* those states rather than being a fourth state: it
moves the existing pitched-glide / pulsing-tone left or right with the sign
of the delay, without changing the timbre that distinguishes them.

`readme.txt` is the end-user manual and ships inside the installer. It spells
out the exact bucket thresholds, the three audio cues, what each slider does,
and the troubleshooting steps — so any change to those behaviours has to be
mirrored there. A stale manual is an accessibility bug: it is the only written
description of cues the user can otherwise only hear.

## Platform notes

- Primary target: Windows 10/11 with MSVC. The project also configures
  cleanly on macOS/Linux but only VST3 is built (no AU/AAX/LV2).
- User VST3 install path on Windows:
  `%CommonProgramFiles%\VST3` (system, needs admin) or
  `%LOCALAPPDATA%\Programs\Common\VST3` (user).
  `AZIMUTER_COPY_PLUGIN_AFTER_BUILD=ON` uses JUCE's default per-user location.
- Reaper picks up rebuilt bundles on next launch, or via Preferences →
  Plug-ins → VST → Re-scan.

## Style

Formatting is enforced by `.clang-format` (LLVM base, Allman braces, 120-col,
4-space indent). Run `clang-format -i` over any changed `.cpp` / `.h` before
committing. There is no separate linter configured; rely on MSVC `/W4` and
JUCE's recommended warning flags, both enabled in `CMakeLists.txt`.
