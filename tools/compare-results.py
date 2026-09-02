#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Puts several cna-benchmark result documents side by side.

    tools/compare-results.py results/*.json
    tools/compare-results.py --csv results/*.json > comparison.csv

Refuses to mix results calibrated against different reference profiles, and marks every
column whose run declared itself non-comparable, because a table is exactly where that
qualification gets lost.
"""

import argparse
import csv
import json
import sys

SCENE_IDS = [
    "sprites",
    "particles",
    "skeletal",
    "terrain",
    "lights",
    "instancing",
    "streaming",
]


def load(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as error:
        sys.exit(f"compare-results: cannot read {path}: {error}")
    except json.JSONDecodeError as error:
        sys.exit(f"compare-results: {path} is not valid JSON: {error}")


def short(value):
    """Formats a magnitude the way the benchmark's own console output does."""
    magnitude = abs(value)
    sign = "-" if value < 0 else ""
    for limit, suffix in ((1e12, "T"), (1e9, "G"), (1e6, "M"), (1e3, "k")):
        if magnitude >= limit:
            return f"{sign}{magnitude / limit:.2f} {suffix}"
    return f"{sign}{magnitude:.0f}"


def grouped(value):
    return f"{int(round(value)):,}"


def main():
    parser = argparse.ArgumentParser(description="Compare cna-benchmark results.")
    parser.add_argument("results", nargs="+", help="result JSON documents")
    parser.add_argument(
        "--csv", action="store_true", help="emit CSV instead of a text table"
    )
    parser.add_argument(
        "--metric",
        choices=("score", "throughput", "fps", "frametime"),
        default="score",
        help="what to put in the cells (default: score)",
    )
    arguments = parser.parse_args()

    documents = [(path, load(path)) for path in arguments.results]

    profiles = {document.get("referenceProfile") for _, document in documents}
    if len(profiles) > 1:
        sys.exit(
            "compare-results: these results were scored against different reference "
            f"profiles ({', '.join(sorted(str(p) for p in profiles))}). Their scores are "
            "expressed in different units and must not be tabulated together."
        )

    columns = []
    for path, document in documents:
        renderer = document.get("renderer", {}).get("active", "?")
        score = document.get("score", {})
        label = document.get("settings", {}).get("label")
        name = renderer if not label else f"{renderer} ({label})"
        if len({d.get("renderer", {}).get("active") for _, d in documents}) < len(documents):
            name = f"{name} [{path}]"
        columns.append(
            {
                "name": name,
                "path": path,
                "document": document,
                "score": score.get("cnaScore", 0),
                "comparable": bool(score.get("comparable", False)),
                "notes": score.get("comparabilityNotes", []),
                "probe": document.get("renderer", {})
                .get("outputProbe", {})
                .get("verdict", "unknown"),
                "scenes": {
                    scene.get("id"): scene for scene in document.get("scenes", [])
                },
            }
        )

    def cell(scene):
        if scene is None or scene.get("outcome") != "measured":
            outcome = "-" if scene is None else scene.get("outcome")
            return {"unsupported": "n/s", "failed": "FAIL", "not-selected": "-"}.get(
                outcome, "-"
            )
        if arguments.metric == "score":
            return grouped(scene["score"])
        if arguments.metric == "throughput":
            return short(scene["throughput"])
        if arguments.metric == "fps":
            return f"{scene['fps']:.1f}"
        return f"{scene['frameTime']['meanMs']:.3f}"

    if arguments.csv:
        writer = csv.writer(sys.stdout)
        writer.writerow(["scene"] + [column["name"] for column in columns])
        for scene_id in SCENE_IDS:
            writer.writerow(
                [scene_id] + [cell(column["scenes"].get(scene_id)) for column in columns]
            )
        writer.writerow(
            ["CNA Score"] + [grouped(column["score"]) for column in columns]
        )
        writer.writerow(
            ["comparable"] + ["yes" if column["comparable"] else "no" for column in columns]
        )
        writer.writerow(["output probe"] + [column["probe"] for column in columns])
        return 0

    first = documents[0][1]
    print(f"CNA Benchmark comparison -- reference profile {first.get('referenceProfile')}")
    print(f"Metric: {arguments.metric}\n")

    # The widest thing any cell will hold, not just the widest column name: the output-probe
    # row carries "not-verifiable", which ran into its neighbour when only names were measured.
    width = max(
        12,
        max(len(column["name"]) for column in columns) + 2,
        max(len(column["probe"]) for column in columns) + 2,
    )
    header = "  " + "scene".ljust(16) + "".join(c["name"].rjust(width) for c in columns)
    print(header)
    print("  " + "-" * (len(header) - 2))

    for scene_id in SCENE_IDS:
        row = "  " + scene_id.ljust(16)
        row += "".join(cell(c["scenes"].get(scene_id)).rjust(width) for c in columns)
        print(row)

    print("  " + "-" * (len(header) - 2))
    print(
        "  "
        + "CNA Score".ljust(16)
        + "".join(grouped(c["score"]).rjust(width) for c in columns)
    )
    print(
        "  "
        + "output probe".ljust(16)
        + "".join(c["probe"].rjust(width) for c in columns)
    )
    print()

    print("  n/s = the renderer does not support that scene; FAIL = the scene threw.")
    print(
        "  output probe: 'verified' means a drawn colour was read back from this renderer.\n"
        "  'no-output' means it accepted the draws and wrote nothing; 'not-verifiable' means\n"
        "  it has no readback path, so neither could be established.\n"
    )

    for column in columns:
        if column["comparable"]:
            continue
        print(f"  {column['name']}: NOT comparable")
        for note in column["notes"]:
            print(f"      - {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
