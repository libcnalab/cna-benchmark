// SPDX-License-Identifier: MS-PL

#include "BenchmarkRunner.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>

#include "Format.hpp"
#include "OutputProbe.hpp"
#include "Report.hpp"
#include "SceneRegistry.hpp"
#include "Scoring.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief The animation clock advances at a fixed 60 steps per second. */
        constexpr double kSceneTimeStep = 1.0 / 60.0;

        /**
         * @brief The largest `p95 / median` frame-time spread a comparable run may show.
         *
         * Four, which sounds loose and is not: on a quiet desktop the slowest twentieth of
         * frames land within about twice the median even with the compositor interrupting, and
         * a machine genuinely busy with something else pushes this well past ten. The threshold
         * is a contention detector, not a demand for a real-time operating system.
         */
        constexpr double kMaxFrameTimeSpread = 4.0;

        double Milliseconds(std::chrono::steady_clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }
    }

    BenchmarkRunner::BenchmarkRunner(RunConfig config)
        : config_(std::move(config))
    {
        // Free-running, not fixed-step: a fixed time step would have CNA's own loop insert
        // sleeps between frames, and the measured frame period would be the target period
        // rather than the renderer's.
        setIsFixedTimeStepProperty(false);

        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(config_.width);
        deviceManager_->setPreferredBackBufferHeightProperty(config_.height);
        deviceManager_->setSynchronizeWithVerticalRetraceProperty(config_.vsync);

        info_ = CollectStaticRunInfo(config_.includeHost);

        scenes_ = CreateAllScenes();
        selected_.assign(scenes_.size(), config_.sceneIds.empty());
        for (std::size_t i = 0; i < scenes_.size(); ++i)
        {
            for (const std::string& wanted : config_.sceneIds)
            {
                if (scenes_[i]->Id() == wanted)
                {
                    selected_[i] = true;
                    break;
                }
            }
        }

        result_.scenes.resize(scenes_.size());
        for (std::size_t i = 0; i < scenes_.size(); ++i)
        {
            result_.scenes[i].id = std::string(scenes_[i]->Id());
            result_.scenes[i].title = std::string(scenes_[i]->Title());
            result_.scenes[i].workUnit = std::string(scenes_[i]->WorkUnitName());
            result_.scenes[i].outcome = SceneOutcome::NotSelected;
            if (!selected_[i])
                result_.scenes[i].note = "not selected by --scenes";
            else
                ++result_.selectedSceneCount;
        }
    }

    BenchmarkRunner::~BenchmarkRunner() = default;

    void BenchmarkRunner::LoadContent()
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();

        context_.device = &device;
        context_.width = device.getViewportProperty().getWidthProperty();
        context_.height = device.getViewportProperty().getHeightProperty();
        context_.workloadScale = config_.workloadScale;
        context_.seed = config_.seed;

        // The requested back-buffer size is a request. A renderer or a window manager may hand
        // back something else, and every scene composes for the size that actually exists --
        // so the result document records what was measured, not what was asked for.
        config_.width = context_.width;
        config_.height = context_.height;

        if (config_.hud)
        {
            try
            {
                hud_.Load(device);
            }
            catch (const std::exception& error)
            {
                std::fprintf(ReportStream(),
                             "cna-benchmark: the overlay could not be created (%s); "
                             "continuing without it\n",
                             error.what());
                config_.hud = false;
            }
        }
    }

    void BenchmarkRunner::UnloadContent()
    {
        if (sceneIndex_ < scenes_.size() && stage_ != Stage::Starting && stage_ != Stage::Finished)
            scenes_[sceneIndex_]->Unload();
        hud_.Unload();
    }

    void BenchmarkRunner::Update(GameTime& /*gameTime*/)
    {
        // Every decision the suite makes belongs to Draw, where the frame it is timing is. An
        // Update that did work would be inside the measured frame period without being inside
        // the submission time, which is exactly the kind of gap a benchmark must not have.
    }

    std::string BenchmarkRunner::UnsupportedReason(const Scene& scene) const
    {
        const GraphicsDevice& device = *context_.device;
        for (const SceneRequirement& requirement : scene.Requirements())
        {
            if (!device.SupportsCapability(requirement.capability))
                return std::string(requirement.reason);
        }
        return {};
    }

    void BenchmarkRunner::RecordSkip(const Scene& scene, SceneOutcome outcome,
                                     const std::string& note, bool announce)
    {
        SceneResult& record = result_.scenes[sceneIndex_];
        record.outcome = outcome;
        record.note = note;

        // `announce` is false when the scene's own line is already open on the console -- a
        // scene that failed mid-render had its name printed before it started, and repeating
        // the whole prefix under it reads as two separate scenes going wrong.
        if (announce)
        {
            std::fprintf(ReportStream(), "  [%zu/%zu] %-20s SKIPPED -- %s\n", sceneIndex_ + 1,
                         scenes_.size(), std::string(scene.Title()).c_str(), note.c_str());
        }
        else
        {
            std::fprintf(ReportStream(), "%s\n", note.c_str());
        }
        std::fflush(ReportStream());
    }

    void BenchmarkRunner::BeginNextScene()
    {
        while (sceneIndex_ < scenes_.size())
        {
            if (!selected_[sceneIndex_])
            {
                ++sceneIndex_;
                continue;
            }

            Scene& scene = *scenes_[sceneIndex_];

            const std::string unsupported = UnsupportedReason(scene);
            if (!unsupported.empty())
            {
                RecordSkip(scene, SceneOutcome::Unsupported, unsupported, true);
                ++sceneIndex_;
                continue;
            }

            try
            {
                scene.Load(context_);
            }
            catch (const std::exception& error)
            {
                RecordSkip(scene, SceneOutcome::Failed,
                           std::string("loading the scene threw: ") + error.what(), true);
                try
                {
                    scene.Unload();
                }
                catch (const std::exception&)
                {
                    // A scene that failed to load may equally fail to tear down; the run must
                    // continue either way, and the load failure is already recorded.
                }
                ++sceneIndex_;
                continue;
            }

            stage_ = Stage::WarmingUp;
            sceneStart_ = Clock::now();
            warmupFramesRendered_ = 0;
            frameInScene_ = 0;
            frameInStage_ = 0;
            havePreviousFrameStart_ = false;
            endToEnd_.Reset(config_.measuredFrames);
            submission_.Reset(config_.measuredFrames);

            std::fprintf(ReportStream(), "  [%zu/%zu] %-20s %8s %-24s ", sceneIndex_ + 1,
                         scenes_.size(), std::string(scene.Title()).c_str(),
                         FormatCount(scene.WorkUnitsPerFrame()).c_str(),
                         (std::string(scene.WorkUnitName()) + "/frame").c_str());
            std::fflush(ReportStream());
            UpdateWindowTitle();
            return;
        }

        stage_ = Stage::Finished;
    }

    bool BenchmarkRunner::SceneBudgetExhausted(double fraction) const
    {
        if (config_.maxSceneSeconds <= 0.0)
            return false;
        return Milliseconds(Clock::now() - sceneStart_) >=
               config_.maxSceneSeconds * 1000.0 * fraction;
    }

    void BenchmarkRunner::FinishCurrentScene()
    {
        Scene& scene = *scenes_[sceneIndex_];
        SceneResult& record = result_.scenes[sceneIndex_];

        record.outcome = SceneOutcome::Measured;
        record.workUnitsPerFrame = scene.WorkUnitsPerFrame();
        record.measuredFrames = endToEnd_.Count();
        record.warmupFrames = warmupFramesRendered_;
        record.truncated = endToEnd_.Count() < config_.measuredFrames ||
                           warmupFramesRendered_ < config_.warmupFrames;
        record.endToEnd = endToEnd_.Summarize();
        record.submission = submission_.Summarize();
        record.detail = scene.Detail();
        record.fps = record.endToEnd.medianMs > 0.0 ? 1000.0 / record.endToEnd.medianMs : 0.0;
        record.meanFps = record.endToEnd.meanMs > 0.0 ? 1000.0 / record.endToEnd.meanMs : 0.0;
        record.onePercentLowFps =
            record.endToEnd.p99Ms > 0.0 ? 1000.0 / record.endToEnd.p99Ms : 0.0;
        record.frameTimeSpread = record.endToEnd.medianMs > 0.0
                                     ? record.endToEnd.p95Ms / record.endToEnd.medianMs
                                     : 0.0;
        record.throughput = record.workUnitsPerFrame * record.fps;
        if (config_.emitSamples)
        {
            record.endToEndSamplesMs = endToEnd_.Samples();
            record.submissionSamplesMs = submission_.Samples();
        }
        result_.measuredSeconds += endToEnd_.TotalMs() / 1000.0;

        std::fprintf(ReportStream(), "%9.1f fps %9.3f ms %10s %s/s%s\n", record.fps,
                     record.endToEnd.medianMs, FormatCount(record.throughput).c_str(),
                     record.workUnit.c_str(),
                     record.truncated ? "  (cut short by --max-scene-seconds)" : "");
        std::fflush(ReportStream());

        try
        {
            scene.Unload();
        }
        catch (const std::exception& error)
        {
            // The measurement is already taken and valid; a teardown failure is worth saying
            // out loud but must not discard the scene's result.
            std::fprintf(ReportStream(), "        (unloading %s threw: %s)\n",
                         record.id.c_str(), error.what());
        }

        ++sceneIndex_;
        stage_ = Stage::Starting;
    }

    void BenchmarkRunner::FinishRun()
    {
        ComputeScore(result_);

        result_.comparable = true;
        if (!result_.complete)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "not every scene in the suite produced a score");
        }
        if (config_.workloadScale != 1.0)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "the workload was scaled away from 1.0");
        }
        if (config_.vsync)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "vertical retrace synchronisation caps the frame rate at the display's");
        }
        if (config_.hud)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "the overlay was drawn inside the measured frames");
        }
        if (config_.measuredFrames < 120)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "fewer than 120 measured frames per scene");
        }

        for (const SceneResult& scene : result_.scenes)
        {
            if (scene.outcome != SceneOutcome::Measured || !scene.truncated)
                continue;
            result_.comparable = false;
            char note[192];
            std::snprintf(note, sizeof(note),
                          "scene '%s' was cut short by --max-scene-seconds: %d of %d warm-up "
                          "and %d of %d measured frames",
                          scene.id.c_str(), scene.warmupFrames, config_.warmupFrames,
                          scene.measuredFrames, config_.measuredFrames);
            result_.comparabilityNotes.emplace_back(note);
        }

        // A renderer that accepted every draw and wrote no pixels produces a perfectly real
        // measurement of CNA's CPU-side cost and a completely misleading graphics score. It
        // belongs in the table -- that CPU floor is genuinely useful to know -- but never as a
        // comparable graphics result.
        if (info_.outputProbe.result == OutputProbeResult::ProducedNothing)
        {
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "this renderer accepted every draw call but the output probe found no pixels; "
                "the score measures CNA's own CPU-side cost, not rendering (" +
                info_.outputProbe.detail + ")");
        }
        else if (info_.outputProbe.result == OutputProbeResult::NotVerifiable)
        {
            // Not an accusation. A renderer with no readback path of any kind may be drawing
            // perfectly -- and it may, like CNA's headless renderer, be drawing nothing at all
            // while reporting the whole 3D pipeline as supported. This suite cannot tell those
            // two apart, so it declines to call the score a comparable graphics result and says
            // exactly why, rather than publishing a number it cannot stand behind.
            result_.comparable = false;
            result_.comparabilityNotes.emplace_back(
                "this renderer could not be shown to produce pixels, so its score cannot be "
                "read as a graphics result (" + info_.outputProbe.detail + ")");
        }

        // A run whose frame times scattered was not measuring this renderer alone. Reporting
        // the number and calling it comparable would be the single easiest way for this suite
        // to publish a wrong answer, so an unsteady scene withdraws the comparability claim
        // and says which scene and by how much.
        for (const SceneResult& scene : result_.scenes)
        {
            if (scene.outcome != SceneOutcome::Measured)
                continue;
            if (scene.frameTimeSpread <= kMaxFrameTimeSpread)
                continue;
            result_.comparable = false;
            char note[224];
            std::snprintf(note, sizeof(note),
                          "scene '%s': the slowest 5%% of frames took %.1f times the median "
                          "(a quiet machine stays under %.1f); something else was using this "
                          "machine",
                          scene.id.c_str(), scene.frameTimeSpread, kMaxFrameTimeSpread);
            result_.comparabilityNotes.emplace_back(note);
        }

        stage_ = Stage::Finished;
    }

    void BenchmarkRunner::UpdateWindowTitle()
    {
        try
        {
            std::string title = "CNA Benchmark -- " + info_.activeRenderer;
            if (sceneIndex_ < scenes_.size())
                title += " -- " + std::string(scenes_[sceneIndex_]->Title());
            getWindowProperty().setTitleProperty(title);
        }
        catch (const std::exception&)
        {
            // A renderer with no window has no title. Not worth a diagnostic: it is the
            // documented behaviour of the headless and no-op renderers.
        }
    }

    void BenchmarkRunner::DrawHud()
    {
        if (!hud_.IsLoaded())
            return;

        std::vector<std::string> lines;
        lines.push_back("CNA BENCHMARK  " + info_.activeRenderer);
        if (sceneIndex_ < scenes_.size())
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "[%zu/%zu] %s", sceneIndex_ + 1,
                          scenes_.size(), std::string(scenes_[sceneIndex_]->Title()).c_str());
            lines.emplace_back(buffer);
            std::snprintf(buffer, sizeof(buffer), "%s  frame %d",
                          stage_ == Stage::Measuring ? "MEASURING" : "warming up",
                          frameInStage_);
            lines.emplace_back(buffer);
            std::snprintf(buffer, sizeof(buffer), "%.2f ms  %.1f fps", lastFrameMs_,
                          lastFrameMs_ > 0.0 ? 1000.0 / lastFrameMs_ : 0.0);
            lines.emplace_back(buffer);
        }

        try
        {
            hud_.Draw(lines, 2);
        }
        catch (const std::exception&)
        {
            // The overlay is never worth aborting a run for.
        }
    }

    void BenchmarkRunner::Draw(const GameTime& /*gameTime*/)
    {
        if (stage_ == Stage::Finished)
            return;

        const Clock::time_point frameStart = Clock::now();

        if (!deviceInfoCaptured_)
        {
            CollectDeviceRunInfo(info_, getGraphicsDeviceProperty());
            info_.outputProbe = ProbeRendererOutput(getGraphicsDeviceProperty());
            deviceInfoCaptured_ = true;
            std::fprintf(ReportStream(), "Renderer: %s\n", info_.activeRenderer.c_str());
            switch (info_.outputProbe.result)
            {
                case OutputProbeResult::Verified:
                    std::fprintf(ReportStream(), "Output:   verified -- %s\n",
                                 info_.outputProbe.detail.c_str());
                    break;
                case OutputProbeResult::ProducedNothing:
                    std::fprintf(ReportStream(), "Output:   NONE -- %s\n",
                                 info_.outputProbe.detail.c_str());
                    break;
                case OutputProbeResult::NotVerifiable:
                    std::fprintf(ReportStream(), "Output:   not verifiable -- %s\n",
                                 info_.outputProbe.detail.c_str());
                    break;
            }
            std::fprintf(ReportStream(),
                         "Back buffer: %dx%d, vsync %s, %d warm-up + %d measured frames "
                         "per scene\n\n",
                         config_.width, config_.height, config_.vsync ? "on" : "off",
                         config_.warmupFrames, config_.measuredFrames);
            std::fflush(ReportStream());
        }

        if (stage_ == Stage::Starting)
        {
            BeginNextScene();
            if (stage_ == Stage::Finished)
            {
                FinishRun();
                Exit();
                return;
            }
        }

        // The gap since the previous frame started is this renderer's real frame period: it
        // includes presentation, the host loop's event handling and any wait the driver
        // imposed, none of which the submission timer below can see.
        if (stage_ == Stage::Measuring && havePreviousFrameStart_)
        {
            const double gapMs = Milliseconds(frameStart - previousFrameStart_);
            endToEnd_.Add(gapMs);
            lastFrameMs_ = gapMs;
        }
        previousFrameStart_ = frameStart;
        havePreviousFrameStart_ = true;

        Scene& scene = *scenes_[sceneIndex_];
        const double sceneTime = static_cast<double>(frameInScene_) * kSceneTimeStep;

        try
        {
            const Clock::time_point submitStart = Clock::now();
            scene.Render(context_, frameInScene_, sceneTime);
            const double submitMs = Milliseconds(Clock::now() - submitStart);
            if (stage_ == Stage::Measuring)
                submission_.Add(submitMs);
        }
        catch (const std::exception& error)
        {
            RecordSkip(scene, SceneOutcome::Failed,
                       std::string("FAILED -- rendering threw: ") + error.what(), false);
            try
            {
                scene.Unload();
            }
            catch (const std::exception&)
            {
                // Already reporting the render failure; a teardown failure adds nothing.
            }
            ++sceneIndex_;
            stage_ = Stage::Starting;
            return;
        }

        if (config_.hud)
            DrawHud();

        ++frameInScene_;
        ++frameInStage_;

        // Half the scene budget is the most the warm-up may take. On a renderer slow enough
        // for the budget to matter at all, the requested warm-up alone can outlast the whole
        // budget, and a scene that never reached its measured window would report nothing.
        if (stage_ == Stage::WarmingUp &&
            (frameInStage_ >= config_.warmupFrames || SceneBudgetExhausted(0.5)))
        {
            warmupFramesRendered_ = frameInStage_;
            stage_ = Stage::Measuring;
            frameInStage_ = 0;
        }
        else if (stage_ == Stage::Measuring &&
                 (endToEnd_.Count() >= config_.measuredFrames ||
                  (endToEnd_.Count() >= 1 && SceneBudgetExhausted(1.0))))
        {
            FinishCurrentScene();
        }
    }
} // namespace CnaBenchmark
