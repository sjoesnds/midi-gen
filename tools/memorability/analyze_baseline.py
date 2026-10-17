#!/usr/bin/env python3
"""Summarize a MIDI Forge memorability baseline and choose clips for human listening."""
import argparse
import csv
import math
from collections import Counter
from pathlib import Path

REQUIRED = {
    "seed", "variation", "quality", "archetype", "mood", "bars",
    "melody_note_count", "hook_present", "hook_count", "hook_spread_bars",
    "return_candidates", "return_exact", "return_varied", "return_random",
    "return_score", "contrast_density", "contrast_register", "contrast_present",
    "return_threshold", "contrast_threshold_density", "contrast_threshold_register",
    "closure_melody_leap", "closure_melody_tonic_or_dominant", "closure_melody_ok",
    "closure_chord_leap", "closure_chord_tonic_or_dominant", "closure_chord_ok",
}


def n(row, key, default=0.0):
    try:
        value = float(row[key])
        return value if math.isfinite(value) else default
    except (KeyError, TypeError, ValueError):
        return default


def i(row, key, default=0):
    try:
        return int(float(row[key]))
    except (KeyError, TypeError, ValueError):
        return default


def pct(values, p):
    values = sorted(values)
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]
    pos = (len(values) - 1) * p
    lo, hi = math.floor(pos), math.ceil(pos)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def reasons(r, q25):
    out = []
    if i(r, "melody_note_count") <= 0:
        out.append("empty_melody")
    if i(r, "hook_present") != 1:
        out.append("no_repeated_hook")
    if i(r, "return_candidates", -1) <= 0:
        out.append("no_return_candidate")
    elif n(r, "return_varied", -1) <= 0:
        out.append("no_varied_return")
    if i(r, "contrast_present") != 1:
        out.append("below_baseline_contrast_cutpoints")
    leap = i(r, "closure_melody_leap", -1)
    if leap < 0:
        out.append("missing_closure_measurement")
    else:
        if leap > 2:
            out.append("large_melody_loop_seam_leap")
        if i(r, "closure_melody_tonic_or_dominant") != 1:
            out.append("ending_not_tonic_or_dominant")

    chord_leap = i(r, "closure_chord_leap", -1)
    if chord_leap >= 0:
        if chord_leap > 2:
            out.append("large_chord_loop_seam_leap")
        if i(r, "closure_chord_tonic_or_dominant") != 1:
            out.append("chord_ending_without_tonic_or_dominant")
    if n(r, "quality", -1) <= q25:
        out.append("quality_at_or_below_q25")
    return out


def all_core(r):
    return (i(r, "hook_present") == 1
            and i(r, "return_candidates", -1) > 0
            and n(r, "return_varied", -1) > 0
            and i(r, "contrast_present") == 1
            and i(r, "closure_melody_ok") == 1)


def choose(rows, q25, limit):
    reason_map = {(r["seed"], r["variation"]): reasons(r, q25) for r in rows}
    quality = [n(r, "quality", -1) for r in rows]
    median_q = pct(quality, 0.5)
    specs = [
        ("best_all_metrics", all_core, lambda r: (n(r, "quality", -1), -i(r, "seed"), -i(r, "variation"))),
        ("highest_quality", lambda r: True, lambda r: (n(r, "quality", -1), -i(r, "seed"), -i(r, "variation"))),
        ("lowest_quality", lambda r: True, lambda r: (-n(r, "quality", -1), -i(r, "seed"), -i(r, "variation"))),
        ("no_hook", lambda r: i(r, "hook_present") != 1, lambda r: (n(r, "quality", -1), -i(r, "seed"))),
        ("weak_return", lambda r: i(r, "return_candidates", -1) <= 0 or n(r, "return_varied", -1) <= 0, lambda r: (n(r, "quality", -1), -i(r, "seed"))),
        ("low_contrast", lambda r: i(r, "contrast_present") != 1, lambda r: (n(r, "quality", -1), -i(r, "seed"))),
        ("large_seam_leap", lambda r: i(r, "closure_melody_leap", -1) > 2, lambda r: (i(r, "closure_melody_leap", -1), n(r, "quality", -1))),
        ("non_cadential_ending", lambda r: 0 <= i(r, "closure_melody_leap", -1) <= 2 and i(r, "closure_melody_tonic_or_dominant") != 1, lambda r: (n(r, "quality", -1), -i(r, "seed"))),
        ("exact_return", lambda r: n(r, "return_exact") > 0, lambda r: (n(r, "quality", -1), n(r, "return_exact"))),
        ("strong_contrast", lambda r: i(r, "contrast_present") == 1, lambda r: (n(r, "contrast_density"), n(r, "contrast_register"), n(r, "quality", -1))),
        ("strong_hook", lambda r: i(r, "hook_present") == 1, lambda r: (i(r, "hook_count"), n(r, "hook_spread_bars"), n(r, "quality", -1))),
        ("multiple_flags", lambda r: len(reason_map[(r["seed"], r["variation"])]) >= 3, lambda r: (len(reason_map[(r["seed"], r["variation"])]), n(r, "quality", -1))),
        ("median_quality", lambda r: True, lambda r: (-abs(n(r, "quality", -1) - median_q), -i(r, "seed"), -i(r, "variation"))),
    ]
    chosen, used, used_seeds = [], set(), set()

    def add(role, row):
        chosen.append((role, row))
        used.add((row["seed"], row["variation"]))
        used_seeds.add(row["seed"])

    for role, predicate, ranking in specs:
        if len(chosen) >= limit:
            break
        candidates = sorted((r for r in rows if predicate(r)), key=ranking, reverse=True)
        row = next((r for r in candidates if (r["seed"], r["variation"]) not in used and r["seed"] not in used_seeds), None)
        if row is None:
            row = next((r for r in candidates if (r["seed"], r["variation"]) not in used), None)
        if row is not None:
            add(role, row)

    for fraction in (0.05, 0.25, 0.50, 0.75, 0.95, 0.35, 0.65, 0.15, 0.85, 0.45, 0.55, 0.90):
        if len(chosen) >= limit:
            break
        target = pct(quality, fraction)
        candidates = sorted(rows, key=lambda r: (abs(n(r, "quality", -1) - target), i(r, "seed"), i(r, "variation")))
        row = next((r for r in candidates if (r["seed"], r["variation"]) not in used and r["seed"] not in used_seeds), None)
        if row is None:
            row = next((r for r in candidates if (r["seed"], r["variation"]) not in used), None)
        if row is not None:
            add("quality_quantile_%02d" % round(fraction * 100), row)
    return chosen[:limit]


def write_csv(path, fields, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True, type=Path)
    ap.add_argument("--output-dir", required=True, type=Path)
    ap.add_argument("--expected-seeds", type=int, default=256)
    ap.add_argument("--expected-slots", type=int, default=8)
    ap.add_argument("--listening-items", type=int, default=12)
    args = ap.parse_args()
    if not args.input.is_file():
        ap.error("input CSV does not exist: %s" % args.input)
    with args.input.open("r", newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        missing = sorted(REQUIRED - set(fields))
        if missing:
            ap.error("input CSV is missing columns: " + ", ".join(missing))
        rows = list(reader)
    seeds = sorted({r["seed"] for r in rows}, key=int)
    expected_rows = args.expected_seeds * args.expected_slots
    per_seed = Counter(r["seed"] for r in rows)
    if (len(seeds) != args.expected_seeds or len(rows) != expected_rows
            or any(count != args.expected_slots for count in per_seed.values())):
        ap.error("expected %d seeds x %d slots = %d rows; got %d seeds and %d rows" %
                 (args.expected_seeds, args.expected_slots, expected_rows, len(seeds), len(rows)))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    quality = [n(r, "quality", -1) for r in rows]
    q25, med, q75 = pct(quality, .25), pct(quality, .5), pct(quality, .75)
    flagged = []
    for row in rows:
        why = reasons(row, q25)
        if why:
            result = dict(row)
            result["counterexample_reasons"] = ";".join(why)
            flagged.append(result)
    write_csv(args.output_dir / "counterexamples.csv", fields + ["counterexample_reasons"], flagged)

    chosen = choose(rows, q25, args.listening_items)
    if len(chosen) != args.listening_items:
        ap.error("selected %d clips, expected %d" % (len(chosen), args.listening_items))
    selection_fields = fields + ["selection_role", "midi_filename"]
    selection = []
    for index, (role, row) in enumerate(chosen, 1):
        result = dict(row)
        result["selection_role"] = role
        result["midi_filename"] = "%02d_%s_seed%s_v%s_a%s.mid" % (
            index, role, row["seed"], row["variation"], row["archetype"])
        selection.append(result)
    write_csv(args.output_dir / "listening-selection.csv", selection_fields, selection)

    measures = [
        ("Repeated hook detected", lambda r: i(r, "hook_present") == 1),
        ("At least one return candidate", lambda r: i(r, "return_candidates", -1) > 0),
        ("At least one varied return", lambda r: n(r, "return_varied", -1) > 0),
        ("Above-baseline contrast cut points", lambda r: i(r, "contrast_present") == 1),
        ("Melody loop seam within 2 semitones", lambda r: 0 <= i(r, "closure_melody_leap", -1) <= 2),
        ("Melody ends on tonic/dominant pitch class", lambda r: i(r, "closure_melody_tonic_or_dominant") == 1),
        ("Composite melody closure proxy passes", lambda r: i(r, "closure_melody_ok") == 1),
        ("All four main proxies pass", all_core),
    ]
    chord_rows = [r for r in rows if i(r, "closure_chord_leap", -1) >= 0]
    chord_seam_near = sum(0 <= i(r, "closure_chord_leap", -1) <= 2 for r in chord_rows)
    chord_cadential = sum(i(r, "closure_chord_tonic_or_dominant") == 1 for r in chord_rows)
    chord_pass = sum(i(r, "closure_chord_ok") == 1 for r in chord_rows)
    lines = [
        "# MIDI Forge memorability baseline summary", "",
        "- Seeds: %d (%s-%s)" % (len(seeds), seeds[0], seeds[-1]),
        "- Bank rows: %d (expected %d x %d)" % (len(rows), args.expected_seeds, args.expected_slots),
        "- Selected FL Studio listening clips: %d" % len(selection),
        "- Rows with counterexample flags: %d / %d (%.1f%%)" % (len(flagged), len(rows), 100 * len(flagged) / len(rows)),
        "", "## Corpus-derived cut points", "",
        "- Return edit-distance threshold: %s" % rows[0]["return_threshold"],
        "- Per-bar density contrast threshold: %s" % rows[0]["contrast_threshold_density"],
        "- Per-bar register contrast threshold: %s" % rows[0]["contrast_threshold_register"],
        "", "These are comparative thresholds derived from this corpus, not universal definitions of a memorable melody.",
        "", "## Structural proxy rates", "", "| Measure | Count | Rate |", "|---|---:|---:|",
    ]
    for label, predicate in measures:
        count = sum(predicate(r) for r in rows)
        lines.append("| %s | %d / %d | %.1f%% |" % (label, count, len(rows), 100 * count / len(rows)))
    lines += [
        "| Chord loop seam within 2 semitones (where chord events exist) | %d / %d | %.1f%% |" %
        (chord_seam_near, len(chord_rows), 100 * chord_seam_near / len(chord_rows) if chord_rows else 0),
        "| Chord ends on tonic/dominant pitch class (where chord events exist) | %d / %d | %.1f%% |" %
        (chord_cadential, len(chord_rows), 100 * chord_cadential / len(chord_rows) if chord_rows else 0),
        "| Composite chord closure proxy (where chord events exist) | %d / %d | %.1f%% |" %
        (chord_pass, len(chord_rows), 100 * chord_pass / len(chord_rows) if chord_rows else 0),
        "", "Chord denominator excludes rows without chord events.",
        "", "## Selected-candidate quality distribution", "",
        "- Min: %.4f" % min(quality), "- 25th percentile: %.4f" % q25,
        "- Median: %.4f" % med, "- 75th percentile: %.4f" % q75, "- Max: %.4f" % max(quality),
        "", "## Counterexample flags", "",
    ]
    reason_counts = Counter(reason for r in flagged for reason in r["counterexample_reasons"].split(";") if reason)
    lines.extend("- %s: %d" % (reason, count) for reason, count in reason_counts.most_common())
    if not reason_counts:
        lines.append("- No flags in this baseline.")
    lines += [
        "", "## Interpretation", "",
        "- Hook detection compares pitch-interval contour with tolerant onset gaps and durations. It is transposition-safe but may miss recognizable hooks.",
        "- Return and contrast rates depend on corpus-derived cut points; they are not objective musical-quality scores.",
        "- The report separates loop-seam distance from whether the ending contains tonic/dominant material. The strict composite proxy requires both a seam within two semitones and a tonic/dominant ending; neither condition alone proves musical closure.",
        "- The 12 selected clips include positive, weak and edge-case examples. Audition them in FL Studio with the same instrument and preset before changing generation or scoring.",
        "",
    ]
    (args.output_dir / "summary.md").write_text("\n".join(lines), encoding="utf-8")
    print("MEMORABILITY_ANALYSIS rows=%d counterexamples=%d listening_items=%d" % (len(rows), len(flagged), len(selection)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
