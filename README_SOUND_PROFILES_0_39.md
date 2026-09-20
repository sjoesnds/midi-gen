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

# 0.39.1 - fixes from real "bad" files
- Rhythm: 0.38 forced patterns with < 5 hits to slide to the next eligible one, so {0,2,8,10,14} became ~25% of all bars.
  Now the groove is picked uniformly among all qualifying patterns and the pool has 16 new patterns (pickups, late starts).
- Chords: Harmonic/Melodic Minor, Dorian and Phrygian produced augmented/diminished triads (up to 32% of bars).
  Such a degree is replaced by the nearest major/minor triad (melody and bass follow, they share the progression).
- Voicing: adjacent voices a semitone apart are penalised (no more cluster chords such as C4+C#4).
- Melody is one line: no simultaneous notes, no note running into the next (candidate mutations used to create dyads).
- Repeated bars no longer have identical velocities (+-2 jitter).
- Octave: shift is now 6 semitones per step (3/4/5/6 = -6/0/+6/+12) and MAGIC picks octave 3-5 only.
