# MIDI Forge 0.14 — Loop Composer

This version replaces the previous note-by-note phrase scorer with a loop-first melody composer.

## Main change

The melody is now built from:

1. a melodic archetype;
2. a sparse rhythmic pattern with explicit rests;
3. a compact motif;
4. controlled contour and interval changes;
5. A / A' / B / A'' style mutation across a four-bar cell.

The generator explicitly rejects the old small-step alternating contour that produced patterns like `1-2-1-2-1-2`.

## Archetypes

The generator rotates between eight identities: hook/off-grid, sparse anchor, wide contour, syncopated, broken, call/response, late-entry, and very sparse/punctuated.

## Intentional differences from 0.12/0.13

- no Russian-vocal 5-4-3 rule in the default melody engine;
- no forced textbook cadence on every loop;
- explicit rests are part of the rhythm;
- longer notes are used as phrase punctuation;
- strong positions use harmony as gravity, not as a requirement to resolve;
- the generic final-bar melody fill was removed in loop-only mode;
- variation identity is changed structurally, not only by random octave tweaks.

## QA target

Generate at least 20 loops. A successful build should produce obvious differences in rhythm, density, contour, register, and motif identity while remaining scale-safe.
