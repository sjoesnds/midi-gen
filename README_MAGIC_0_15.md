# MIDI Forge 0.15 — Magic Foundation

## Why this release exists

The previous 0.14 Loop Composer had a critical generation-identity bug: the melody hash did not include the user seed or a per-generation nonce. As a result, pressing GENERATE could rebuild the same musical material indefinitely.

## 0.15 changes

- Added a per-generation `generationNonce`.
- Added `generationSeed`, derived from the user seed + generation nonce.
- Melody identity now includes `generationSeed`.
- Variation-bank RNG now also uses `generationSeed`.
- Renamed the main generation button to `MAGIC 8` as the first UI step toward the Magic workflow.

The user-facing seed remains a reproducibility control: changing it changes the starting search space, while repeated MAGIC presses explore new generations.

## Next Magic milestones

1. Generate many genuinely different complete loop concepts.
2. Extract musical fingerprints and reject near-duplicates.
3. Separate motif/rhythm/harmony generation.
4. Score complete loops rather than individual notes.
5. Add a compact candidate-ranking layer.
6. Later evaluate whether a local ML judge is worth adding.
