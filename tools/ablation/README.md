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
