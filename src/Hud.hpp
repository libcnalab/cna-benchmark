// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Hud.hpp
 * @brief The optional on-screen overlay.
 *
 * Off unless `--hud` is given, and a run that enables it is marked non-comparable: the overlay
 * is drawn inside the frame it describes, so its own cost lands in that frame's measurement.
 * It exists to watch a run and to demonstrate the suite, not to produce a published number.
 */

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class SpriteBatch;
    class Texture2D;
}

namespace CnaBenchmark
{
    /** @brief A text overlay built on the suite's 8x8 bitmap font. */
    class Hud
    {
    public:
        Hud();
        ~Hud();
        Hud(const Hud&) = delete;
        Hud& operator=(const Hud&) = delete;

        /**
         * @brief Creates the overlay's font atlas and sprite batch.
         * @param device Device that owns the resources.
         */
        void Load(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Releases the overlay's resources. Safe to call when not loaded. */
        void Unload();
        /** @brief Whether `Load` has succeeded and `Draw` will render. */
        [[nodiscard]] bool IsLoaded() const { return batch_ != nullptr; }

        /**
         * @brief Draws @p lines in the top-left corner over a translucent panel.
         * @param lines Text lines; characters outside ASCII 0x20..0x7E render as blanks.
         * @param scale Integer pixel scale of the 8x8 glyphs.
         */
        void Draw(const std::vector<std::string>& lines, int scale);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_ = nullptr;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> batch_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> font_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> panel_;
    };
} // namespace CnaBenchmark
