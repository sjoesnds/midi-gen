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
