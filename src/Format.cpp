// SPDX-License-Identifier: MS-PL

#include "Format.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace CnaBenchmark
{
    std::string FormatCount(double value)
    {
        const char* sign = value < 0.0 ? "-" : "";
        const double magnitude = std::fabs(value);

        char buffer[64];
        if (magnitude >= 1e12)
            std::snprintf(buffer, sizeof(buffer), "%s%.2f T", sign, magnitude / 1e12);
        else if (magnitude >= 1e9)
            std::snprintf(buffer, sizeof(buffer), "%s%.2f G", sign, magnitude / 1e9);
        else if (magnitude >= 1e6)
            std::snprintf(buffer, sizeof(buffer), "%s%.2f M", sign, magnitude / 1e6);
        else if (magnitude >= 1e3)
            std::snprintf(buffer, sizeof(buffer), "%s%.1f k", sign, magnitude / 1e3);
        else
            std::snprintf(buffer, sizeof(buffer), "%s%.0f", sign, magnitude);
        return buffer;
    }

    std::string FormatGrouped(long long value)
    {
        // Grouped by hand rather than through a locale: the score is part of a published
        // result, and a machine whose locale groups differently -- or not at all -- must not
        // print a different string for the same number.
        const bool negative = value < 0;
        unsigned long long magnitude =
            negative ? 0ULL - static_cast<unsigned long long>(value)
                     : static_cast<unsigned long long>(value);

        std::string digits;
        do
        {
            digits.insert(digits.begin(), static_cast<char>('0' + magnitude % 10));
            magnitude /= 10;
        } while (magnitude != 0);

        // A separator goes before digit `i` when exactly a multiple of three digits follow
        // it. Expressed as the count of remaining digits rather than as a distance from the
        // first group, because the latter needs a subtraction that underflows on an unsigned
        // index -- which silently produced "88,2" for 882 before this was written this way.
        std::string grouped;
        for (std::size_t i = 0; i < digits.size(); ++i)
        {
            if (i != 0 && (digits.size() - i) % 3 == 0)
                grouped.push_back(',');
            grouped.push_back(digits[i]);
        }

        return negative ? "-" + grouped : grouped;
    }
} // namespace CnaBenchmark
