#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Checks that a cna-benchmark result document has the shape the suite promises.

Run by ctest after the smoke run. It is deliberately a *structural* check and never asserts a
performance number: the machine a test runs on is not the machine a score is calibrated for,
and a benchmark whose test suite demands a particular speed fails for the wrong reason.
"""

import json
import math
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

OUTCOMES = {"measured", "unsupported", "not-selected", "failed"}

STATS_FIELDS = [
    "samples",
    "meanMs",
    "minMs",
    "medianMs",
    "p95Ms",
    "p99Ms",
    "maxMs",
    "stdDevMs",
]


def fail(message):
    print(f"verify-result: {message}", file=sys.stderr)
    sys.exit(1)


def require(container, key, kind, where):
    if key not in container:
        fail(f"{where} is missing '{key}'")
    value = container[key]
    if not isinstance(value, kind):
        fail(f"{where}.{key} should be {kind}, got {type(value).__name__}")
    return value


def check_stats(stats, where):
    for field in STATS_FIELDS:
        value = require(stats, field, (int, float), where)
        if isinstance(value, float) and not math.isfinite(value):
            fail(f"{where}.{field} is not a finite number")
        if value < 0:
            fail(f"{where}.{field} is negative")
    if stats["minMs"] > stats["meanMs"] + 1e-9:
        fail(f"{where}: the minimum frame time exceeds the mean")
    if stats["maxMs"] + 1e-9 < stats["meanMs"]:
        fail(f"{where}: the maximum frame time is below the mean")
    if stats["medianMs"] > stats["p95Ms"] + 1e-9:
        fail(f"{where}: the median exceeds the 95th percentile")
    if stats["p95Ms"] > stats["p99Ms"] + 1e-9:
        fail(f"{where}: the 95th percentile exceeds the 99th")


def main(argv):
    if len(argv) != 2:
        fail("usage: verify-result.py <result.json>")

    try:
        with open(argv[1], "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except OSError as error:
        fail(f"cannot read {argv[1]}: {error}")
    except json.JSONDecodeError as error:
        fail(f"{argv[1]} is not valid JSON: {error}")

    require(document, "schema", int, "document")
    require(document, "suite", str, "document")
    require(document, "benchmarkVersion", str, "document")
    require(document, "referenceProfile", str, "document")
    require(document, "startedAt", str, "document")

    score = require(document, "score", dict, "document")
    require(score, "cnaScore", (int, float), "score")
    require(score, "compositeIndex", (int, float), "score")
    require(score, "measuredScenes", int, "score")
    require(score, "selectedScenes", int, "score")
    require(score, "complete", bool, "score")
    require(score, "comparable", bool, "score")
    require(score, "comparabilityNotes", list, "score")

    renderer = require(document, "renderer", dict, "document")
    active = require(renderer, "active", str, "renderer")
    if not active:
        fail("renderer.active is empty")
    compiled = require(renderer, "compiledIn", list, "renderer")
    if not compiled:
        fail("renderer.compiledIn is empty")
    features = require(renderer, "features", list, "renderer")
    if not features:
        fail("renderer.features is empty")

    environment = require(document, "environment", dict, "document")
    for key in ("cnaVersion", "sharpRuntimeVersion", "compiler", "operatingSystem"):
        require(environment, key, str, "environment")

    settings = require(document, "settings", dict, "document")
    for key in ("width", "height", "measuredFrames", "warmupFrames"):
        require(settings, key, int, "settings")

    scenes = require(document, "scenes", list, "document")
    if [scene.get("id") for scene in scenes] != SCENE_IDS:
        fail(
            "scenes are not the suite's seven, in order: got "
            f"{[scene.get('id') for scene in scenes]}"
        )

    measured = 0
    for scene in scenes:
        where = f"scene '{scene.get('id')}'"
        outcome = require(scene, "outcome", str, where)
        if outcome not in OUTCOMES:
            fail(f"{where} has an unknown outcome '{outcome}'")
        if outcome != "measured":
            continue

        measured += 1
        for key in ("workUnit",):
            require(scene, key, str, where)
        for key in (
            "workUnitsPerFrame",
            "fps",
            "throughput",
            "referenceThroughput",
            "index",
            "meanFps",
            "onePercentLowFps",
            "frameTimeSpread",
        ):
            value = require(scene, key, (int, float), where)
            if not math.isfinite(float(value)):
                fail(f"{where}.{key} is not a finite number")
            if value < 0:
                fail(f"{where}.{key} is negative")
        require(scene, "score", (int, float), where)
        require(scene, "detail", dict, where)
        check_stats(require(scene, "frameTime", dict, where), f"{where}.frameTime")
        check_stats(
            require(scene, "submissionTime", dict, where), f"{where}.submissionTime"
        )

        expected = scene["workUnitsPerFrame"] * scene["fps"]
        if abs(expected - scene["throughput"]) > max(1.0, abs(expected) * 1e-6):
            fail(f"{where}.throughput is not workUnitsPerFrame * fps")

    if measured != score["measuredScenes"]:
        fail(
            f"score.measuredScenes says {score['measuredScenes']} but "
            f"{measured} scenes carry a measurement"
        )
    if measured == 0:
        fail("no scene was measured; the smoke run proved nothing")

    print(
        f"verify-result: {argv[1]} is well formed "
        f"({measured} measured scene(s), renderer {active})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
