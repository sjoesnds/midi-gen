# MIDI Forge 0.8 Melody Engine

The melody generator now uses a phrase-first, context-aware statistical prior rather than independent random note selection.

## Musical priors
- Stepwise and small-interval motion is strongly preferred.
- Larger leaps are selective and concentrated around structural moments.
- Chord tones receive stronger weight on strong beats and phrase endings.
- Notes remain inside the selected scale.
- Rhythm is selected from reusable contemporary phrase patterns, then perturbed by density/pause controls.
- Previous-bar material is reused as contour/rhythm with controlled A/A-prime variation rather than literal copying.
- Odd bars can form a call/response relationship to the preceding phrase.
- Phrase endings favour stable chord tones and longer notes.
- Velocity follows phrase position and beat strength with small human variation.
- SoundCloud mode uses a smaller pitch/rhythm range and longer sustained notes while retaining the same underlying musical model.

## Data basis
The engine does not embed copyrighted songs or copy melodies. Its priors are informed by published symbolic-music research/datasets and contemporary chart-level observations, then implemented as generic rules.
