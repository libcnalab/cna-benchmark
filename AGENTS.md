AI must not be used to generate code for contributions to this project.

"AI" in this case means a Large Language Model ("LLM"), such as ChatGPT,
Claude, Copilot, Grok, etc.

AI-generated code is based upon sources of unknown origins and may not be 
compatible with the licenses applicable to this project, or may introduce 
conflicting license terms if it includes code from other projects.

AI can be used to identify issues with contributions to this project, but the
solutions to those issues should be authored by humans.

We have found that AI will frequently hallucinate issues that are not actually
problems in practice, report incorrect information, and describe problems that
are actually not issues at all. If AI identifies a problem with this codebase,
please make sure you understand what it is saying and have independently
confirmed that the issue exists before submitting a bug report or pull request.

By submitting a pull request to this project, you confirm that you are the
author of your changes, have the necessary rights to contribute them, and
comply with all applicable licensing requirements of this project.

# cna-benchmark

A 3DMark-style benchmark suite for CNA. See `README.md` for what it is and `docs/scoring.md` for
how the score is computed.

## Build rules

Reuse `build/`, keep build trees outside `/tmp`, and use ccache when available. CNA's build rule
limits compilation to at most eight parallel jobs.

Configuring this project builds the whole of CNA and sharp-runtime, so a discarded build tree is
expensive. There is one build directory:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cna-benchmark --parallel 8
```

ccache is on by default here (`CNA_BENCHMARK_USE_CCACHE`), and the project sets
`CMAKE_CXX_COMPILER_LAUNCHER` for CNA and sharp-runtime too.

Release, not Debug. A Debug build of a benchmark measures the optimiser.

## Upstream checkouts

This project prefers the **next** development lines, as siblings, and falls back to the current
`../../libcna` checkouts:

- `../cnanext` or `../../libcna/cna` — override with `-DCNA_ROOT_DIR`
- `../sharp-runtimenext` or `../../libcna/sharp-runtime` — override with
  `-DCNA_BENCHMARK_SHARP_RUNTIME_ROOT`

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
<CNA_ROOT_DIR>/tools/platform/run_gpu_tests_private.sh build --output-on-failure
```

Structural only. No test asserts a performance number — the machine a test runs on is not the
machine a score is calibrated for.
