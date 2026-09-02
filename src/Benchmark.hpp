// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Benchmark.hpp
 * @brief The vocabulary the whole suite shares: run configuration, the scene contract, and the
 *        result records a run produces.
 *
 * Nothing here knows about a specific renderer. A scene is handed a `GraphicsDevice` and a
 * workload scale and draws the same thing on every renderer; whether that renderer can express
 * the scene at all is answered before the scene is loaded, by CNA's own capability profile,
 * never by testing its name.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CNA/GraphicsCapability.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CnaBenchmark
{
    /** @brief Everything the command line decides about one run. */
    struct RunConfig
    {
        /** @brief Scene ids to run, in registry order; empty means every registered scene. */
        std::vector<std::string> sceneIds;
        /** @brief Renderer identity requested with `--renderer`; empty means the build default. */
        std::string rendererRequest;
        /** @brief Frames per scene that are timed and scored. */
        int measuredFrames = 300;
        /** @brief Untimed frames rendered before each scene's measured window. */
        int warmupFrames = 60;
        /** @brief Back-buffer width in pixels. */
        int width = 1280;
        /** @brief Back-buffer height in pixels. */
        int height = 720;
        /** @brief Whether presentation waits for vertical retrace. Scoring requires this off. */
        bool vsync = false;
        /** @brief Draw the on-screen overlay. Off by default: it would be timed with the scene. */
        bool hud = false;
        /** @brief Multiplies every scene's workload. 1.0 is the scored, comparable workload. */
        double workloadScale = 1.0;
        /** @brief Seed for the suite's own deterministic generator. */
        std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
        /** @brief Where the result document is written; empty writes no file. */
        std::string jsonPath = "cna-benchmark-result.json";
        /** @brief Also print the result document to stdout. */
        bool jsonToStdout = false;
        /** @brief Emit every per-frame sample into the result document. */
        bool emitSamples = false;
        /** @brief Fail the run when any scene could not be measured. */
        bool strict = false;
        /** @brief Include the machine name in the result document. Off by default. */
        bool includeHost = false;
        /** @brief Free-text label carried into the result document, e.g. a hardware note. */
        std::string label;
        /**
         * @brief Seconds one scene may take in total before it is cut short; 0 = no limit.
         *
         * Covers the warm-up as well as the measured window -- half the budget at most goes to
         * the warm-up -- because on a renderer slow enough to need this, the warm-up alone can
         * outlast a user's patience.
         *
         * Zero by default, because a limit that fired silently would make two runs of the same
         * suite mean different things. It exists for the renderers where the full window is
         * genuinely impractical: CNA's CPU rasterizer draws some of these scenes at about one
         * frame a second. A scene it truncates says so and withdraws comparability.
         */
        double maxSceneSeconds = 0.0;
    };

    /** @brief One renderer capability a scene cannot do without, and why it needs it. */
    struct SceneRequirement
    {
        /** @brief The capability queried through `GraphicsDevice::SupportsCapability`. */
        CNA::GraphicsCapability capability;
        /** @brief Stable English explanation printed and stored when the scene is skipped. */
        std::string_view reason;
    };

    /** @brief What a scene is given. Deliberately small: a device, a viewport and a workload. */
    struct SceneContext
    {
        /** @brief The active device. Never null while a scene is loaded. */
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device = nullptr;
        /** @brief Back-buffer width the scene should compose for. */
        int width = 0;
        /** @brief Back-buffer height the scene should compose for. */
        int height = 0;
        /** @brief Workload multiplier from `--workload-scale`. */
        double workloadScale = 1.0;
        /** @brief Seed for this run's deterministic generator. */
        std::uint64_t seed = 0;
    };

    /** @brief One named number a scene wants recorded next to its result. */
    using SceneDetail = std::pair<std::string, double>;

    /**
     * @brief One benchmark scene.
     *
     * A scene owns its own GPU resources, renders a fixed workload per frame, and reports how
     * much work one frame contains. It must not read the clock, keep frame-rate state, or
     * adapt its workload: every timing decision belongs to the runner, and an adaptive scene
     * cannot be compared across renderers.
     */
    class Scene
    {
    public:
        virtual ~Scene() = default;

        /** @brief Stable machine identity, e.g. `"sprites"`. Used by `--scenes` and the JSON. */
        [[nodiscard]] virtual std::string_view Id() const = 0;
        /** @brief Human-readable name for the console and the overlay. */
        [[nodiscard]] virtual std::string_view Title() const = 0;
        /** @brief One sentence describing what this scene actually measures. */
        [[nodiscard]] virtual std::string_view Description() const = 0;
        /** @brief The unit this scene counts, e.g. `"sprites"`, `"triangles"`, `"bytes"`. */
        [[nodiscard]] virtual std::string_view WorkUnitName() const = 0;

        /** @brief Capabilities without which this scene cannot run. Empty for 2D-only scenes. */
        [[nodiscard]] virtual std::vector<SceneRequirement> Requirements() const { return {}; }

        /** @brief Creates this scene's resources. @param context The run's device and viewport. */
        virtual void Load(const SceneContext& context) = 0;
        /** @brief Releases this scene's resources. Called even after a failed `Render`. */
        virtual void Unload() = 0;

        /**
         * @brief Renders exactly one frame of this scene's fixed workload.
         * @param context     The run's device and viewport.
         * @param frameIndex  Frames rendered so far in this scene, warm-up included.
         * @param sceneTime   A deterministic animation clock in seconds, derived from
         *                    @p frameIndex rather than from the wall clock, so the geometry a
         *                    frame draws does not depend on how fast the renderer is.
         */
        virtual void Render(const SceneContext& context, int frameIndex, double sceneTime) = 0;

        /** @brief Work units in one rendered frame; the numerator of this scene's throughput. */
        [[nodiscard]] virtual double WorkUnitsPerFrame() const = 0;

        /** @brief Extra numbers worth recording, e.g. draw calls or bytes uploaded per frame. */
        [[nodiscard]] virtual std::vector<SceneDetail> Detail() const { return {}; }
    };

    /** @brief Distribution of one timed quantity over a measured window, in milliseconds. */
    struct FrameStats
    {
        /** @brief Number of samples behind these statistics. */
        int count = 0;
        /** @brief Arithmetic mean in milliseconds. */
        double meanMs = 0.0;
        /** @brief Smallest sample in milliseconds. */
        double minMs = 0.0;
        /** @brief Largest sample in milliseconds. */
        double maxMs = 0.0;
        /** @brief Median sample in milliseconds. */
        double medianMs = 0.0;
        /** @brief 95th percentile in milliseconds. */
        double p95Ms = 0.0;
        /** @brief 99th percentile in milliseconds. */
        double p99Ms = 0.0;
        /** @brief Population standard deviation in milliseconds. */
        double stdDevMs = 0.0;
    };

    /** @brief Why a scene produced no measurement. */
    enum class SceneOutcome
    {
        /** @brief The scene ran to completion and carries a score. */
        Measured,
        /** @brief The active renderer lacks a capability the scene requires. */
        Unsupported,
        /** @brief The scene was not selected by `--scenes`. */
        NotSelected,
        /** @brief The scene threw while loading or rendering. */
        Failed
    };

    /** @brief Everything one scene contributes to the result document. */
    struct SceneResult
    {
        /** @brief The scene's stable id. */
        std::string id;
        /** @brief The scene's human-readable name. */
        std::string title;
        /** @brief The unit `workUnitsPerFrame` and `throughput` count. */
        std::string workUnit;
        /** @brief How this scene ended. */
        SceneOutcome outcome = SceneOutcome::NotSelected;
        /** @brief Stable English explanation when `outcome` is not `Measured`. */
        std::string note;
        /** @brief Work units in one frame at the run's workload scale. */
        double workUnitsPerFrame = 0.0;
        /** @brief Frames in the measured window. */
        int measuredFrames = 0;
        /** @brief Untimed frames actually rendered, which a time budget may have shortened. */
        int warmupFrames = 0;
        /** @brief True when `--max-scene-seconds` ended the scene before it was full. */
        bool truncated = false;
        /** @brief Wall-clock gap between successive frame starts: the real frame period. */
        FrameStats endToEnd;
        /** @brief CPU time spent inside this scene's own `Render`, presentation excluded. */
        FrameStats submission;
        /**
         * @brief Frames per second derived from the MEDIAN frame period.
         *
         * The median, not the mean, and the reason is visible in any desktop run's samples: a
         * scene settles into a steady period and then, a handful of times in three hundred
         * frames, loses several milliseconds to the compositor or the scheduler. Those hitches
         * are real and this suite reports them -- in `meanFps`, `p95`, `p99` and
         * `onePercentLowFps` -- but they are a property of the host, and letting six outliers
         * decide the headline number would make the score a measurement of how quiet the
         * desktop happened to be.
         */
        double fps = 0.0;
        /** @brief Frames per second derived from the mean frame period, hitches included. */
        double meanFps = 0.0;
        /**
         * @brief The frame rate of the slowest one per cent of frames: 1000 / `p99Ms`.
         *
         * The standard way of reporting stutter, and the number a smooth-looking average can
         * hide completely.
         */
        double onePercentLowFps = 0.0;
        /**
         * @brief `p95Ms / medianMs`: how much slower the slowest twentieth of frames were.
         *
         * A robust dispersion measure, unlike a standard deviation, which a single 8 ms hitch
         * in three hundred 0.5 ms frames sends past 100% of the mean.
         */
        double frameTimeSpread = 0.0;
        /** @brief Work units per second: `workUnitsPerFrame * fps`. */
        double throughput = 0.0;
        /** @brief The reference profile's throughput for this scene. */
        double referenceThroughput = 0.0;
        /** @brief `throughput / referenceThroughput`; 1.0 is exactly reference speed. */
        double index = 0.0;
        /** @brief `round(1000 * index)`: the scene's own score. */
        long long score = 0;
        /** @brief Scene-supplied extra numbers. */
        std::vector<SceneDetail> detail;
        /** @brief Per-frame end-to-end samples in milliseconds; filled only with `--samples`. */
        std::vector<double> endToEndSamplesMs;
        /** @brief Per-frame submission samples in milliseconds; filled only with `--samples`. */
        std::vector<double> submissionSamplesMs;
    };

    /** @brief The complete outcome of a run. */
    struct RunResult
    {
        /** @brief Per-scene results in registry order, including scenes that did not run. */
        std::vector<SceneResult> scenes;
        /** @brief The headline CNA Score, or 0 when no scene was measured. */
        long long cnaScore = 0;
        /** @brief Geometric mean of the measured scenes' indices. */
        double compositeIndex = 0.0;
        /** @brief Scenes that produced a score. */
        int measuredSceneCount = 0;
        /** @brief Registered scenes that were selected for this run. */
        int selectedSceneCount = 0;
        /** @brief True only when every selected scene was measured at the scored settings. */
        bool complete = false;
        /**
         * @brief True when this run's settings make its score comparable with other runs.
         *
         * A run is comparable only at the published settings: the full scene set, workload
         * scale 1.0, vsync off, and no overlay. Anything else is a perfectly good measurement
         * and a meaningless comparison, so the flag travels with the number.
         */
        bool comparable = false;
        /** @brief Stable English list of the reasons `comparable` is false. */
        std::vector<std::string> comparabilityNotes;
        /** @brief Total wall-clock seconds spent measuring, warm-up excluded. */
        double measuredSeconds = 0.0;
    };
} // namespace CnaBenchmark
