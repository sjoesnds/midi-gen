#!/usr/bin/env python3
"""Validate source corpus targets against the anonymized per-track metric summaries.

This verifies target aggregation, not the original MIDI-to-metric calculation: source
MIDI files are intentionally not included. Per-track values are rounded to three
decimals, so quartile comparisons allow the small resulting rounding difference.
"""
from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any

LAYERS = ("top", "riff", "bass")
METRICS = ("steps_le2", "leaps_ge5", "window_repeat", "dur_beats", "notes_per_bar")
ROUNDING_TOLERANCE = 0.0011


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"expected JSON object in {path}")
    return value


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tracks", type=Path, default=root / "data/corpus_per_track_stats.json")
    parser.add_argument("--targets", type=Path, default=root / "data/corpus_targets.json")
    args = parser.parse_args()

    tracks_doc = load_json(args.tracks)
    targets = load_json(args.targets)
    tracks = tracks_doc.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        raise SystemExit("per-track summary has no non-empty 'tracks' array")
    if targets.get("n_tracks") != len(tracks) or tracks_doc.get("n_tracks") != len(tracks):
        raise SystemExit(f"track-count mismatch: summaries={len(tracks)}, targets={targets.get('n_tracks')}, source={tracks_doc.get('n_tracks')}")

    min_notes = int(targets.get("min_notes_per_layer", 50))
    if min_notes < 1:
        raise SystemExit("min_notes_per_layer must be positive")
    target_layers = targets.get("layers", {})
    checks = 0
    max_error = 0.0
    eligible_counts: dict[str, int] = {}

    for layer in LAYERS:
        eligible = [
            row[layer]
            for row in tracks
            if isinstance(row, dict)
            and isinstance(row.get(layer), dict)
            and int(row[layer].get("notes", 0)) >= min_notes
        ]
        eligible_counts[layer] = len(eligible)
        if len(eligible) < 2:
            raise SystemExit(f"not enough eligible track values for {layer}: {len(eligible)}")
        for metric in METRICS:
            values = sorted(float(row[metric]) for row in eligible if row.get(metric) is not None)
            if len(values) != len(eligible):
                raise SystemExit(f"missing {metric} values for eligible {layer} tracks")
            quartiles = statistics.quantiles(values, n=4, method="exclusive")
            computed = (statistics.median(values), quartiles[0], quartiles[2])
            layer_target = target_layers.get(layer, {})
            median = layer_target.get(metric)
            iqr = layer_target.get("iqr", {}).get(metric)
            if median is None or not isinstance(iqr, list) or len(iqr) != 2:
                raise SystemExit(f"target missing median/IQR for {layer}.{metric}")
            expected = (float(median), float(iqr[0]), float(iqr[1]))
            for label, actual, wanted in zip(("median", "Q1", "Q3"), computed, expected):
                error = abs(actual - wanted)
                max_error = max(max_error, error)
                if error > ROUNDING_TOLERANCE:
                    raise SystemExit(
                        f"{layer}.{metric} {label} mismatch: computed={actual:.6f}, target={wanted:.6f}, "
                        f"abs_error={error:.6f} > tolerance={ROUNDING_TOLERANCE:.4f}"
                    )
            checks += 1

    print(
        f"PASS: {checks} layer/metric summaries match 36 anonymized track aggregates "
        f"(eligible tracks top={eligible_counts['top']}, riff={eligible_counts['riff']}, "
        f"bass={eligible_counts['bass']}); maximum median/IQR difference={max_error:.6f}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
