#!/usr/bin/env python3
"""MIDI Forge: like-rate report from feedback.csv (stdlib only).

Usage:
    python tools/analyze_feedback.py                # default location of feedback.csv
    python tools/analyze_feedback.py path\\to\\feedback.csv
    python tools/analyze_feedback.py --min-n 10     # hide groups with fewer ratings

Every row of feedback.csv is one event: like / dislike / export / drag_all / drag_part.
Like-rate = likes / (likes + dislikes). The +/- column is the half-width of a 95% Wilson interval:
with few ratings it is huge, and that is the point - do not tune anything on groups whose interval is wide.
"""
import argparse
import csv
import math
import os
import sys
from collections import defaultdict


def default_path():
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA", os.path.expanduser("~"))
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.path.expanduser("~/.config")
    return os.path.join(base, "MidiForge", "feedback.csv")


def wilson(likes, n, z=1.96):
    if n == 0:
        return 0.0, 0.0
    p = likes / n
    denom = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    return centre, half


DIMENSIONS = ["archetype", "transform", "sound", "mood", "melody_type", "scale", "bars", "engine"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", default=default_path())
    ap.add_argument("--min-n", type=int, default=1, help="hide groups with fewer than N likes+dislikes")
    args = ap.parse_args()

    if not os.path.exists(args.csv):
        sys.exit("No feedback file at %s - press LIKE / DISLIKE in the plugin first." % args.csv)

    with open(args.csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    rated = [r for r in rows if r.get("verdict") in ("like", "dislike")]
    kept = [r for r in rows if r.get("verdict") in ("export", "drag_all", "drag_part")]
    likes = sum(r["verdict"] == "like" for r in rated)
    print("File: %s" % args.csv)
    print("Events: %d  |  rated: %d (%d like / %d dislike)  |  exported or dragged: %d" %
          (len(rows), len(rated), likes, len(rated) - likes, len(kept)))
    if rated:
        p, h = wilson(likes, len(rated))
        print("Overall like-rate: %.0f%% +/- %.0f" % (100 * likes / len(rated), 100 * h))
    print()

    for dim in DIMENSIONS:
        groups = defaultdict(lambda: [0, 0, 0])  # likes, rated, kept
        for r in rated:
            g = groups[r.get(dim, "?")]
            g[1] += 1
            g[0] += r["verdict"] == "like"
        for r in kept:
            groups[r.get(dim, "?")][2] += 1
        table = [(k, v) for k, v in groups.items() if v[1] >= args.min_n]
        if len(table) < 2 and dim != "archetype":
            continue
        print("== by %s ==" % dim)
        print("%-16s %5s %6s %8s %7s" % ("value", "n", "likes", "rate", "kept"))
        for k, (l, n, kp) in sorted(table, key=lambda kv: -(kv[1][0] / kv[1][1] if kv[1][1] else 0)):
            _, h = wilson(l, n)
            print("%-16s %5d %6d %4.0f%%+/-%-2.0f %7d" % (k, n, l, 100 * l / n if n else 0, 100 * h, kp))
        print()

    if len(rated) < 100:
        print("Note: only %d ratings so far. Below ~200 the intervals are too wide to tune weights on." % len(rated))


if __name__ == "__main__":
    main()
