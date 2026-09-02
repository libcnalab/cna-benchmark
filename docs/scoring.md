# How the CNA Score is computed

## The chain

Each scene reports one number: **work units per frame**, fixed at load time and never adapted.

```
throughput  = work units per frame  x  1000 / median frame time in ms
scene index = throughput / reference throughput
scene score = round(1000 x scene index)
CNA Score   = round(1000 x geometric mean of the scene indices)
```

Nothing else enters the number. There is no per-renderer adjustment, no bonus, no penalty, and no
term that depends on which renderer produced the throughput.

## Why the median frame time

Two clocks are taken every frame:

- **frame time** — the wall-clock gap between the start of one frame and the start of the next.
  This is the real frame period: presentation, the host loop's event handling and any wait the
  driver imposed are all inside it.
- **submission time** — CPU time spent inside the scene's own drawing code, which sees none of
  that.

The score uses the median of the frame times. Here is a real 200-frame window from the sprites
scene on the calibration machine, in milliseconds:

```
0.92 0.90 0.96 0.95 2.44 0.98 0.89 0.76 0.75 0.67 0.74 0.64 0.69 6.63 0.88 1.08 3.16 0.66 ...
... 0.50 0.49 0.50 0.49 0.49 0.49 0.50
```

Median 0.59 ms, mean 0.77 ms, 99th percentile 5.9 ms. The scene settles at about half a
millisecond and then, a handful of times, loses several milliseconds to the compositor or the
scheduler. Those hitches are real — and they are a property of the desktop, not of the renderer.

Scored on the mean, three consecutive runs of the same suite on the same machine disagreed by up to
60%. Scored on the median, they agree within 2% to 7%. The hitches are not hidden: `meanFps`,
`p95Ms`, `p99Ms` and `onePercentLowFps` are all in the result document, and a run whose slowest 5%
of frames exceeded four times the median is reported as non-comparable.

## Why a geometric mean

The composite is the weighted geometric mean of the seven indices, computed in log space (the
indices span several orders of magnitude across CNA's renderer set, and multiplying seven of them
before taking a root overflows a `double` long before the root brings it back).

A geometric mean is scale-invariant per scene: a renderer twice as fast everywhere scores twice as
much, while a renderer twice as fast in one scene of seven gains about 10%. With an arithmetic
mean, the scene with the largest raw numbers would decide the score on its own.

All seven weights are equal.

## The reference profile `cna-bench-ref-v1`

| Scene | Reference throughput | Unit |
|---|---:|---|
| `sprites` | 400,000 | sprites/s |
| `particles` | 550,000 | particles/s |
| `skeletal` | 2,250,000 | skinned vertices/s |
| `terrain` | 25,000,000 | triangles/s |
| `lights` | 17,500,000 | triangle-lights/s |
| `instancing` | 2,250,000 | instances/s |
| `streaming` | 27,500,000 | bytes/s |

These are a **scale, not a claim**. They fix the units a score is expressed in and nothing more.
Comparing two runs is comparing them with each other.

### How they were derived

Each constant is the median of three consecutive complete runs on the machine below, divided by
twenty and rounded to two significant figures — so that machine scores about 20,000 and a number in
the tens of thousands reads as "tens of times the reference baseline".

Three runs rather than one, because a single run cannot show whether a number is repeatable. These
agreed within 2% to 7%, which is what made them usable as a scale at all.

```
Calibration machine for cna-bench-ref-v1
  CPU     16 logical processors
  GPU     AMD Radeon 780M (integrated, RDNA 3), Mesa 25.0.7 radeonsi
  OS      Debian 13, Linux 6.12, X11
  CNA     OPENGLES3 renderer, 1280x720, vsync off, Release build
          60 warm-up + 300 measured frames per scene
```

The medians those constants came from were 8.00 M sprites/s, 10.9 M particles/s, 44.7 M skinned
vertices/s, 507 M triangles/s, 336 M triangle-lights/s, 42.7 M instances/s and 543 M bytes/s.

### Changing them

Changing any constant invalidates every previously published score, so the profile is versioned.
Bump `kReferenceProfileId` in `src/Scoring.hpp` with it; `tools/compare-results.py` refuses to
tabulate results from two different profiles rather than quietly mixing units.

## Scenes that did not run

A scene the renderer cannot draw contributes **nothing at all** to the composite — neither a zero
nor a one. A zero would drive the geometric mean to zero; a one would silently reward a renderer
for a scene it cannot draw. Both would let an incomplete run pass as a comparable one.

Instead the run's `complete` flag goes false, `comparable` goes false, and the reason is recorded
per scene. The remaining scenes' scores stay exactly as measured and are perfectly usable — for
comparing that renderer with itself over time, or with another renderer on the same subset.

## When a score is not comparable

`comparable` is false, with a note naming the cause, when any of these hold:

| Cause | Why it matters |
|---|---|
| A selected scene was not measured | The composite is over a different set of scenes |
| The renderer could not be shown to produce pixels | See below |
| The slowest 5% of frames exceeded 4x the median | Something else was using the machine |
| `--workload-scale` was not 1.0 | A different amount of work per frame |
| `--vsync` | The display caps the frame rate, not the renderer |
| `--hud` | The overlay is drawn inside the frames being measured |
| Fewer than 120 measured frames per scene | Too short to be stable |
| `--max-scene-seconds` cut a scene short | Fewer frames than the settings asked for |

The numbers are always reported in full. Only the claim that they can be compared is withheld.

## The output probe

Before the first scene, the suite draws a known colour — `rgb(218,61,137)`, chosen so that every
channel differs from every other — over a contrasting clear, into a 16x16 off-screen target, and
reads the centre pixel back. If no render target is available it repeats the test against the back
buffer, because several real renderers have one and not the other.

| Verdict | Meaning |
|---|---|
| `verified` | The drawn colour came back. This renderer really rasterizes. |
| `no-output` | The readback worked and the colour was not there. |
| `not-verifiable` | Neither readback route exists, so nothing could be established. |

This exists because CNA's `HEADLESS` renderer reports the complete 3D pipeline as supported, and it
is not lying: it accepts every draw call, validates every argument and maintains every counter the
contract promises. It simply produces no pixels. Unchecked, it runs all seven scenes, finishes each
frame in microseconds, and tops the comparison table by a factor of ten.

A capability query cannot tell that renderer from a real one — `ThreeDimensionalPipeline`,
`DepthStencilBuffer` and `InstancedDrawing` all come back `Supported`. A pixel can.

`not-verifiable` is not an accusation. A renderer with no readback path may be drawing perfectly.
But this suite cannot tell it apart from one drawing nothing, so it declines to present the score
as a comparable graphics result and says exactly why, instead of publishing a number it cannot
stand behind.
