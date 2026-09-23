# MIDI Forge 0.43.0 — Musical Quality Judge 1.0

## Goal

Improve candidate selection without replacing the existing Genre, Mood, DNA, Taste,
Phrase, Harmony, Sound Profile, or diversity systems.

## New Judge layer

Each of the 1000 candidates receives a soft whole-loop coherence score based on:

- **Harmony fit** — melody notes are compared with chord tones in the same bar.
  Chord tones are preferred, while non-chord passing/approach tones remain allowed.
- **Leap recovery** — large melodic jumps are rewarded when followed by a smaller
  movement in the opposite direction.
- **Cadence** — the final melodic note receives a preference for a convincing landing,
  especially on a chord tone, with a small bonus for a close approach.
- **Phrase balance** — bar density is checked for empty/overloaded bars and healthy
  variation between phrases.

The combined layer has deliberately limited influence (`+0.16`) so it cannot collapse
the generator into one formula. Existing DNA, Genre, Taste ML, phrase and diversity
scoring remain active.

## Engineering

- Version: 0.43.0
- Candidate pool: 1000
- Final variations: 8
- No change to Drag/OLE path
- No change to Taste model persistence
- No change to Piano Roll data format
- Added `<numeric>` for standard accumulation used by the new judge.

## What this is intended to fix

The previous Judge could select MIDI that was statistically healthy but still felt
disconnected as a complete musical phrase. This layer specifically looks at the
relationship between melody, harmony, phrase ending and melodic recovery.

It is a ranking improvement, not a guarantee of subjective musical quality.
