# MIDI Forge

**Current version: 0.109.0**

MIDI Forge is a JUCE 8 / C++17 VST3 plugin for generating editable MIDI loops in FL Studio. Current output supports a melody with optional chords and bass. See [CHANGELOG.md](CHANGELOG.md) for release history.

## Current behavior

- Creator-facing composition controls: **Key, Scale and Bars**.
- MAGIC randomizes the root key and the **Major/Minor** mode on each press unless their individual lock icons are engaged; Bars remains fixed.
- MAGIC generates a bank of eight variations; per-control locks preserve values the user wants to keep.
- MAGIC is deterministic for a given saved plugin state, engine version and sequence of MAGIC presses. The persisted state stores the press counter; wall-clock time does not seed MAGIC.
- Generated melody attacks are checked against the eighth-note grid and tonal/register contracts. QA checks support these invariants but do not prove every loop is musically strong.
- Taste ML updates from LIKE/DISLIKE feedback. Its ranking influence builds with repeated ratings; several examples of both preferences help it learn what to favor and avoid.

## Reproducing a feedback entry

The plugin writes local telemetry to `feedback.csv` next to `taste.json`, in the operating system's user-application-data directory under `MidiForge`. Rows contain the engine version and `generation_seed` / `dna_seed` identifiers.

1. Match the row's `engine` version before comparing or attempting to regenerate a result.
2. Restore the FL Studio project/plugin state from the time of the rating. The saved state and selected MIDI are the practical replay checkpoint; keep an exported MIDI too when an exact artifact matters.
3. Use `generation_seed`, `dna_seed`, `slot` and `verdict` to locate and audit the corresponding feedback event.

**Limitation:** a `feedback.csv` row alone is not a complete replay recipe for the current engine. It does not store every generation input (including the root key and MAGIC lock mask/press history), so the two seeds alone cannot guarantee exact reconstruction. A saved project/plugin state or exported MIDI is required for an exact reference.

## Build

Requirements: CMake 3.22+, a C++17 compiler, and internet access for CMake to fetch JUCE 8.0.14. The project builds **VST3 only**.

```powershell
cmake -B build
cmake --build build --config Release
```

## Validation and known limitations

- GitHub Actions runs headless quality checks, sanitizer checks and the Windows VST3 build.
- Real FL Studio validation remains **manual**: long sessions (30+ minutes), drag-and-drop into the Playlist, and saving/reloading a project are not proven by CI alone.
- After a sound-changing build, audition 10–15 MAGIC results with the same instrument and preset, keep 3–5 useful loops, and note feedback freely. No calibration spreadsheet is required.
- Exact reproduction from a CSV row alone is not yet supported; see above.

## Project structure

Current `Source/` files (29 files). Generate the list from the filesystem in PowerShell with:

```powershell
Get-ChildItem -Path Source -File | Sort-Object Name | ForEach-Object { $_.Name }
```

```text
Source/
  ComposerGrammar.cpp
  ComposerGrammar.h
  ComposerJudge.cpp
  ComposerJudge.h
  CreativeRange.cpp
  CreativeRange.h
  LoopClosure.cpp
  LoopClosure.h
  MelodicProsody.cpp
  MelodicProsody.h
  MelodyDecision.cpp
  MelodyDecision.h
  MelodyIntent.cpp
  MelodyIntent.h
  MidiForgeAblation.h
  MidiForgeMidiIO.cpp
  MidiForgeShared.h
  MidiForgeState.cpp
  MidiForgeTaste.cpp
  MotifSemantics.cpp
  MotifSemantics.h
  PluginEditor.cpp
  PluginEditor.h
  PluginProcessor.cpp
  PluginProcessor.h
  RhythmGrammar.cpp
  RhythmGrammar.h
  TasteModel.h
  VariationIntelligence.h
```

## Scope

MIDI generation and MIDI workflow only. No user-facing genre/era controls. Realtime playback changes require their own QA, and `PluginProcessor.cpp` refactoring is deferred until the fixed-seed MIDI export can be shown byte-for-byte identical.

Source: https://github.com/sjoesnds/midi-gen
