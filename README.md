# Azimuter

**Accessible cassette deck azimuth alignment, by ear.**

Azimuter is a VST3 plugin that helps you align the playback head azimuth of a
cassette deck. Load it on a stereo track carrying the deck's output, play a
tape, and turn the azimuth screw while you listen to a guidance tone:

- **Pitch** rises as you approach correct azimuth: 200 Hz far off, 2000 Hz when aligned.
- **Stereo position** tells you which way to turn. The tone moves toward the
  centre as you get closer.
- **A pulse** comes in when you are well past the lock point. Its depth tells
  you how far past you are.
- **Silence** means there is nothing to measure, such as a gap between songs
  or hiss only.

It is built **accessibility-first**: its main user is a blind audio engineer.
Every control works from the keyboard and with screen readers. Speech goes
through your own screen reader: through OSARA when it is loaded in REAPER,
otherwise through a UIA notification on Windows and VoiceOver on macOS. While
you turn the screw, the only thing spoken is "Excellent alignment." when you
reach it.

## Why not just aim for zero delay?

A tilted head delays one channel against the other, and it also dulls the high
frequencies in both. Only the treble loss identifies true azimuth. A tape can
carry a delay of its own, printed by a duplicator or inherited from a dub, and
no head angle removes it. On the two tapes Azimuter was developed against,
correct azimuth sat at **+54 µs** and **−99 µs**. Aiming at zero would have
cost 2–3.5 dB and 9–15 dB of the top octave.

So Azimuter measures the inter-channel delay with GCC-PHAT, using a two-stage
coarse/fine lock. A one-time **calibration sweep** per tape then finds the
delay at which treble is greatest. It does this by fitting the physical
gap-loss model across the sweep. After that, every cue is driven by the error
against that per-tape target.

## Download

See [Releases](../../releases):

- **Windows** (64-bit): `Azimuter-<version>-Setup.exe` installs to
  `C:\Program Files\Common Files\VST3`. A plain zip is there too.
- **macOS** (universal, Apple Silicon and Intel): unzip and copy
  `Azimuter.vst3` to `~/Library/Audio/Plug-Ins/VST3`. The build is unsigned.
  If your DAW won't load it, run
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Azimuter.vst3`.
  The macOS build is produced by CI and has not yet been tested on a real Mac.

The full user manual is [`readme.txt`](readme.txt). It covers setup, the
meaning of every cue, calibration, troubleshooting and the technical
background. It is the same file the installer ships.

## Building from source

Requirements: CMake 3.22 or newer, a C++17 compiler (Visual Studio 2022/2026
on Windows, Xcode on macOS), and Git.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

JUCE and Catch2 are fetched automatically on the first configure. You can
point at local copies instead with the `JUCE_SDK_DIR` / `CATCH2_SDK_DIR`
environment variables. The plugin bundle lands in
`build/Azimuter_artefacts/Release/VST3/Azimuter.vst3`.

The company name contains a non-ASCII character (č). To keep it intact in
the VST3 Unicode class info, apply the JUCE patch after fetching or upgrading
JUCE:

```sh
pwsh patches/juce_vst3_unicode_vendor.ps1 <JUCE>/modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h
```

On Windows, `patches\apply_juce_patches.bat` does the same. Other build
helpers:

- `build.bat`: RelWithDebInfo build plus tests.
- `build_release.bat`: Release build plus tests.
- `build_installer.bat`: wraps Inno Setup 6 over `Azimuter.iss` to make the
  Windows installer.

CI (`.github/workflows/build.yml`) builds and tests on Windows and macOS on
every push. Pushing a tag `v*` publishes a release.

## Layout

```
Source/Azimuth/   Plugin-agnostic DSP (azimuter_core, no JUCE): GCC-PHAT,
                  spectral tilt, calibrator, sonifier, readout logic
Source/           JUCE plugin: processor, accessible editor, screen-reader speech
Tests/            Catch2 tests; Tests/data holds real azimuth-sweep fixtures
patches/          JUCE vendor-string patch and moduleinfo.json repair
```

[`CLAUDE.md`](CLAUDE.md) holds the detailed developer notes: design decisions,
measured thresholds, and the accessibility rules any change has to respect.

## License

Azimuter is free software, released under the
[GNU General Public License v3.0 or later](LICENSE). It is built with
[JUCE](https://juce.com), used under JUCE's AGPLv3 licence option. GPLv3 is
compatible with that licence.

## Author

Hermis Kasperavičius, 2026. [hermisk.lt](http://hermisk.lt) ·
hermis.kasperavicius@gmail.com
