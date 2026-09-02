// SPDX-License-Identifier: MS-PL

#include "Hud.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Font8x8.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Glyphs per row in the generated atlas. */
        constexpr int kAtlasColumns = 16;
        /** @brief Glyph rows in the generated atlas: 95 glyphs over 16 columns. */
        constexpr int kAtlasRows = 6;
        constexpr int kAtlasWidth = kAtlasColumns * kFontGlyphSize;
        constexpr int kAtlasHeight = kAtlasRows * kFontGlyphSize;
    }

    Hud::Hud() = default;
    Hud::~Hud() = default;

    void Hud::Load(GraphicsDevice& device)
    {
        device_ = &device;

        std::vector<std::uint8_t> atlas(
            static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
        for (int glyph = 0; glyph <= kFontLastChar - kFontFirstChar; ++glyph)
        {
            const std::uint8_t* rows = GlyphRows(static_cast<char>(kFontFirstChar + glyph));
            const int originX = (glyph % kAtlasColumns) * kFontGlyphSize;
            const int originY = (glyph / kAtlasColumns) * kFontGlyphSize;
            for (int y = 0; y < kFontGlyphSize; ++y)
            {
                for (int x = 0; x < kFontGlyphSize; ++x)
                {
                    const bool lit = (rows[y] & (1u << x)) != 0;
                    const std::size_t offset =
                        (static_cast<std::size_t>(originY + y) * kAtlasWidth + originX + x) * 4;
                    const std::uint8_t value = lit ? 255 : 0;
                    atlas[offset + 0] = value;
                    atlas[offset + 1] = value;
                    atlas[offset + 2] = value;
                    atlas[offset + 3] = value;
                }
            }
        }

        font_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, kAtlasWidth, kAtlasHeight, atlas));
        panel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        batch_ = std::make_unique<SpriteBatch>(device);
    }

    void Hud::Unload()
    {
        batch_.reset();
        font_.reset();
        panel_.reset();
        device_ = nullptr;
    }

    void Hud::Draw(const std::vector<std::string>& lines, int scale)
    {
        if (!IsLoaded() || lines.empty())
            return;

        const int glyphSize = kFontGlyphSize * std::max(1, scale);
        const int margin = glyphSize / 2;
        std::size_t widest = 0;
        for (const std::string& line : lines)
            widest = std::max(widest, line.size());

        const int panelWidth = static_cast<int>(widest) * glyphSize + margin * 2;
        const int panelHeight = static_cast<int>(lines.size()) * glyphSize + margin * 2;

        batch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                      &SamplerState::PointClamp, nullptr, nullptr);

        batch_->Draw(*panel_, Rectangle(0, 0, panelWidth, panelHeight),
                     std::optional<Rectangle>{}, Color(0, 0, 0, 170));

        for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const std::string& line = lines[lineIndex];
            const int y = margin + static_cast<int>(lineIndex) * glyphSize;
            for (std::size_t column = 0; column < line.size(); ++column)
            {
                const char character = line[column];
                if (character < kFontFirstChar || character > kFontLastChar || character == ' ')
                    continue;
                const int glyph = character - kFontFirstChar;
                const Rectangle source((glyph % kAtlasColumns) * kFontGlyphSize,
                                       (glyph / kAtlasColumns) * kFontGlyphSize,
                                       kFontGlyphSize, kFontGlyphSize);
                const Rectangle destination(margin + static_cast<int>(column) * glyphSize, y,
                                            glyphSize, glyphSize);
                batch_->Draw(*font_, destination, std::optional<Rectangle>{source},
                             Color(230, 240, 255, 255));
            }
        }

        batch_->End();
    }
} // namespace CnaBenchmark
