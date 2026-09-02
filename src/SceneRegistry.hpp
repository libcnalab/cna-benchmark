// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SceneRegistry.hpp
 * @brief The suite's scene list, in the fixed order every run reports.
 */

#include <memory>
#include <string_view>
#include <vector>

#include "Benchmark.hpp"

namespace CnaBenchmark
{
    /** @brief Creates the sprite-batching scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateSpritesScene();
    /** @brief Creates the additive particle scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateParticlesScene();
    /** @brief Creates the GPU-skinning scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateSkeletalScene();
    /** @brief Creates the terrain scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateTerrainScene();
    /** @brief Creates the multi-pass lighting scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateLightsScene();
    /** @brief Creates the hardware-instancing scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateInstancingScene();
    /** @brief Creates the dynamic-upload scene. */
    [[nodiscard]] std::unique_ptr<Scene> CreateStreamingScene();

    /**
     * @brief Builds every scene, in the suite's fixed order.
     *
     * The order is part of the published contract: scores are indexed by scene id, and the
     * 2D scenes deliberately run first so that a 2D-only renderer produces its results before
     * reaching the scenes it must skip.
     */
    [[nodiscard]] std::vector<std::unique_ptr<Scene>> CreateAllScenes();

    /**
     * @brief Returns whether @p id names a registered scene.
     * @param id Candidate scene id.
     */
    [[nodiscard]] bool IsKnownSceneId(std::string_view id);

    /** @brief Returns every registered scene id, in registry order. */
    [[nodiscard]] std::vector<std::string> AllSceneIds();
} // namespace CnaBenchmark
