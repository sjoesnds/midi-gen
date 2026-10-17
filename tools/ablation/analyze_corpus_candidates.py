#!/usr/bin/env python3
"""Deterministic, offline analysis of MIDI Forge's headless candidate audit CSV."""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any

POOL = "candidate_pool_before_final_selection"
SELECTED = "selected_bank_before_output_cleanup"
LAYERS = ("top", "riff", "bass")
METRICS = ("steps_le2", "leaps_ge5", "window_repeat", "dur_beats", "notes_per_bar")
REPEAT_PROBES = (
    "window_repeat6_sliding_clip",
    "window_repeat4_nonoverlap_clip",
    "window_repeat4_sliding_clip",
    "window_repeat6_nonoverlap_raw",
)
TOP_SELECTION_METRICS = ("steps_le2", "leaps_ge5", "notes_per_bar")
MIN_TOP_NOTES = 20


def num(value: Any) -> float | None:
    if value is None or str(value).strip().upper() in ("", "NA"):
        return None
    try:
        value = float(value)
    except (TypeError, ValueError):
        return None
    return value if math.isfinite(value) else None


def q(values: list[float], p: float) -> float | None:
    if not values:
        return None
    values = sorted(values)
    pos = max(0.0, min(1.0, p)) * (len(values) - 1)
    lo = int(pos)
    hi = min(len(values) - 1, lo + 1)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def rnd(value: float | None) -> float | None:
    return None if value is None else round(value, 6)


def rate(numerator: int, denominator: int) -> float | None:
    return rnd(numerator / denominator) if denominator else None


def bounds_for(target: dict[str, Any], metric: str) -> list[float] | None:
    value = target.get("iqr", {}).get(metric)
    return value if isinstance(value, list) and len(value) >= 2 else None


def value_for(row: dict[str, str], layer: str, metric: str) -> float | None:
    return num(row.get(f"{layer}_{metric}"))


def within(value: float | None, bounds: list[float] | None) -> bool:
    return value is not None and bounds is not None and bounds[0] <= value <= bounds[1]


def fits(row: dict[str, str], layer: str, target: dict[str, Any], metrics: tuple[str, ...]) -> bool:
    return all(within(value_for(row, layer, m), bounds_for(target, m)) for m in metrics)


def quality_stats(rows: list[dict[str, str]]) -> dict[str, Any]:
    vals = [v for row in rows if (v := num(row.get("quality"))) is not None]
    return {"count": len(vals), "p10": rnd(q(vals, .10)), "p50": rnd(q(vals, .50)), "p90": rnd(q(vals, .90))}


def summarize_stage_layer(rows: list[dict[str, str]], layer: str, target: dict[str, Any]) -> dict[str, Any]:
    counts = [int(num(row.get(f"{layer}_note_count")) or 0) for row in rows]
    result: dict[str, Any] = {
        "candidate_count": len(rows),
        "note_count": {
            "p10": rnd(q([float(x) for x in counts], .10)),
            "p50": rnd(q([float(x) for x in counts], .50)),
            "p90": rnd(q([float(x) for x in counts], .90)),
            "at_least_20_count": sum(x >= 20 for x in counts),
            "at_least_20_fraction": rate(sum(x >= 20 for x in counts), len(rows)),
            "at_least_50_count": sum(x >= 50 for x in counts),
            "at_least_50_fraction": rate(sum(x >= 50 for x in counts), len(rows)),
        },
        "quality": quality_stats(rows),
        "metrics": {},
    }
    metric_iqrs = {m: bounds_for(target, m) for m in METRICS}
    valid_joint_metrics = tuple(m for m in METRICS if metric_iqrs[m] is not None)
    fit_all = sum(fits(row, layer, target, valid_joint_metrics) for row in rows)
    fit_contour = sum(fits(row, layer, target, TOP_SELECTION_METRICS) for row in rows)
    eligible_contour = [
        row for row in rows
        if int(num(row.get(f"{layer}_note_count")) or 0) >= 50
        and fits(row, layer, target, TOP_SELECTION_METRICS)
    ]

    for metric in METRICS:
        vals = [v for row in rows if (v := value_for(row, layer, metric)) is not None]
        metric_bounds = metric_iqrs[metric]
        inside = sum(within(value_for(row, layer, metric), metric_bounds) for row in rows) if metric_bounds else None
        result["metrics"][metric] = {
            "target_iqr": metric_bounds,
            "valid_count": len(vals),
            "valid_fraction": rate(len(vals), len(rows)),
            "median": rnd(q(vals, .50)),
            "within_iqr_count": inside,
            "within_iqr_fraction_of_valid": rate(inside, len(vals)) if inside is not None else None,
        }

    result["joint_source_iqr_match"] = {
        "metrics": list(valid_joint_metrics), "candidate_count": fit_all,
        "candidate_fraction": rate(fit_all, len(rows)),
    }
    result["joint_contour_density_match"] = {
        "metrics": list(TOP_SELECTION_METRICS), "candidate_count": fit_contour,
        "candidate_fraction": rate(fit_contour, len(rows)),
        "seeds_with_match": len({row["seed"] for row in rows if fits(row, layer, target, TOP_SELECTION_METRICS)}),
        "quality": quality_stats([row for row in rows if fits(row, layer, target, TOP_SELECTION_METRICS)]),
        "at_least_50_notes_count": len(eligible_contour),
    }
    return result


def repeat_probe_stats(rows: list[dict[str, str]], layer: str, column_suffix: str) -> dict[str, Any]:
    values = [
        (row, value)
        for row in rows
        if (value := num(row.get(f"{layer}_{column_suffix}"))) is not None
    ]
    all_count = len(rows)
    nonzero = sum(value > 0.0 for _, value in values)
    stats: dict[str, Any] = {
        "candidate_count": all_count,
        "valid_count": len(values),
        "valid_fraction": rate(len(values), all_count),
        "nonzero_count": nonzero,
        "nonzero_fraction_of_valid": rate(nonzero, len(values)),
        "median": rnd(q([value for _, value in values], .50)),
        "p90": rnd(q([value for _, value in values], .90)),
        "note_count_bands": {},
    }
    bands = (
        ("lt20", lambda n: n < 20),
        ("20to49", lambda n: 20 <= n < 50),
        ("ge50", lambda n: n >= 50),
    )
    for name, predicate in bands:
        band_rows = [
            (row, value)
            for row, value in values
            if predicate(int(num(row.get(f"{layer}_note_count")) or 0))
        ]
        band_nonzero = sum(value > 0.0 for _, value in band_rows)
        stats["note_count_bands"][name] = {
            "candidate_count": sum(
                predicate(int(num(row.get(f"{layer}_note_count")) or 0)) for row in rows
            ),
            "valid_count": len(band_rows),
            "nonzero_count": band_nonzero,
            "nonzero_fraction_of_valid": rate(band_nonzero, len(band_rows)),
            "median": rnd(q([value for _, value in band_rows], .50)),
            "p90": rnd(q([value for _, value in band_rows], .90)),
        }
    return stats


def summarize_repeat_sensitivity(rows: list[dict[str, str]]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "source_definition_verified": False,
        "warning": "Probe variants are diagnostic only; the corpus calculator/formula is not in this repository, so none is treated as matching its window_repeat target.",
        "stages": {},
    }
    for stage in (POOL, SELECTED):
        stage_rows = [row for row in rows if row.get("stage") == stage]
        result["stages"][stage] = {
            layer: {
                "original_window_repeat": repeat_probe_stats(stage_rows, layer, "window_repeat"),
                "variants": {
                    name: repeat_probe_stats(stage_rows, layer, name)
                    for name in REPEAT_PROBES
                },
            }
            for layer in LAYERS
        }
    result["variant_definitions"] = {
        "window_repeat": "Current diagnostic metric: complete non-overlapping six-interval windows; each interval clipped to [-12, 12]; excess exact duplicates divided by window count; reports zero with one complete window.",
        "window_repeat6_sliding_clip": "Six-interval windows, stride one, intervals clipped to [-12, 12]; valid only with at least two windows.",
        "window_repeat4_nonoverlap_clip": "Four-interval windows, stride four, intervals clipped to [-12, 12]; valid only with at least two windows.",
        "window_repeat4_sliding_clip": "Four-interval windows, stride one, intervals clipped to [-12, 12]; valid only with at least two windows.",
        "window_repeat6_nonoverlap_raw": "Six-interval windows, stride six, no interval clipping; valid only with at least two windows.",
    }
    return result


def per_seed_bank_p10(rows: list[dict[str, str]]) -> dict[str, float]:
    by_seed: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        if (v := num(row.get("quality"))) is not None:
            by_seed[row["seed"]].append(v)
    return {seed: float(q(vals, .10)) for seed, vals in by_seed.items() if vals}


def selection_probe(pool_rows: list[dict[str, str]], selected_rows: list[dict[str, str]], target: dict[str, Any]) -> dict[str, Any]:
    """Diagnostic rerank only: quality-first inside three corpus IQRs, fallback to existing selected bank."""
    pool_by_seed: dict[str, list[dict[str, str]]] = defaultdict(list)
    selected_by_seed: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in pool_rows:
        pool_by_seed[row["seed"]].append(row)
    for row in selected_rows:
        selected_by_seed[row["seed"]].append(row)

    baseline_p10 = per_seed_bank_p10(selected_rows)
    alternative: dict[str, list[dict[str, str]]] = {}
    fit_counts: dict[str, int] = {}
    for seed in sorted(pool_by_seed, key=int):
        fit = [
            row for row in pool_by_seed[seed]
            if int(num(row.get("top_note_count")) or 0) >= MIN_TOP_NOTES
            and fits(row, "top", target, TOP_SELECTION_METRICS)
        ]
        fit.sort(key=lambda row: (
            -(num(row.get("quality")) if num(row.get("quality")) is not None else -math.inf),
            int(num(row.get("candidate_index")) or 0),
        ))
        fit_counts[seed] = len(fit)
        chosen = fit[:8]
        chosen_ids = {row.get("candidate_identity") for row in chosen}
        fallback = [
            row for row in selected_by_seed.get(seed, [])
            if row.get("candidate_identity") not in chosen_ids
        ]
        fallback.sort(key=lambda row: (
            -(num(row.get("quality")) if num(row.get("quality")) is not None else -math.inf),
            int(num(row.get("candidate_index")) or 0),
        ))
        chosen.extend(fallback[:max(0, 8 - len(chosen))])
        alternative[seed] = chosen[:8]

    alt_rows = [row for seed in sorted(alternative, key=int) for row in alternative[seed]]
    alt_p10 = per_seed_bank_p10(alt_rows)
    seeds = sorted(set(baseline_p10) & set(alt_p10), key=int)
    deltas = [alt_p10[s] - baseline_p10[s] for s in seeds]
    metric_report: dict[str, Any] = {}
    for metric in METRICS:
        vals = [v for row in alt_rows if (v := value_for(row, "top", metric)) is not None]
        bounds = bounds_for(target, metric)
        median_by_seed = []
        seeds_in_iqr = 0
        valid_seeds = 0
        for chosen in alternative.values():
            per_seed = [v for row in chosen if (v := value_for(row, "top", metric)) is not None]
            if per_seed:
                median_value = q(per_seed, .50)
                median_by_seed.append(float(median_value))
                valid_seeds += 1
                if within(median_value, bounds):
                    seeds_in_iqr += 1
        within_count = sum(within(value_for(row, "top", metric), bounds) for row in alt_rows) if bounds else None
        metric_report[metric] = {
            "target_iqr": bounds,
            "valid_candidate_count": len(vals),
            "candidate_fraction_within_iqr": rate(within_count, len(vals)) if within_count is not None else None,
            "median_of_per_seed_medians": rnd(q(median_by_seed, .50)),
            "seeds_with_median_in_iqr": seeds_in_iqr if bounds else None,
            "seeds_evaluated": valid_seeds if bounds else None,
        }

    baseline_p10_vals = list(baseline_p10.values())
    alt_p10_vals = list(alt_p10.values())
    return {
        "description": "Quality-ranked top-8 candidates matching top steps/leaps/notes-per-bar IQRs; missing slots filled from the existing selected bank.",
        "minimum_top_notes_for_match": MIN_TOP_NOTES,
        "seeds_with_at_least_8_fit_candidates": sum(n >= 8 for n in fit_counts.values()),
        "seeds_total": len(alternative),
        "fit_candidate_count_per_seed": {
            "p10": rnd(q([float(v) for v in fit_counts.values()], .10)),
            "p50": rnd(q([float(v) for v in fit_counts.values()], .50)),
            "p90": rnd(q([float(v) for v in fit_counts.values()], .90)),
        },
        "quality_guardrail": {
            "current_bank_median_per_seed_p10": rnd(q(baseline_p10_vals, .50)),
            "probe_bank_median_per_seed_p10": rnd(q(alt_p10_vals, .50)),
            "probe_bank_p10_of_seed_p10": rnd(q(alt_p10_vals, .10)),
            "probe_bank_p90_of_seed_p10": rnd(q(alt_p10_vals, .90)),
            "median_per_seed_p10_delta": rnd(q(deltas, .50)),
            "p10_per_seed_p10_delta": rnd(q(deltas, .10)),
            "seeds_without_quality_p10_regression": sum(d >= -1e-9 for d in deltas),
            "seeds_compared": len(deltas),
        },
        "bank_metrics": metric_report,
        "caveat": "This offline probe ignores the production diversity gate and is not a MIDI-output change. It cannot fix the missing window_repeat matches by reranking this candidate pool.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", required=True, type=Path)
    parser.add_argument("--targets", default=Path("data/corpus_targets.json"), type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not args.candidates.is_file():
        parser.error(f"candidate CSV not found: {args.candidates}")
    if not args.targets.is_file():
        parser.error(f"target JSON not found: {args.targets}")
    try:
        targets = json.loads(args.targets.read_text(encoding="utf-8"))
        with args.candidates.open("r", encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
    except (OSError, json.JSONDecodeError, csv.Error) as exc:
        parser.error(str(exc))
    required = {
        "seed", "treatment", "stage", "candidate_index", "candidate_identity", "quality",
        *(f"{layer}_{probe}" for layer in LAYERS for probe in REPEAT_PROBES),
    }
    if not rows or not required.issubset(rows[0]):
        parser.error("candidate CSV is empty or missing required candidate/repeat-probe columns")
    rows = [row for row in rows if row.get("treatment") == "Baseline"]
    pool = [row for row in rows if row.get("stage") == POOL]
    selected = [row for row in rows if row.get("stage") == SELECTED]
    if not pool or not selected:
        parser.error("candidate CSV needs Baseline pool and selected-bank rows")
    report = {
        "schema_version": 1,
        "source_summary": {
            "baseline_candidate_rows": len(rows),
            "seed_count": len({row.get("seed") for row in rows}),
            "pool_candidate_rows": len(pool),
            "selected_candidate_rows": len(selected),
            "corpus_track_count": targets.get("n_tracks"),
            "corpus_min_notes_per_layer": targets.get("min_notes_per_layer"),
        },
        "interpretation_notes": [
            "The 50-notes-per-layer threshold describes source-corpus eligibility; conditioning a four-bar loop on 50 notes can bias notes_per_bar upward.",
            "The original corpus window_repeat calculator/formula is not included in this repository. Four explicitly named alternative window definitions are diagnostic probes only and are not compared to the corpus target.",
            "Pitch bands top/riff/bass are rough proxies, not semantic source tracks; aggregate targets from 36 tracks are diagnostics, not a score to optimize blindly.",
            "The alternative selector probe ignores musical diversity. This script never changes MIDI output, production scores, judge weights, Taste ML, or the plugin version.",
        ],
        "stage_layer_summary": {
            POOL: {layer: summarize_stage_layer(pool, layer, targets["layers"][layer]) for layer in LAYERS},
            SELECTED: {layer: summarize_stage_layer(selected, layer, targets["layers"][layer]) for layer in LAYERS},
        },
        "window_repeat_sensitivity": summarize_repeat_sensitivity(rows),
        "top_layer_selection_probe": selection_probe(pool, selected, targets["layers"]["top"]),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    probe = report["top_layer_selection_probe"]
    guard = probe["quality_guardrail"]
    print(f"Analysis rows: {len(pool)} pool / {len(selected)} selected; fit all source IQRs: "
          f"{report['stage_layer_summary'][POOL]['top']['joint_source_iqr_match']['candidate_count']}")
    print(f"Top selection probe quality P10 median: {guard['current_bank_median_per_seed_p10']} -> "
          f"{guard['probe_bank_median_per_seed_p10']}; no-regression seeds "
          f"{guard['seeds_without_quality_p10_regression']}/{guard['seeds_compared']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
