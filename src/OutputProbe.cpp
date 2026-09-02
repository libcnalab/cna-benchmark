// SPDX-License-Identifier: MS-PL

#include "OutputProbe.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Edge length of the off-screen probe target. Evidence, not a workload. */
        constexpr int kProbeSize = 16;

        /**
         * @brief The colour the probe draws.
         *
         * Deliberately not white, not black, not grey and not a primary: every channel differs
         * from every other, so a renderer that swizzles channels, one that only clears, and one
         * that leaves the surface untouched each produce a different, recognisable wrong answer
         * instead of all three producing the same one.
         */
        constexpr std::uint8_t kDrawR = 218;
        constexpr std::uint8_t kDrawG = 61;
        constexpr std::uint8_t kDrawB = 137;

        /** @brief The clear colour, far from the draw colour in every channel. */
        constexpr std::uint8_t kClearR = 7;
        constexpr std::uint8_t kClearG = 190;
        constexpr std::uint8_t kClearB = 23;

        /** @brief Channel tolerance, generous enough for 16-bit and 5-6-5 target formats. */
        constexpr int kTolerance = 24;

        bool ChannelMatches(int actual, int expected)
        {
            const int difference = actual - expected;
            return difference >= -kTolerance && difference <= kTolerance;
        }

        /** @brief Classifies one read-back pixel against the probe's expected colour. */
        OutputProbe Classify(const Color& pixel, const char* route)
        {
            OutputProbe probe;
            const int r = static_cast<int>(pixel.getRProperty());
            const int g = static_cast<int>(pixel.getGProperty());
            const int b = static_cast<int>(pixel.getBProperty());

            char detail[224];
            if (ChannelMatches(r, kDrawR) && ChannelMatches(g, kDrawG) &&
                ChannelMatches(b, kDrawB))
            {
                std::snprintf(detail, sizeof(detail),
                              "a quad drawn in rgb(%d,%d,%d) read back from the %s as "
                              "rgb(%d,%d,%d)",
                              kDrawR, kDrawG, kDrawB, route, r, g, b);
                probe.result = OutputProbeResult::Verified;
            }
            else
            {
                std::snprintf(detail, sizeof(detail),
                              "a quad drawn in rgb(%d,%d,%d) over a rgb(%d,%d,%d) clear read "
                              "back from the %s as rgb(%d,%d,%d)",
                              kDrawR, kDrawG, kDrawB, kClearR, kClearG, kClearB, route, r, g, b);
                probe.result = OutputProbeResult::ProducedNothing;
            }
            probe.detail = detail;
            return probe;
        }

        /** @brief Draws the probe quad over the whole of the currently bound target. */
        void DrawProbeQuad(GraphicsDevice& device, int width, int height)
        {
            Texture2D white = Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &SamplerState::PointClamp,
                        nullptr, nullptr);
            batch.Draw(white, Rectangle(0, 0, width, height), std::optional<Rectangle>{},
                       Color(kDrawR, kDrawG, kDrawB, static_cast<std::uint8_t>(255)));
            batch.End();
        }

        /**
         * @brief The off-screen route: draw into a `RenderTarget2D` and read the target back.
         *
         * Tried first because it disturbs nothing the run depends on, and because more
         * renderers implement it than implement back-buffer readback.
         */
        OutputProbe ProbeViaRenderTarget(GraphicsDevice& device)
        {
            RenderTarget2D target(device, kProbeSize, kProbeSize);

            device.SetRenderTarget(&target);
            device.Clear(Color(kClearR, kClearG, kClearB, static_cast<std::uint8_t>(255)));
            DrawProbeQuad(device, kProbeSize, kProbeSize);
            device.SetRenderTarget(nullptr);

            std::vector<Color> pixels(static_cast<std::size_t>(kProbeSize) * kProbeSize);
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            return Classify(pixels[pixels.size() / 2 + kProbeSize / 2], "render target");
        }

        /**
         * @brief The back-buffer route, for renderers with no off-screen target at all.
         *
         * Some renderers -- the older Direct3D families and the 2D-only ones especially --
         * have no `RenderTarget2D` and a perfectly real back buffer. Refusing to verify those
         * because the first route did not apply would be the probe drawing a conclusion about
         * the renderer from its own limitation.
         */
        OutputProbe ProbeViaBackBuffer(GraphicsDevice& device)
        {
            device.Clear(Color(kClearR, kClearG, kClearB, static_cast<std::uint8_t>(255)));

            const auto& viewport = device.getViewportProperty();
            DrawProbeQuad(device, viewport.getWidthProperty(), viewport.getHeightProperty());

            const Rectangle sample(viewport.getWidthProperty() / 2,
                                   viewport.getHeightProperty() / 2, 1, 1);
            Color pixel;
            device.GetBackBufferData(&sample, &pixel, 0, 1);
            return Classify(pixel, "back buffer");
        }
    }

    OutputProbe ProbeRendererOutput(GraphicsDevice& device)
    {
        std::string renderTargetFailure;

        try
        {
            return ProbeViaRenderTarget(device);
        }
        catch (const std::exception& error)
        {
            renderTargetFailure = error.what();
            try
            {
                // A device left bound to a destroyed target would fail every scene after this.
                device.SetRenderTarget(nullptr);
            }
            catch (const std::exception&)
            {
                // Nothing further can be done; the fallback below is tried regardless.
            }
        }

        try
        {
            return ProbeViaBackBuffer(device);
        }
        catch (const std::exception& error)
        {
            OutputProbe probe;
            probe.result = OutputProbeResult::NotVerifiable;
            probe.detail = "neither readback route was available -- render target: " +
                           renderTargetFailure + "; back buffer: " + error.what();
            return probe;
        }
    }
} // namespace CnaBenchmark
