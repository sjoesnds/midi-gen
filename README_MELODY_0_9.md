# MIDI Forge 0.9 — Expressive Melody Engine

The melody generator now uses a context-aware phrase-first scoring model rather than independent random note selection.

## Musical model

- Four-bar phrase identity with A/A' style rhythmic inheritance.
- Motif continuity from the previous bar.
- Contour planning with a phrase peak.
- Chord-tone attraction on strong beats and cadences.
- Controlled non-chord scale tones in phrase development.
- Stepwise-motion preference and moderated leaps.
- Leap compensation / directional balance.
- Phrase tension and resolution.
- Stable final cadence on root/third.
- Rhythmic DNA with controlled mutation.
- Call/response behaviour through motif inheritance.
- Register coherence.
- Deliberate duration changes and breathing room.
- Phrase-aware velocity.
- SoundCloud keeps a sparse, sustained character.
- No hard-coded source melodies or copied songs.

## Design rule

A generated phrase should have an identifiable idea, repetition, controlled variation, a high point, and a resolution. Randomness is used only as a tie-breaker/variation mechanism after musical constraints have been scored.
