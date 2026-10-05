# Melody Pipeline Contract

This document defines the active melody-generation ownership rules for the 0.86.x development cycle.

## Core rule

There is one musical author, with a small number of explicitly scoped development/safety stages:

1. **addMelody() is the primary melody author.**
   It chooses the melodic language, rhythm vocabulary, interval language, register journey, complexity class, motif vocabulary, and tonal context.

2. **applyMotifDevelopment() and applyMotifSemantics() are the phrase-development authors.**
   They may reshape a complete phrase because their responsibility is contextual A / A' / B / A'' development. They must remain coherent with the melodic language already selected by addMelody().

3. **Harmony and prosody are constrained corrections, not replacement composers.**
   applyMelodicProsody() assigns note roles, velocity, and sustain shape; it is pitch-neutral.
   applyHarmonicIntelligence() supplies limited chord gravity and anticipation only for structurally important non-chord tones.
   Neither may replace the whole contour with a generic arpeggio language.

4. **applyMelodyFoundation() is safety only.**
   It protects scale, register, and leap contracts. It must not decide the musical idea.

5. **repairLocalMelodyQuality() is repair only.**
   It may fix a severe local transition, but only when the local quality measurably improves.

6. **applyLoopClosure() owns only the loop ending.**
   It may shape the final destination note/gesture, not rewrite the phrase.

7. **Judge/score functions never rewrite MIDI.**
   Quality systems such as Composer Judge, local quality, rhythm quality, character fit, memorability, and creative-range scores evaluate candidates and influence selection.

8. **Candidate flattening is pitch-neutral.**
   Candidate search may vary timing, length, velocity, and other presentation details, but it must not inject an independent random pitch author after the melody has been generated.

9. **Variation transforms happen after candidate selection.**
   transformLoop() is allowed to create explicit final variations. Those transformations are not part of the base melody authoring pipeline.

## Removed anti-patterns

The following patterns are intentionally not part of the active candidate pipeline:

- a second full melody rewrite after addMelody();
- one-per-archetype candidate selection used as a proxy for musical diversity;
- independent random pitch mutations in flatten();
- redundant pleasantness/expression/memory passes that rewrite the melody while a newer stage already owns the same responsibility;
- dead phrase-rewriter functions left in the repository after their call sites are removed.

## Change gate for future work

Before adding a new melody-generation system, answer these questions in code review:

- Which existing stage owns this musical decision today?
- Is the new stage an author, a constrained correction, a safety gate, a transformation, or a judge?
- Does it rewrite pitch that another stage already owns?
- Can its purpose be achieved by improving the existing owner instead?
- What QA assertion proves the new behavior is actually exercised?

If a new system cannot have a unique answer to those questions, it should not be added as another generation layer.
