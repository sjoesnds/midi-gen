# Melody Pipeline Contract

This document defines the active melody-generation ownership rules for the 0.96.x development cycle.

## Core rule

There is one authored melody identity. Musical stages must refine, validate, transform, or rank that identity; they must not create a hidden competing melody.

1. **MelodyIntent + addMelody() own authorship.**
   MelodyIntent defines the coherent language, phrase grammar, motif vocabulary, complexity class, register intent, rhythm language and tonal context. addMelody() realizes that plan as the base MIDI line.

2. **Phrase memory is contextual development, not a second generator.**
   A / A' / B / A'' development may reuse contour and rhythmic gestures from the existing phrase. B may contrast; A' and A'' may remember. The development layer cannot replace the whole idea with an unrelated melody.

3. **Harmony, prosody and sound profiles are constrained context.**
   They may pull notes toward active scale/chord tones, shape articulation, sustain, density and register, or protect instrument-specific contracts. They cannot become independent melody authors.

4. **Foundation / final contract are safety only.**
   Scale validity, MIDI bounds, register ceiling and leap limits are hard invariants. Safety code must not invent a musical style.

5. **Judge / Taste ML only rank.**
   Composer Judge, local quality metrics, Creative Range scoring and Taste ML evaluate candidates. They must not rewrite MIDI or mutate generation controls. Taste ML may change ranking, not authorship.

6. **Variation transforms are explicit post-selection thoughts.**
   The eight final slots are Original, Close, Rhythmic, Contrast, Register, Motif, Experimental and Wildcard. Each transform changes a defined musical dimension while remaining anchored to the selected source idea.

7. **Humanize is outside authored generation.**
   LIVE HUMANIZE is a playback/performance layer. Piano Roll HUMANIZE is an explicit MIDI edit. Neither may silently alter the authored generation used for ranking or deterministic QA.

8. **Candidate flattening is pitch-neutral.**
   Flattening and selection may change presentation/timing details only. They must not inject an independent random pitch author.

9. **Piano Roll edits are user-authored state.**
   Manual edits are stored in the selected variation, participate in undo/redo and REVERT, and are persisted in project state v3.

## Removed anti-patterns

The following patterns are intentionally not part of the active pipeline:

- a second full melody rewrite after addMelody();
- independent random pitch authors hidden inside judges, repairs or flattening;
- using Taste feedback to rewrite Density / Energy / Complexity controls;
- one-per-archetype selection as a proxy for musical diversity;
- cosmetic variations that apply multiple unrelated transforms just to make MIDI different;
- generation-time Humanize that changes the MIDI identity;
- stale phrase rewriters or compatibility layers whose call sites no longer exist.

## Change gate for future work

Before adding any generation-related system, answer:

- Which existing stage owns this musical decision?
- Is the new code authoring, contextual development, correction, safety, transformation, or ranking?
- Does it rewrite pitch another stage already owns?
- Can the change be made inside the existing owner instead?
- Which deterministic QA assertion proves the behavior is exercised?
- Does the change preserve clean MIDI determinism when Humanize is disabled?

If the answer is not unique, the new system should not be added as another melody engine.
