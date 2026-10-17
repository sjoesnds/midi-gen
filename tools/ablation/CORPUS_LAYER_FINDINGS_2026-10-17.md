# MIDI Forge corpus-layer findings — 2026-10-17

## Scope

This note records a diagnostic-only investigation for MIDI Forge **0.109.0**, using the 64-seed, fixed-seed candidate audit on commit `951866da7bf5e6fc121638e82cb769047ad2fc7f`. It does not describe a production audio/MIDI change.

The audit records 64,000 baseline candidate rows before the final selector and 512 selected-bank rows (eight per seed), with per-layer note counts, the existing quality value, and interval/density metrics. It uses approximate pitch bands (`top >= 60`, `riff 48–59`, `bass < 48`), not semantic source tracks.

## Key observations

- The target file contains aggregate summaries for 36 tracks, but the repo does **not** contain the original source-corpus MIDI files or the calculator/formula that produced `window_repeat`. A public web search for the metric labels and formula did not identify a source implementation. The target formula therefore remains unverified.
- In the current helper, `window_repeat` means exact duplicate non-overlapping six-interval windows, clipped to [-12, 12], with repeats defined as excess copies divided by the number of windows. It is now marked `NA` unless at least two complete windows exist.
- The diagnostic definition is highly sensitive to window policy. Across the candidate pool:
  - Top: original definition has 9 nonzero observations among 55,692 valid candidates (0.016%); six-interval sliding+clipped has 223 / 62,770 (0.355%); four-interval sliding+clipped has 2,501 / 63,343 (3.948%).
  - Riff: original definition has 3 nonzero observations among 7,863 valid candidates (0.038%); four-interval sliding+clipped has 7,192 / 28,237 (25.470%), with P90 = 0.2.
  - Bass: the pool has no candidates with at least 50 bass-band notes. For the original definition, only 3,685 / 64,000 candidate samples are valid (5.758%); all observed values are zero.
- The four-interval sliding definition naturally counts overlapping sub-windows, so it can return more repeated sequences than non-overlapping windows. These rates must **not** be judged against the supplied corpus IQR until the source definition is recovered and matched.
- Under the existing current definition, none of the 64,000 pool candidates matched all supplied IQRs for its rough pitch band. This is only a diagnostic comparison, not yet evidence that generated MIDI is inconsistent with source tracks: the definition of the metric and the layer mapping differ or may be incomparable.
- An offline top-8 rerank that selects by the existing quality value among candidates matching the top-layer `steps_le2`, `leaps_ge5`, and `notes_per_bar` ranges did not meet the current quality guardrail. Median per-seed bank quality P10 changed from 3.937112 to 3.812715; only 5 of 64 seeds avoided a lower-tail regression. It also cannot solve a missing repeat metric by itself.

## Decision

**Do not change production generation, selection, judge weights, Taste ML, golden MIDI, UI, or project version on these statistics alone.** The selection-only probe fails the quality guardrail, and the exact corpus metric definition is unavailable.

## Next valid step

Recover the source-calculator code or the track-level corpus export that produced the aggregate medians/IQRs. Then implement the same measurement against both source data and generated MIDI, including the same track splitting, track-length handling, onset policy, interval representation, window size/stride, clipping, denominator, and short-track policy. Only after this reproduces the supplied aggregate statistics should a musical experiment be proposed.

Any later candidate-generation experiment must remain isolated and pass all existing release gates: deterministic output, tonal-safety fraction = 1.000, grid fraction = 1.000, no golden-test regressions, target-metric improvement, and no degradation of the bank's bottom-10% quality guardrail.

## Reproducibility

The full machine-readable sensitivity report is published as `corpus-candidate-selection-analysis.json` in the `corpus-layer-ablation` GitHub Actions artifact for the run associated with commit `951866da7bf5e6fc121638e82cb769047ad2fc7f`. Analyzer entry point: `tools/ablation/analyze_corpus_candidates.py`.
