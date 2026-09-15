# MIDI Forge 0.18 — Magic Composition Engine

This release moves the Loop-only generator from a small archetype bank toward a composition engine.

## What changed

- 16 rhythm identities instead of 8.
- Generation identity now controls the melodic archetype, so repeated GENERATE calls do not reuse the same archetype family.
- 16 motif families instead of 8.
- Motif transformations: reverse, inversion-like degree reflection, rotation and selective lift.
- Phrase-cell identity adds controlled A/A'/B/A'' contrast across longer loops.
- 8 composition profiles change anchor, contrast, register and repetition behaviour without random pitch spam.
- Candidate similarity now compares pitch-class sequence, onset rhythm and interval fingerprint.
- Existing 96-candidate Magic Judge and diversity selection remain in place.
- Existing grid-fix behaviour remains: default rhythms stay on the stable 1/8-note grid, while deliberate syncopation may use 1/16 positions.

## Product direction

Song Mode remains out of the user workflow. The project is focused on producing strong standalone loops.

The goal is not to make the binary larger for its own sake. The additional complexity represents musical decision-making that should make generated loops more distinct, memorable and producer-usable.

## QA note

This archive was assembled from the latest 0.17 Grid Fix source and upgraded to 0.18. A full JUCE build should be run in GitHub Actions because the local environment may not be able to fetch JUCE dependencies.
