# MIDI Forge 0.17 — Magic Judge 2.0

The candidate engine now searches 96 full loop candidates and ranks them using a broader musical judge.

## Judge dimensions
- hook / repetition
- intentional space
- rhythm identity
- motif identity
- contour and controlled leaps
- loop seam quality
- register spread
- controlled surprise
- density fit
- explicit penalty for alternating neighbour-note walking (the old 1-2-1-2 pathology)

## Candidate mutations
Candidate search may now make small structural mutations to melody onsets, register, note length and velocity. These are deterministic per candidate and are intended to create genuinely different musical identities rather than cosmetic octave swaps.

## Selection
The final variation bank uses stronger similarity rejection and profile spreading so the eight selected loops do not collapse into one archetype.

This version is intentionally focused on the Magic search engine rather than adding UI features.
