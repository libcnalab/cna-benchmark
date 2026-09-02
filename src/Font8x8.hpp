// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Font8x8.hpp
 * @brief An 8x8 bitmap font, so the overlay needs no font file and no content pipeline.
 *
 * A benchmark must run from a bare build tree on a machine with no assets, and a missing font
 * must never be the reason a result is missing.
 */

#include <cstdint>

namespace CnaBenchmark
{
    /** @brief First glyph the table covers: ASCII space. */
    inline constexpr char kFontFirstChar = 0x20;
    /** @brief Last glyph the table covers: ASCII tilde. */
    inline constexpr char kFontLastChar = 0x7E;
    /** @brief Glyph edge length in pixels. */
    inline constexpr int kFontGlyphSize = 8;

    /**
     * @brief Returns one glyph's eight row bitmaps, bit 0 being the leftmost pixel.
     * @param character Any character; anything outside the covered range yields the blank glyph.
     */
    [[nodiscard]] const std::uint8_t* GlyphRows(char character);
} // namespace CnaBenchmark
