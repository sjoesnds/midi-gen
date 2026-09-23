# MIDI Forge 0.10 — Phrase Composer

## What changed

The melody engine now composes a four-bar musical sentence instead of treating every bar as an independent random melody.

### Phrase roles
- Bar 1: statement — establishes the motif.
- Bar 2: answer — remembers the opening contour while allowing transposition/variation.
- Bar 3: development/peak — increases tension, controlled movement and climax.
- Bar 4: cadence — prepares and resolves to a stable chord tone.

### Motif memory
- Opening-bar rhythm becomes the phrase's rhythmic DNA.
- Later bars inherit that rhythm with small mutations.
- Opening pitches are referenced by matching rhythmic positions.
- Repetition is rewarded, but exact copying is not forced.

### Expressiveness
- Phrase-level contour and climax.
- Leap recovery: larger jumps tend to resolve in the opposite direction.
- Anti-robotic repetition penalties.
- Controlled tension on weak beats/development bars.
- Stronger pre-cadence and final resolution.
- Peak notes receive extra duration and velocity emphasis.

## Safety / compatibility
No new dependencies or UI changes. Existing Piano Roll, MIDI export, drag-and-drop, Smart Lock, Hook/SoundCloud modes and variation system are preserved.
