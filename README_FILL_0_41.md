# MIDI Forge 0.41 - Fill Amount

The Fill Amount slider was stored, saved and randomised by MAGIC, but never applied.
Now, at the end of a phrase (4th bar of the cycle, and the last bar of the loop, which leads back to bar 1),
a short scale run of 1-3 sixteenth notes leads into the next bar's chord.

- Amount = how often the fill happens (chance = 2.2 x amount, max 95%) and how long it is
  (1 note; 2 notes above 0.30; 3 notes above 0.60).
- The run ends a scale step away from the next chord's root, starting close to the last melody note (no big jumps).
- Repeated phrase ends decide in the same way (loop identity), so a fill becomes part of the hook.
- Bell / Pad profiles never get fills (they keep their long notes). The melody stays one line: no overlaps.

Measured (notes in steps 13-15 of phrase-end bars): slider 0 -> 0.64 per bar, 0.18 (default) -> 0.92, 0.38 -> 1.66, 0.70 -> 2.24.
Notes per bar, monophony and chord agreement are unchanged.
