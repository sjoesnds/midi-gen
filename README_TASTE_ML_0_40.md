# MIDI Forge 0.40 - Taste ML (real)

The 0.37 README promised an online logistic regression, but the code only compared loops with the running mean of the
liked / disliked ones.  0.40 implements the model for real (`Source/TasteModel.h`).

## Model
- 17 features of the whole loop (melody + chords + bass): density, loudness, dynamics, stepwise / skippy / leapy motion,
  repeated notes, offbeat share, repeating rhythm, pitch colour, range, register, note length, legato, chord fullness,
  bass activity, melody-in-chord.
- Logistic regression p(like) = sigmoid(b + (w + w_sound + w_genre) . z). Global weights + small, strongly regularised
  residuals per Sound target and per Genre. Features are standardised against the current 1000-candidate pool.
- Online SGD. LIKE = 1, DISLIKE = 0 (weight 1). Dragging or exporting a loop = weak LIKE (weight 0.5, once per loop).
- The Judge re-ranks the 1000 candidates with +-0.7 sigma of its own quality spread at full confidence
  (confidence grows over the first ~10 ratings), so the taste guides the search; diversity selection is unchanged.
- Persisted in the existing preferences JSON (`tasteML`), survives restarts.

## UI
- Label: `TASTE +likes / -dislikes  ML xx%  VAR n (score)` and a second line with the strongest learned preference.
- `RESET` forgets everything, `TASTE ML` toggle switches the influence on / off (for A/B listening).

## Measured (simulated hidden user, 15 rounds of LIKE best / DISLIKE worst of 8, then 40 fresh MAGIC presses, 3 repeats)
Utility of the loop MAGIC shows first (sum of 3 preference z-scores):
none ~0.0 | old similarity taste (0.39.1) ~0.7 | Taste ML ~1.9 | Taste ML + drag-as-like ~2.1.
Variety inside one MAGIC press is unchanged (std of utility 1.96 -> 1.94). Still works with 30% / 50% random ratings.
