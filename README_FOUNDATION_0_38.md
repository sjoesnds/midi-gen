# MIDI Forge 0.38 - Foundation

Goal: fix the *material* (chords, bass, melody) before adding sound profiles.

## Chords
- Root, third and fifth are never dropped (density used to delete chord tones: dyads, single notes, empty bars).
- Voicing chosen by voice leading (closest inversion to the previous chord), span kept <= ~1.5 octaves.
- Chord Density = fullness (7th/9th, doubled root), Voicing Width = open voicing.

## Bass
- Guaranteed root on beat 1 of every bar.
- Bass lane E1..E3 (MIDI 28..52): no more inaudible sub notes (down to MIDI 18).

## Melody
- Loop identity: rhythm / motif / contour grammar chosen once per loop (A) and once per phrase for the contrast bar (B).
  Bars repeat as A A' B A'' instead of being re-rolled every bar.
- Minimum playable density (>= 4 notes per bar, except Sparse Lead / Ambient).
- Slider fixes: Melody Length (sustain), Pause Chance, Leap Chance now actually affect generation.
- Velocity accent randomness capped (+-10).

## Register lanes
- Chords 48..72, melody 62..86, bass 28..52. Octave shifts chords+melody together by at most one octave.

## Judge
- Now scores the whole loop: complete chords, bass anchor, repeating rhythmic hook, note density floor.
- Density targets raised (~4.5-5 notes per bar).

Measured on 300 MAGIC generations, before -> after (headless harness, JUCE 8.0.14, GCC):
melody notes/bar 2.65 -> 4.66 | melody coverage 27% -> 62% | bars with a full triad 42% -> 100% |
bass on beat 1 29% -> 96% (rest = bass layer off) | bass below E1 19% -> 0% |
melody on strong beats = chord tone 56% -> 88% | loops >= 4 bars with repeated rhythm 7% -> 56%.
