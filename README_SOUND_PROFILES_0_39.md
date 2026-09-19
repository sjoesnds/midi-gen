# MIDI Forge 0.39 - Sound Profiles + Contour

## Sound target (new dropdown next to Era)
Piano / Pluck / Synth Lead / Bell-Mallet / Pad-Strings / Brass.
MAGIC never changes it - you choose the instrument, MAGIC writes for it. Saved in the project.

| Profile | Register | Note length | Dynamics | Density | Max leap | Chords |
|---|---|---|---|---|---|---|
| Piano | normal | slider | full velocity range | normal | 12 | held |
| Pluck | normal | 1-2 steps, staccato | flat | high | 12 | stabs on beat 1 + 3 |
| Synth Lead | normal | legato (fills the gap) | flat | normal | 9 | held |
| Bell / Mallet | +1 octave | 3-6, rings | medium | lower | 12 | held |
| Pad / Strings | -7 semitones | long, fills the gap | flat | sparse (2-3/bar) | 5 | held |
| Brass | -5 semitones | 2-6 | punchy | normal | 7 | stabs on beat 1 + 3 |

## Melody contour (all profiles)
Melodies were angular: the median interval was 8 semitones, 54% of intervals were >= a minor sixth,
34% were an octave or more, only 18% were steps. Cause: octave displacements and forced leaps applied
independently per note. Now the scale-degree path of each bar is planned first, smoothed to the nearest
octave and centred as a unit (repeated bars keep their shape), a big leap survives only as a deliberate
leap (Leap Chance slider) and weak beats prefer thirds over skips.

## Other
- Leap Chance / Melody Length / Pause Chance sliders actually affect generation (0.38); Fill Amount still does nothing.
- Duplicate notes (same layer, step and pitch) are removed.
- State format is backward compatible (older projects load with Piano).
