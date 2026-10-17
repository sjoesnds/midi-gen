# MIDI Forge headless judge ablation

The ablation switches are compiled into the `MIDIFORGE_HEADLESS` QA target only. The production/VST3 build gets no-op masking helpers and retains the original Composer Judge aggregation path.

## Run the full fixed-seed report on Windows

From the repository root in PowerShell:

```powershell
.\tools\run_ablation_report.ps1
```

Default: 256 fixed seeds (108000–108255) over 30 conditions: baseline, 19 named score functions, nine Composer Judge aggregate groups and Taste ML disabled. The output is `ablation-report.csv` in the repository root. A two-seed smoke run is:

```powershell
.\tools\run_ablation_report.ps1 -Seeds 2 -Output ablation-pilot.csv
```

Each CSV row is one treatment/seed pair. Metrics include total and melody event counts; tonal-safety and grid fractions; distinct melody fingerprints across eight variations; simple/balanced/complex loop counts; mean attacks per bar; mean unique pitches; pitch range; runtime; and the full MIDI-bank fingerprint.

Taste ML uses a matched deterministic synthetic model for every condition: on the same seed's initial bank, variation 1 receives a like and variation 8 a dislike. The `TasteML` row uses the same model but disables its ranking contribution. Personal saved preference files are never used.

## CI pilot and scope notes

The quality workflow runs a two-seed pilot and uploads `ablation-pilot.csv` as an artifact; the full 256-seed benchmark is kept out of every PR run. `loopForgeScore` and `contextualPhraseQualityScore` are also used by MUTATE/EVOLVE, not the primary MAGIC bank selection path. Their MAGIC ablation rows are useful negative controls; a transform-specific ablation is a separate follow-up if needed.

The report changes selection scores only in the headless test target. It does not tune weights or alter shipped musical behaviour.


## First-pick blind MIDI listening pilot

The headless QA executable creates a blinded listening pack using the first-ranked MAGIC result only. It compares the baseline against nine score ablations that changed the MIDI bank in both seeds of the initial pilot. A condition is included in ratings only if the first-picked MIDI differs; identical first picks are recorded in comparison-audit.csv and not rated.

From PowerShell at the repository root, run:

    .\tools\run_calibration_pack.ps1

The local script defaults to eight fixed seeds (108000-108007), creating up to 72 comparisons. The CI calibration-midi-pilot artifact uses the same eight-seed pack. The actual number of A/B pairs depends on how often excluding a score changes the first-ranked MIDI; the audit CSV records all attempted comparisons.

Keep answer-key.csv closed until ratings-template.csv is complete. For each pair, import A and B into the same FL Studio project and use the same instrument, preset, mixer chain and volume. Rate musical quality, not loudness. The answer key indicates the baseline and ablated score only after listening.

This is subjective calibration, not an automatic weight-tuning mechanism. No production score weights or shipped generation behaviour are changed by this tool.

## Corpus layer target diagnostics (0.109.0)

The corpus summary lives in `data/corpus_targets.json`; no source MIDI notes, interval phrases or rhythm patterns are bundled. The target file contains aggregate medians/IQR ranges only. The QA-only helper is `tests/corpus_layer_metrics.h`, which is not part of the VST3 target.

From the repository root, after building `MidiForgeQA`, run:

```powershell
.\build-qa\MidiForgeQA_artefacts\Release\MidiForgeQA.exe --corpus-layer-ablation 64 corpus-layer-ablation.csv
```

The command compares the baseline to every existing ablation treatment (named score functions, Composer Judge groups and Taste ML disabled) on fixed seeds 108000–108063. It writes the 450-row summary table for each treatment × layer × metric. Each metric median is calculated across the eight MIDI candidates generated per seed; the report also includes its sample count, corpus median, available IQR bounds, signed deviation in IQR widths, whether the value is outside the IQR, the difference from baseline, and tonal/grid safety fractions. The row order and CSV formatting are deterministic; CI checks two identical two-seed runs byte-for-byte and runs the 64-seed report.

Layer assignment is by MIDI pitch: bass < 48, riff 48–59, top ≥ 60. Internal drum channel 5 and standard MIDI channels 9/10 are excluded. Each onset contributes the highest pitch in its layer; durations and note counts use all non-drum notes in that pitch layer. Positions are 16th-note steps, so 4 steps equal one beat and 16 equal a 4/4 bar. `window_repeat` splits the interval line into non-overlapping windows of six intervals, ignores an incomplete tail, and clips each interval to [-12, 12].

The same command also writes a sibling file named `<output-stem>-pipeline.csv`. It records two stages for each seed/treatment: the whole 1,000-candidate scored pool immediately before the existing final selector, and the eight candidates chosen by the existing judge + diversity selector before output cleanup. For each layer/metric it reports P10/P50/P90 across that stage's candidates, per-layer note-count percentiles, how many candidates meet the corpus's 50-notes-per-layer eligibility threshold, and candidate-quality P10/P50/P90. Each record now has `population=all` or `population=layer50_eligible`. The eligible population is filtered independently per pitch layer, so a `top` metric is computed only from candidates with at least 50 top-layer notes, etc. When a layer has no eligible candidates, its metrics are `NA` with zero valid samples; these must not be treated as measured zeros. This lets the report compare only like-enough sample sizes when such candidates exist, while still quantifying how often eligibility is impossible. The quality P10 of the selected eight is the baseline lower-tail guardrail for any future experiment.

CI compares the two pipeline smoke files byte-for-byte. The 2-seed smoke report has 3,600 data rows + header; the 64-seed report has 115,200 data rows + header. A few targets may be undefined for short candidate lines, so each metric row carries its own valid-sample count. The console summary prints the baseline pool/selected medians both across all candidates and within each layer's eligible subset, layer-note coverage, and quality percentiles.

Important comparability notes:
- `min_notes_per_layer=50` describes corpus-track eligibility. Generated samples are four-bar candidate loops and therefore report loop-level metrics rather than pretending each loop contains 50 layer notes.
- The supplied target JSON does not include an IQR for `dur_beats`; its IQR deviation and outside-IQR status are explicitly `NA`.
- The 36-track corpus has broad dispersion and a rough pitch-only layer split. A target miss is diagnostic evidence, not a direction to change production scores or learn/copy any corpus phrase.
- This report is QA-only. It does not change the generator, judge weights, Taste ML or golden MIDI fingerprints.

