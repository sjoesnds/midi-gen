# 0.108.0 fixed-seed bank diagnostics

This directory is the measurement baseline for the 0.108.0 generation engine. The report covers fixed seeds **108000–108255**: 256 generated banks, eight selected variations per bank, and 7,168 pairwise comparisons.

## Summary

- **Exact fingerprints:** 8 unique melody fingerprints in every bank (256/256 banks). This only rules out exact duplicates under the test fingerprint; it does not prove that people will hear all eight ideas as distinct.
- **Pairwise idea distance:** mean **0.6265**, median **0.6383**, 10th–90th percentile **0.4708–0.7750**. Distance is calculated as 1 minus the existing `ideaSimilarity` value.
- **Closest pair within a bank:** median similarity **0.6367**; 90th percentile **0.7600**; worst observed pair similarity **0.8800**. Some banks still contain a comparatively similar pair even though their fingerprints differ.
- **Existing candidate-quality score:** mean of per-bank means **4.0111**. This is an internal ranking score, not a calibrated user rating; it must not be read as 4.01/5.
- **Phrase-half observations:** all 2,048 variation slots had valid half metrics. Mean first/second-half idea similarity was **0.3511**; mean change in second-half attack density was **+0.091 attacks per bar**; mean pitch-range change was **+0.004 semitones**.

## How to use this baseline

These measurements describe one eight-variation bank at a time. They do not measure similarity between separate MAGIC presses, and they cannot replace listening in FL Studio. The data should help compare later engine versions under the same seeds, not create automatic diversity targets or justify production score-weight changes by itself.

User listening notes from 2026-10-11 remain the musical guide: improve consistency and distinct identity, preserve strong local moments, and make phrase development feel intentional. Keep the next musical change isolated and code-led; retain deterministic-seed, tonal-safety and strict-grid checks. No generation behavior changes are included in this diagnostic baseline.

## Files

- `bank-summary.csv`: one aggregate row per seed/bank.
- `pairwise-ideas.csv`: all 28 pair comparisons per bank.
- `slot-diagnostics.csv`: candidate quality and first-half/second-half observations for each selected slot.
- `metadata.txt`: measured commit, seed range, workflow run ID and SHA-256 checksums.
