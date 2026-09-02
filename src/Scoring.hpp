// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Scoring.hpp
 * @brief The CNA Score: how seven scene throughputs become one comparable number.
 */

#include <string_view>
#include <vector>

#include "Benchmark.hpp"

namespace CnaBenchmark
{
    /** @brief Identity of the calibration constants a score was computed against. */
    inline constexpr std::string_view kReferenceProfileId = "cna-bench-ref-v1";

    /** @brief One scene's entry in the reference profile. */
    struct ReferenceEntry
    {
        /** @brief The scene id this entry calibrates. */
        std::string_view sceneId;
        /** @brief Work units per second that score exactly 1000 for this scene. */
        double throughput;
        /** @brief Relative weight in the composite. The suite uses equal weights. */
        double weight;
    };

    /** @brief The reference profile's entries, in registry order. */
    [[nodiscard]] std::vector<ReferenceEntry> ReferenceProfile();

    /**
     * @brief Returns the reference throughput for @p sceneId, or 0 when it has no entry.
     * @param sceneId The scene's stable id.
     */
    [[nodiscard]] double ReferenceThroughputFor(std::string_view sceneId);

    /**
     * @brief Returns the composite weight for @p sceneId, or 0 when it has no entry.
     * @param sceneId The scene's stable id.
     */
    [[nodiscard]] double ReferenceWeightFor(std::string_view sceneId);

    /**
     * @brief Fills in every scene's index and score, then the run's composite and CNA Score.
     *
     * Scenes that were not measured contribute nothing at all -- they are neither a zero nor a
     * one. A zero would drive the geometric mean to zero and a one would silently reward a
     * renderer for a scene it cannot draw; both would let an incomplete run masquerade as a
     * comparable one. `RunResult::complete` is what says the difference, and the caller is
     * expected to print it next to the score.
     *
     * @param result The run to score in place.
     */
    void ComputeScore(RunResult& result);
} // namespace CnaBenchmark
