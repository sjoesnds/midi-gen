# MIDI Forge corpus-layer findings — 2026-10-17

## Scope

This is a diagnostic-only investigation for MIDI Forge **0.109.0**. It updates the corpus comparison after the source-method script, anonymized per-track aggregates and full-precision target medians/IQRs were provided. No production MIDI-generation or selection behavior is changed.

The audit contains 64,000 baseline candidate rows before final selection and 512 selected-bank rows (eight per seed), with per-layer note counts, existing quality, and interval/density metrics. Generated layers use approximate pitch bands (top >= 60, riff 48–59, bass < 48), not semantic source tracks.

## Source definition recovered

The supplied method defines the following measurements:

- A layer is omitted from metric calculation when it has fewer than five raw notes.
- Contour metrics use the highest pitch within the layer at each rounded onset. Consecutive onset pitches form the interval sequence; `steps_le2` and `leaps_ge5` use absolute semitone distances.
- `dur_beats` is the median of all note durations divided by ticks per beat.
- `notes_per_bar` is the median raw-note count over bars from bar zero to the final occupied bar, including zero-count gaps and excluding trailing empty bars.
- `window_repeat` uses every sliding window of six consecutive intervals, stride one. Each interval is clipped to [-12, 12], window tuples are compared exactly, repeats are the sum of excess copies, and the result is divided by max(1, window count). With at least five raw notes, the source returns zero when no complete window exists; it does not mark a one-window sample as NA.

The source script's comment mentions a +/-1 semitone tolerance, but the code does not implement fuzzy matching: after converting pitches to integer semitone intervals and clipping, window tuples are compared exactly.

## Target data validated

- The 36 anonymized per-track aggregate summaries are stored in `data/corpus_per_track_stats.json`; source MIDI notes and track names/text are not included.
- `data/corpus_targets.json` now contains the supplied medians and IQRs for all five metrics, including `dur_beats`.
- Aggregation uses only tracks with at least 50 notes in the corresponding pitch band. The medians and exclusive quartiles reproduce the stored target summaries within 0.0011 absolute error (the per-track values were rounded to three decimals).
- The new CI check `tools/ablation/validate_corpus_targets.py` checks all 15 layer/metric summaries and reports the eligible counts: top=31, riff=31, bass=33.

The validation confirms the aggregate targets against the anonymized per-track summary; because the original MIDI notes are not included, it does not independently regenerate every source track's metrics from note events.

## Important correction to the prior candidate report

The previous 64-seed audit used a **non-overlapping** six-interval `window_repeat` implementation and marked samples with fewer than two windows as NA. That was not the supplied source definition. Those old repeat-rate counts and any joint target-match conclusions that depended on them must be treated as historical and must not be used for musical decisions.

The diagnostic helper now uses sliding six-interval windows and source-compatible zero semantics. It also skips metrics for pitch bands with fewer than five notes, computes notes-per-bar only through the final occupied bar, and exposes the source-IQR match fraction only among candidates that meet the 50-note-per-layer eligibility gate. CI will regenerate the deterministic 64-seed artifacts from this corrected measurement implementation.

## Decision

**Do not change production generation, the production selector, judge weights, Taste ML, golden MIDI, UI, or project version from this update.** The work corrects measurement comparability and strengthens diagnostics. Source tracks are much longer than the generator's four-bar candidates, and pitch bands are only rough proxies for source parts, so a residual statistical mismatch is not by itself a musical defect.

Any future musical experiment remains gated on reproducible output, tonal-safety fraction = 1.000, grid fraction = 1.000, no golden-test regressions, improvement in comparable source metrics, and no degradation in bottom-10% bank quality.
