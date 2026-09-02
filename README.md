# CNA Benchmark Suite

A 3DMark-style benchmark for [CNA](https://github.com/openeggbert/cna), the C++ reimplementation
of the XNA 4.0 programming model. It runs seven fixed graphics workloads, scores each against a
published reference profile, and reduces them to a single number:

```
  CNA Score: 19,849
```

Every run also writes a JSON document, so the same binary can benchmark several CNA renderers and
the results can be put side by side afterwards.

The suite is a plain CNA application. It is written against the public XNA 4.0 surface plus the
handful of CNAEXT accessors that report which renderer is running and what it supports — no
renderer-specific code path exists anywhere in it, which is what makes a comparison between two
renderers a comparison of the renderers.

It consumes the **`cnanext`** and **`sharp-runtimenext`** development lines of both projects.

---

## The scenes

| Scene | What it measures | Work unit | Needs |
|---|---|---|---|
| `sprites` | The whole `SpriteBatch` path: per-sprite CPU work, quad generation, batching, and holding one texture across thousands of draws. Every sprite moves, rotates and scales each frame. | sprites | 2D only |
| `particles` | A fixed-step CPU particle simulation plus the additive overdraw of drawing every particle. Two costs a particle system really pays. | particles | 2D only |
| `skeletal` | GPU skinning end to end: 64-bone palettes re-evaluated and uploaded every frame, four weighted influences per vertex. | skinned vertices | 3D |
| `terrain` | Sustained indexed triangle throughput with real per-pixel work: a tiled height field submitted in full every frame, with no culling. | triangles | 3D |
| `lights` | Twelve moving directional lights over one cube field, accumulated across four additive `BasicEffect` passes. | triangle-lights | 3D, depth buffer |
| `instancing` | `DrawInstancedPrimitives`: one mesh, one draw call, tens of thousands of copies from a per-instance matrix stream. | instances | 3D, instancing |
| `streaming` | Per-frame upload bandwidth: a full texture surface plus a discarded dynamic vertex buffer, both rewritten *and redrawn* every frame. | bytes | 3D |

`cna-benchmark --list-scenes` prints the same table with each scene's full description.

A renderer that lacks a capability a scene needs **skips that scene and says which capability was
missing**. The result is reported, the run is marked incomplete, and its score is flagged
non-comparable — a partial run is a perfectly good measurement and a meaningless comparison.

---

## Building

Requires CMake 3.21+, a C++23 compiler, SDL3, and sibling checkouts of `cnanext` and
`sharp-runtimenext`:

```
<parent>/
  cnanext/
  sharp-runtimenext/
  cna-benchmark/      <- this repository
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cna-benchmark -j$(nproc)
```

Release matters. A Debug build measures the optimiser, not the renderer.

Point the build elsewhere with `-DCNA_ROOT_DIR=...` and
`-DCNA_BENCHMARK_SHARP_RUNTIME_ROOT=...`.

### Choosing which renderers to compile in

By default the binary carries five:

```
OPENGLES3  SDL_RENDERER  SOFTWARE  HEADLESS  STUB
```

which is what a stock desktop checkout can build with no extra SDKs, and spans the interesting
classes: a real GPU renderer, a native 2D renderer, a CPU rasterizer, and the two no-output
renderers that isolate CNA's own CPU cost from any drawing at all.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCNA_BENCHMARK_RENDERERS="OPENGL33;VULKAN;BGFX;SOFTWARE"
```

The **first** entry is the build default. Only one OpenGL-profile identity may appear —
`OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1` and `WEBGL2` are served by one implementation
family and cannot be compiled twice into the same binary. CNA rejects an impossible combination
at configure time and says why.

One build tree serves the whole comparison. That is the point: *N* single-renderer build trees
are *N* full CNA builds.

---

## Running

```bash
./build/cna-benchmark                      # the build's default renderer
./build/cna-benchmark --renderer=SOFTWARE  # any renderer compiled in
./build/cna-benchmark --list-renderers
```

The renderer is chosen before any `GraphicsDevice` exists, through
`CNA::GraphicsRendererSelection`. CNA latches the choice the moment the device is created, so one
process benchmarks one renderer — which is why the sweep below runs one process per renderer.

### Comparing every compiled renderer

```bash
tools/run-all-renderers.sh build results
```

runs the suite once per renderer and prints a table. Anything after `--` is passed to every run,
which is how a sweep including the CPU rasterizer stays finite — `SOFTWARE` draws some of these
scenes at about one frame a second:

```bash
tools/run-all-renderers.sh build results -- --max-scene-seconds=20
```

A scene the budget cuts short says so, and the run is reported as non-comparable. Compare saved results at any time:

```bash
tools/compare-results.py results/*.json
tools/compare-results.py --metric=throughput results/*.json
tools/compare-results.py --csv results/*.json > comparison.csv
```

### Useful options

```
--frames=N            measured frames per scene (default 300)
--warmup=N            untimed frames before each measured window (default 60)
--width / --height    back-buffer size (default 1280x720)
--workload-scale=F    multiply every scene's workload
--max-scene-seconds=N cap the total time one scene may take, warm-up included
--quick               --frames=90 --warmup=20
--json=PATH           where the result document goes
--json-stdout         print the document to stdout; the report moves to stderr,
                      so `cna-benchmark --json-stdout | jq ...` works
--samples             include every per-frame sample in it
--hud                 draw the on-screen overlay
--strict              exit non-zero unless every selected scene was measured
```

`cna-benchmark --help` has the complete list.

---

## What the score means

Each scene's throughput is divided by that scene's entry in the reference profile, giving an
**index** — how many times reference speed. The **CNA Score** is 1000 times the geometric mean of
those indices.

```
scene index = measured throughput / reference throughput
CNA Score   = round(1000 x geometric mean of the seven indices)
```

A geometric mean, not an arithmetic one, so no single scene can dominate: a renderer twice as fast
everywhere scores twice as much, while a renderer twice as fast in one scene of seven gains about
10%.

The reference constants are a **scale, not a claim**. They were calibrated so that one particular
machine scores about 20,000, and they carry no meaning beyond fixing the units both sides of a
comparison are expressed in. `docs/scoring.md` gives the constants, the calibration machine, and
how they were derived.

### The honesty machinery

A benchmark's hardest problem is not measuring; it is not reporting a number that means less than
it looks like it means. This suite refuses to call a score comparable, and says exactly why, when:

- **not every scene was measured** — a renderer that skipped the 3D scenes cannot be put next to
  one that ran them;
- **the renderer could not be shown to produce pixels** — before the first scene, the suite draws
  a known colour into an off-screen target and reads it back. CNA's `HEADLESS` renderer reports the
  complete 3D pipeline as supported and is not lying — it validates every call and maintains every
  counter — but it rasterizes nothing, and unchecked it tops the table by a factor of ten. A
  capability query cannot tell that renderer from a real one. A pixel can;
- **the frame times scattered** — if the slowest 5% of frames took more than four times the
  median, something else was using the machine;
- **the settings were changed** — a scaled workload, vsync on, the overlay drawn inside the
  measured frames, or fewer than 120 frames per scene.

The numbers are always reported in full. Only the claim that they can be compared is withheld.

### Frame timing

Two clocks per frame, both recorded:

- **frame time** — the wall-clock gap between successive frame starts. The real frame period,
  including presentation, the host loop and any wait the driver imposed.
- **submission time** — CPU time inside the scene's own drawing code.

The score uses the **median** frame time. A desktop run settles into a steady period and then
loses several milliseconds, a handful of times in three hundred frames, to the compositor or the
scheduler. Those hitches are real and the suite reports them — `meanFps`, `p95Ms`, `p99Ms` and
`onePercentLowFps` are all in the JSON — but they are a property of the host, and letting six
outliers decide the headline would make the score a measurement of how quiet the desktop happened
to be. Median-scored runs on the calibration machine repeat within 2–7%; mean-scored runs on the
same machine varied by 60%.

---

## Content

There is none. Every texture and every mesh is generated from `--seed` with the suite's own
xorshift generator, so two runs on two renderers draw byte-identical geometry and sample
byte-identical texels, and the benchmark runs from a bare build tree on a machine with no assets.

The animation clock advances a fixed 1/60 s per rendered frame and never reads the wall clock. A
fast renderer and a slow one therefore draw the *same* frames — without that, they would not be
running the same benchmark.

---

## Results

`docs/result-schema.md` documents the JSON. Every run records the active renderer and its complete
capability profile, the CNA and Sharp Runtime versions, the compiler, the OS, the settings, and per
scene: throughput, index, score, the full frame-time distribution, and scene-specific detail such
as draw calls and bytes uploaded per frame.

The machine name is **not** recorded unless `--include-host` is given.

---

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Argument handling, the two listing commands, rejection of bad input, a complete end-to-end run on
a renderer that needs no GPU and no window, and a structural check of the result document it
produces. Deliberately no test asserts a performance number: the machine a test runs on is not the
machine a score is calibrated for.

---

## Licence

MS-PL, matching CNA. See `LICENSE`.
