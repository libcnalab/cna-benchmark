// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file FrameSampler.hpp
 * @brief Collects per-frame durations and reduces them to a distribution.
 *
 * Two things this deliberately does not do: it never discards an outlier, and it never
 * averages on the fly. Both would hide exactly the stutter a benchmark exists to expose, so
 * every sample is kept and the percentiles are computed from the complete window.
 */

#include <vector>

#include "Benchmark.hpp"

namespace CnaBenchmark
{
    /** @brief An append-only window of millisecond samples. */
    class FrameSampler
    {
    public:
        /** @brief Reserves capacity for @p expectedSamples and drops any previous window. */
        void Reset(int expectedSamples);
        /** @brief Appends one sample. @param milliseconds The measured duration. */
        void Add(double milliseconds);
        /** @brief Number of samples collected so far. */
        [[nodiscard]] int Count() const { return static_cast<int>(samples_.size()); }
        /** @brief Sum of every sample in milliseconds. */
        [[nodiscard]] double TotalMs() const;
        /** @brief The samples in collection order. */
        [[nodiscard]] const std::vector<double>& Samples() const { return samples_; }
        /** @brief Reduces the window to its distribution. */
        [[nodiscard]] FrameStats Summarize() const;

    private:
        std::vector<double> samples_;
    };
} // namespace CnaBenchmark
