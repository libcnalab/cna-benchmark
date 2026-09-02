# The result document

`cna-benchmark` writes one JSON document per run (`--json=PATH`, default
`cna-benchmark-result.json`). `schema` is `1`; it is incremented for any change that would break a
reader.

Sizes and units are stated everywhere they could be ambiguous. Times are milliseconds, rates are
per second, and SI prefixes in the console output are decimal.

## Top level

| Field | Type | Meaning |
|---|---|---|
| `schema` | int | Result-schema version. |
| `suite` | string | Always `"CNA Benchmark Suite"`. |
| `benchmarkVersion` | string | The benchmark's own version. |
| `referenceProfile` | string | Which calibration constants the scores are expressed in, e.g. `cna-bench-ref-v1`. Results from two different profiles must not be tabulated together. |
| `startedAt` | string | ISO-8601 round-trip timestamp with UTC offset. |
| `score` | object | The headline result. |
| `renderer` | object | Which renderer ran, and what it can do. |
| `environment` | object | Build and machine. |
| `settings` | object | Everything the numbers depend on. |
| `scenes` | array | One entry per registered scene, always all seven, in suite order. |

## `score`

| Field | Type | Meaning |
|---|---|---|
| `cnaScore` | int | 1000 x the geometric mean of the measured scenes' indices. |
| `compositeIndex` | number | That geometric mean, un-scaled. |
| `measuredScenes` | int | Scenes that produced a score. |
| `selectedScenes` | int | Scenes `--scenes` selected. |
| `complete` | bool | Every scene in the suite was measured. |
| `comparable` | bool | This score may be compared with another run's. |
| `comparabilityNotes` | array of string | Every reason `comparable` is false, in English. |
| `measuredSeconds` | number | Wall-clock seconds inside measured windows, warm-up excluded. |

**Read `comparable` before reading `cnaScore`.** A run can be perfectly valid as a measurement and
meaningless as a comparison; `docs/scoring.md` lists every cause.

## `renderer`

| Field | Type | Meaning |
|---|---|---|
| `active` | string | The renderer that ran, e.g. `OPENGLES3`. |
| `buildDefault` | string | The renderer this build would use with no `--renderer`. |
| `multiRendererBuild` | bool | Whether the binary carries more than one. |
| `compiledIn` | array of string | Every renderer identity in the binary. |
| `features` | array of object | `{feature, support, note?}` for every entry of `CNA::RendererFeature`, in CNA's stable order. `support` is `Supported`, `Unsupported`, `Restricted` or `Unknown`. |
| `additionalLimitations` | string | The renderer's own English qualifications, when it gave any. |
| `outputProbe` | object | `{verdict, detail}`. `verdict` is `verified`, `no-output` or `not-verifiable`; `detail` names the colour drawn and the colour read back, or why neither readback route was available. |

`features` is the machine-readable record of *why* a scene was skipped, straight from CNA's own
capability profile rather than from anything this benchmark inferred.

## `environment`

`cnaVersion`, `sharpRuntimeVersion`, `compiler`, `buildType`, `operatingSystem`,
`processorCount`, `processBits`, and `machineName` — the last present **only** when
`--include-host` was given.

## `settings`

`width`, `height`, `measuredFrames`, `warmupFrames`, `workloadScale`, `maxSceneSeconds`, `vsync`,
`hud`, `seed`, and `label` when `--label` was given.

`width` and `height` are the back buffer that actually existed, which is not necessarily the one
requested: a renderer or a window manager may hand back a different size, and every scene composes
for the size it was given.

## `scenes[]`

Always seven entries, in suite order, including scenes that did not run.

| Field | Type | Meaning |
|---|---|---|
| `id` | string | `sprites`, `particles`, `skeletal`, `terrain`, `lights`, `instancing`, `streaming`. |
| `title` | string | Human-readable name. |
| `outcome` | string | `measured`, `unsupported`, `not-selected` or `failed`. |
| `note` | string | Present unless `measured`: which capability was missing, or what threw. |

The fields below appear **only** when `outcome` is `measured`:

| Field | Type | Meaning |
|---|---|---|
| `workUnit` | string | What this scene counts. |
| `workUnitsPerFrame` | number | Work in one frame, at this run's workload scale. |
| `measuredFrames` | int | Frames in the measured window. |
| `warmupFrames` | int | Untimed frames actually rendered, which a time budget may have shortened. |
| `truncated` | bool | `--max-scene-seconds` ended the scene before it was full. |
| `fps` | number | `1000 / frameTime.medianMs`. The scored frame rate. |
| `meanFps` | number | `1000 / frameTime.meanMs`, host hitches included. |
| `onePercentLowFps` | number | `1000 / frameTime.p99Ms`. The standard stutter figure. |
| `throughput` | number | `workUnitsPerFrame * fps`, in `workUnit` per second. |
| `referenceThroughput` | number | The reference profile's entry for this scene. |
| `index` | number | `throughput / referenceThroughput`. |
| `score` | int | `round(1000 * index)`. |
| `frameTimeSpread` | number | `p95Ms / medianMs`. Above 4 the run is reported non-comparable. |
| `frameTime` | object | Distribution of the real frame period. |
| `submissionTime` | object | Distribution of CPU time inside the scene's own drawing code. |
| `detail` | object | Scene-specific numbers — see below. |
| `samples` | object | `{frameTimeMs, submissionTimeMs}`, present only with `--samples`. |

### Distribution objects

`samples`, `meanMs`, `minMs`, `medianMs`, `p95Ms`, `p99Ms`, `maxMs`, `stdDevMs`. Percentiles use
linear interpolation — the definition NumPy and `PERCENTILE.INC` use — so they can be re-derived
from `samples` in a spreadsheet. No sample is ever discarded as an outlier.

### `detail`, per scene

| Scene | Keys |
|---|---|
| `sprites` | `spritesPerFrame`, `atlasTiles`, `spriteBatchesPerFrame` |
| `particles` | `particlesPerFrame`, `emitters`, `genuineAdditiveBlending` |
| `skeletal` | `characters`, `bonesPerCharacter`, `verticesPerCharacter`, `trianglesPerFrame`, `boneMatrixUploadsPerFrame` |
| `terrain` | `chunks`, `cellsPerChunkAxis`, `trianglesPerFrame`, `drawCallsPerFrame` |
| `lights` | `cubes`, `lights`, `lightPasses`, `trianglesPerFrame`, `drawCallsPerFrame` |
| `instancing` | `instances`, `trianglesPerInstance`, `trianglesPerFrame`, `drawCallsPerFrame` |
| `streaming` | `textureEdge`, `textureBytesPerFrame`, `dynamicVertices`, `vertexBytesPerFrame` |

`genuineAdditiveBlending` is `1` when CNA reports this renderer's `BlendState::Additive` as its
real additive path and `0` when the renderer silently degrades to ordinary source-over. The
particle scene runs either way — that is still real blending work — but two runs that blended
differently are not a fair pair.

## Example

```json
{
  "id": "terrain",
  "title": "Terrain",
  "outcome": "measured",
  "workUnit": "triangles",
  "workUnitsPerFrame": 294912.0,
  "measuredFrames": 300,
  "fps": 1788.08,
  "meanFps": 1498.57,
  "onePercentLowFps": 502.82,
  "throughput": 527326337.17,
  "referenceThroughput": 25000000.0,
  "index": 21.093,
  "score": 21093,
  "frameTimeSpread": 1.637,
  "frameTime": {
    "samples": 300, "meanMs": 0.667, "minMs": 0.447, "medianMs": 0.559,
    "p95Ms": 0.915, "p99Ms": 1.989, "maxMs": 9.423, "stdDevMs": 0.727
  },
  "submissionTime": {
    "samples": 300, "meanMs": 0.334, "minMs": 0.258, "medianMs": 0.315,
    "p95Ms": 0.469, "p99Ms": 0.543, "maxMs": 0.591, "stdDevMs": 0.057
  },
  "detail": {
    "chunks": 16.0, "cellsPerChunkAxis": 96.0,
    "trianglesPerFrame": 294912.0, "drawCallsPerFrame": 16.0
  }
}
```

## Reading results

`tools/compare-results.py` puts several documents side by side and refuses to mix reference
profiles. `tools/verify-result.py` checks a document's structure — it is what ctest runs after the
smoke run, and it asserts no performance number.
