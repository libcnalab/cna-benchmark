# cna-benchmark

A 3DMark-style benchmark suite for CNA. See `README.md` for what it is and `docs/scoring.md` for
how the score is computed.

## Build rules

The openeggbert build rules in the parent directory's `CLAUDE.md` apply in full: reuse `build/`,
never build in the scratchpad or under `/tmp`, always pass ccache, and use the whole machine.

Configuring this project builds the whole of CNA and sharp-runtime, so a discarded build tree is
expensive. There is one build directory:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cna-benchmark -j$(nproc)
```

ccache is on by default here (`CNA_BENCHMARK_USE_CCACHE`), and the project sets
`CMAKE_CXX_COMPILER_LAUNCHER` for CNA and sharp-runtime too.

Release, not Debug. A Debug build of a benchmark measures the optimiser.

## Upstream checkouts

This project consumes the **next** development lines, as siblings:

- `../cnanext` — override with `-DCNA_ROOT_DIR`
- `../sharp-runtimenext` — override with `-DCNA_BENCHMARK_SHARP_RUNTIME_ROOT`

Both are separate git checkouts, not submodules.

## Rules the suite itself lives by

These are not style preferences; each one is load-bearing for whether a published number means
anything.

1. **No renderer-specific code path, anywhere.** Scenes are written against the public XNA 4.0
   surface. Whether a renderer can run a scene is answered by `GraphicsDevice::SupportsCapability`,
   never by testing the renderer's name.
2. **A scene never reads the clock and never adapts.** Its workload is fixed at load time and its
   animation advances a fixed 1/60 s per rendered frame. A fast renderer and a slow one must draw
   the same frames, or they are not running the same benchmark.
3. **No content on disk.** Every texture and mesh is generated from `--seed` with the suite's own
   xorshift generator — not `rand()` and not `std::mt19937`, whose output differs between standard
   libraries.
4. **Never discard an outlier.** Every sample is kept; the score uses the median and the
   distribution is published alongside it.
5. **Changing a reference constant invalidates every published score.** Bump
   `kReferenceProfileId` with it. `tools/compare-results.py` refuses to mix profiles.
6. **A number that cannot be compared must say so.** When in doubt, report the measurement and
   withhold the comparability claim, with the reason in English.

## Adding a scene

Implement `Scene` in `src/scenes/`, register it in `SceneRegistry.cpp`, add its reference entry in
`Scoring.cpp`, and add its `detail` keys to `docs/result-schema.md`. Adding a scene changes the
composite, so it needs a new reference-profile id.

## Tests

```bash
DISPLAY=:0 ctest --test-dir build --output-on-failure
```

Structural only. No test asserts a performance number — the machine a test runs on is not the
machine a score is calibrated for.
