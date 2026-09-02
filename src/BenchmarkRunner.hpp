// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file BenchmarkRunner.hpp
 * @brief The `Game` that drives the suite: one scene at a time, warm up, measure, score.
 */

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "Benchmark.hpp"
#include "FrameSampler.hpp"
#include "Hud.hpp"
#include "RunInfo.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"

namespace Microsoft::Xna::Framework
{
    class GraphicsDeviceManager;
}

namespace CnaBenchmark
{
    /**
     * @brief Runs every selected scene inside one CNA `Game` and produces a scored result.
     *
     * The whole suite shares one window, one device and one process. That is not a convenience:
     * creating a device per scene would put driver initialization inside the comparison, and
     * seven processes would compare seven different thermal states of the same machine.
     */
    class BenchmarkRunner final : public Microsoft::Xna::Framework::Game
    {
    public:
        /** @brief Builds a runner for @p config. @param config The run's settings. */
        explicit BenchmarkRunner(RunConfig config);
        ~BenchmarkRunner() override;

        /** @brief The scored outcome. Valid once `Run()` has returned. */
        [[nodiscard]] const RunResult& Result() const { return result_; }
        /** @brief Machine, build and renderer context captured during the run. */
        [[nodiscard]] const RunInfo& Info() const { return info_; }
        /** @brief The exception that aborted the run, or an empty string if none did. */
        [[nodiscard]] const std::string& FatalError() const { return fatalError_; }

    protected:
        void LoadContent() override;
        void UnloadContent() override;
        void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
        void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    private:
        using Clock = std::chrono::steady_clock;

        /** @brief What the runner is doing on the current frame. */
        enum class Stage
        {
            /** @brief Nothing loaded yet; the next frame picks up the next scene. */
            Starting,
            /** @brief Rendering untimed frames so caches and clocks settle. */
            WarmingUp,
            /** @brief Rendering the frames that are timed and scored. */
            Measuring,
            /** @brief Every scene is done and the result has been computed. */
            Finished
        };

        void BeginNextScene();
        /**
         * @brief Whether this scene has used up @p fraction of its time budget.
         * @param fraction Share of `RunConfig::maxSceneSeconds` to test against.
         */
        [[nodiscard]] bool SceneBudgetExhausted(double fraction) const;
        void FinishCurrentScene();
        void FinishRun();
        void RecordSkip(const Scene& scene, SceneOutcome outcome, const std::string& note,
                        bool announce);
        [[nodiscard]] std::string UnsupportedReason(const Scene& scene) const;
        void UpdateWindowTitle();
        void DrawHud();

        RunConfig config_;
        RunResult result_;
        RunInfo info_;
        std::string fatalError_;

        std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> deviceManager_;
        std::vector<std::unique_ptr<Scene>> scenes_;
        std::vector<bool> selected_;
        SceneContext context_{};
        Hud hud_;

        std::size_t sceneIndex_ = 0;
        Stage stage_ = Stage::Starting;
        int frameInScene_ = 0;
        int frameInStage_ = 0;
        int warmupFramesRendered_ = 0;
        bool deviceInfoCaptured_ = false;

        FrameSampler endToEnd_;
        FrameSampler submission_;
        Clock::time_point sceneStart_{};
        Clock::time_point previousFrameStart_{};
        bool havePreviousFrameStart_ = false;
        double lastFrameMs_ = 0.0;
    };
} // namespace CnaBenchmark
