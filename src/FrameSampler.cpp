// SPDX-License-Identifier: MS-PL

#include "FrameSampler.hpp"

#include <algorithm>
#include <cmath>

namespace CnaBenchmark
{
    void FrameSampler::Reset(int expectedSamples)
    {
        samples_.clear();
        if (expectedSamples > 0)
            samples_.reserve(static_cast<std::size_t>(expectedSamples));
    }

    void FrameSampler::Add(double milliseconds)
    {
        samples_.push_back(milliseconds);
    }

    double FrameSampler::TotalMs() const
    {
        // Pairwise-free but ordered summation is enough here: the windows are hundreds of
        // samples of comparable magnitude, not the mixed-scale case that needs compensation.
        double total = 0.0;
        for (const double sample : samples_)
            total += sample;
        return total;
    }

    namespace
    {
        /**
         * @brief The linear-interpolation percentile (the "inclusive" definition), on a sorted
         *        vector. Chosen because it is what NumPy, Excel's PERCENTILE.INC and every
         *        spreadsheet a reader is likely to check these numbers with produce.
         */
        double Percentile(const std::vector<double>& sorted, double fraction)
        {
            if (sorted.empty())
                return 0.0;
            if (sorted.size() == 1)
                return sorted.front();

            const double position = fraction * static_cast<double>(sorted.size() - 1);
            const double lowerIndex = std::floor(position);
            const double upperIndex = std::ceil(position);
            const double lower = sorted[static_cast<std::size_t>(lowerIndex)];
            const double upper = sorted[static_cast<std::size_t>(upperIndex)];
            if (lowerIndex == upperIndex)
                return lower;
            return lower + (upper - lower) * (position - lowerIndex);
        }
    }

    FrameStats FrameSampler::Summarize() const
    {
        FrameStats stats;
        stats.count = Count();
        if (samples_.empty())
            return stats;

        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        stats.meanMs = TotalMs() / static_cast<double>(sorted.size());
        stats.minMs = sorted.front();
        stats.maxMs = sorted.back();
        stats.medianMs = Percentile(sorted, 0.50);
        stats.p95Ms = Percentile(sorted, 0.95);
        stats.p99Ms = Percentile(sorted, 0.99);

        double variance = 0.0;
        for (const double sample : sorted)
        {
            const double deviation = sample - stats.meanMs;
            variance += deviation * deviation;
        }
        stats.stdDevMs = std::sqrt(variance / static_cast<double>(sorted.size()));
        return stats;
    }
} // namespace CnaBenchmark
