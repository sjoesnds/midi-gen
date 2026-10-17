#!/usr/bin/env python3
"""Prepare an anonymous MIDI listening pack without changing generated MIDI."""
import argparse
import csv
import re
from pathlib import Path


def read_rows(path: Path):
    if not path.is_file():
        raise SystemExit("selection CSV does not exist: %s" % path)
    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        fields = reader.fieldnames or []
    missing = sorted({"seed", "variation", "archetype"} - set(fields))
    if missing:
        raise SystemExit("selection CSV is missing columns: " + ", ".join(missing))
    if not rows:
        raise SystemExit("selection CSV contains no listening items")
    return fields, rows


def write_csv(path: Path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def prepare(args):
    source_fields, rows = read_rows(args.selection)
    if len(rows) != args.expected_items:
        raise SystemExit("expected %d selected clips, got %d" % (args.expected_items, len(rows)))

    exports = []
    answers = []
    for index, row in enumerate(rows, 1):
        name = "clip_%02d.mid" % index
        source_name = row.get("midi_filename", "")
        answer = dict(row)
        answer["source_midi_filename"] = source_name
        answer["blind_filename"] = name
        answer["midi_filename"] = name
        answers.append(answer)
        exports.append({
            "seed": row["seed"],
            "variation": row["variation"],
            "archetype": row["archetype"],
            "midi_filename": name,
        })

    write_csv(args.export_selection,
              ["seed", "variation", "archetype", "midi_filename"], exports)
    answer_fields = list(source_fields)
    for extra in ("source_midi_filename", "blind_filename"):
        if extra not in answer_fields:
            answer_fields.append(extra)
    write_csv(args.answer_key, answer_fields, answers)

    summary = args.selection.parent / "summary.md"
    if summary.is_file():
        previous = summary.read_text(encoding="utf-8")
        heading = "## Anonymous listening pack"
        if heading not in previous:
            with summary.open("a", encoding="utf-8") as stream:
                stream.write(
                    "\n\n" + heading + "\n\n"
                    "- The MIDI files use neutral names such as clip_01.mid.\n"
                    "- listening-answer-key.csv maps each clip ID to its diagnostic role and source metadata.\n"
                    "- For an unbiased first listen, open only the separately uploaded memorability-listening-pack-blind artifact. Keep the answer key closed until after noting impressions.\n"
                )
    print("BLIND_LISTENING_SELECTION items=%d names=clip_01..clip_%02d.mid" %
          (len(exports), len(exports)))


def finalize(args):
    pack = args.pack_dir
    if not pack.is_dir():
        raise SystemExit("listening pack directory does not exist: %s" % pack)
    midi_files = sorted(pack.glob("*.mid"))
    expected = {"clip_%02d.mid" % index for index in range(1, args.expected_items + 1)}
    actual = {item.name for item in midi_files}
    if len(midi_files) != args.expected_items or actual != expected:
        raise SystemExit("blind pack filenames do not match the expected anonymous clip IDs")
    if any(re.search(r"(best|worst|quality|hook|return|contrast|closure|seed|archetype|low|high)", name, re.I)
           for name in actual):
        raise SystemExit("a diagnostic label leaked into a listening filename")

    prompt = (
        "# MIDI Forge — anonymous free-form listening notes\n\n"
        "Open the 12 clips in FL Studio with the same instrument, preset, volume and effects. "
        "The filenames are neutral clip IDs; they do not reveal why a clip was selected. "
        "No scores or spreadsheet are needed.\n\n"
        "After listening, send free-form notes using the clip ID (for example, clip_03):\n\n"
        "1. Which melody stayed in your memory, and what exactly: motif, rhythm or contour?\n"
        "2. Where does a melody feel random, awkward or like it stumbles?\n"
        "3. Does the ending feel complete when the loop returns to its start?\n\n"
        "Write impressions before opening the separate answer key. It maps clip IDs to seeds, variations and diagnostic selection roles.\n"
    )
    (pack / "LISTENING_PROMPT.md").write_text(prompt, encoding="utf-8")
    print("BLIND_LISTENING_PACK validated=%d" % len(midi_files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="create an anonymous export CSV and separate answer key")
    prep.add_argument("--selection", type=Path, required=True)
    prep.add_argument("--export-selection", type=Path, required=True)
    prep.add_argument("--answer-key", type=Path, required=True)
    prep.add_argument("--expected-items", type=int, default=12)
    prep.set_defaults(func=prepare)
    done = sub.add_parser("finalize", help="validate the exported clip names and write an anonymous prompt")
    done.add_argument("--pack-dir", type=Path, required=True)
    done.add_argument("--expected-items", type=int, default=12)
    done.set_defaults(func=finalize)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
