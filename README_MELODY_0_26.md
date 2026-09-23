# MIDI Forge 0.26 — Melody Engine 3.0

## Changes
- Added deterministic 4-bar phrase grammar: statement (A), variation (A'), contrast/peak (B), return/cadence (A'').
- Added six contour grammars operating in scale degrees, preserving scale safety.
- Added controlled phrase peak/register movement.
- Added phrase-arc judging so the 1000-candidate search rewards rise/contrast/release.
- Preserved Phrase Memory, Humanization, Harmony Intelligence, Magic DNA 2.0 and Taste Learning 2.0.
- Timing remains grid-safe; phrase shaping does not introduce arbitrary off-grid notes.

## QA
Static source checks only. A full JUCE/MSBuild build was not run in this environment.
