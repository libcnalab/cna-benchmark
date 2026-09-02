// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Format.hpp
 * @brief Console number formatting. Kept in one place so every line of the report agrees.
 */

#include <string>

namespace CnaBenchmark
{
    /**
     * @brief Formats a magnitude compactly: `4.0 k`, `295 k`, `1.42 M`, `2.05 G`.
     *
     * Decimal SI prefixes, not binary ones, even for the byte counts: a benchmark's byte
     * figures are rates, and rates are conventionally decimal.
     *
     * @param value The number to render. Negative values keep their sign.
     */
    [[nodiscard]] std::string FormatCount(double value);

    /**
     * @brief Formats an integer with thousands separators: `18472` becomes `18,472`.
     * @param value The number to render.
     */
    [[nodiscard]] std::string FormatGrouped(long long value);
} // namespace CnaBenchmark
