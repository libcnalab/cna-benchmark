// SPDX-License-Identifier: MS-PL
//
// Scene 1 -- Sprites.
//
// What it measures: the whole SpriteBatch path. Per-sprite CPU work, quad generation, the
// batching itself, and the renderer's ability to keep one texture bound across thousands of
// draws. Every sprite moves, rotates and scales every frame, so nothing about the batch can be
// cached between frames.
//
// Why the atlas has sixteen distinct tiles: a batch drawn from one sub-rectangle is the easiest
// possible case, and a renderer that flushes per sprite instead of per texture looks identical
// to a correct one on it. Sixteen tiles in one texture keeps the *texture* constant -- so the
// scene is not secretly measuring texture switches -- while making every quad's source
// rectangle different, which is what a real sprite-based game does.
//
// This scene needs nothing but 2D, so it runs on every CNA renderer, including the ones with no
// 3D pipeline at all.

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "Benchmark.hpp"
#include "Procedural.hpp"
#include "SceneRegistry.hpp"
#include "scenes/SceneCommon.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Sprites drawn per frame at workload scale 1. */
        constexpr int kNominalSpriteCount = 4000;
        /** @brief Atlas edge length in pixels. */
        constexpr int kAtlasSize = 256;
        /** @brief Tiles per atlas axis. */
        constexpr int kAtlasTiles = 4;

        /** @brief The per-sprite constants chosen once at load and never touched again. */
        struct SpriteSeed
        {
            float originX;
            float originY;
            float driftX;
            float driftY;
            float spinRate;
            float scaleBase;
            float scaleRate;
            int tile;
            Color tint;
        };

        class SpritesScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "sprites"; }
            [[nodiscard]] std::string_view Title() const override { return "Sprites"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "Thousands of rotating, scaling, tinted SpriteBatch quads drawn from "
                       "sixteen sub-rectangles of one texture atlas.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override { return "sprites"; }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                Rng rng(context.seed ^ 0x5D1E5ULL);

                atlas_ = CreateAtlasTexture(device, kAtlasSize, kAtlasTiles, rng);
                batch_ = std::make_unique<SpriteBatch>(device);

                spriteCount_ = ScaledCount(kNominalSpriteCount, context.workloadScale);
                seeds_.clear();
                seeds_.reserve(static_cast<std::size_t>(spriteCount_));

                const auto width = static_cast<float>(context.width);
                const auto height = static_cast<float>(context.height);
                for (int i = 0; i < spriteCount_; ++i)
                {
                    SpriteSeed seed{};
                    seed.originX = rng.NextFloat(0.0f, width);
                    seed.originY = rng.NextFloat(0.0f, height);
                    seed.driftX = rng.NextFloat(-0.35f, 0.35f) * width;
                    seed.driftY = rng.NextFloat(-0.35f, 0.35f) * height;
                    seed.spinRate = rng.NextFloat(-2.5f, 2.5f);
                    seed.scaleBase = rng.NextFloat(0.10f, 0.30f);
                    seed.scaleRate = rng.NextFloat(0.5f, 2.0f);
                    seed.tile = rng.NextInt(kAtlasTiles * kAtlasTiles);
                    seed.tint = Color(static_cast<std::uint8_t>(rng.NextInt(128) + 128),
                                      static_cast<std::uint8_t>(rng.NextInt(128) + 128),
                                      static_cast<std::uint8_t>(rng.NextInt(128) + 128),
                                      static_cast<std::uint8_t>(255));
                    seeds_.push_back(seed);
                }

                viewportWidth_ = static_cast<float>(context.width);
                viewportHeight_ = static_cast<float>(context.height);
            }

            void Unload() override
            {
                batch_.reset();
                atlas_.reset();
                seeds_.clear();
            }

            void Render(const SceneContext& context, int /*frameIndex*/, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                device.Clear(Color(12, 14, 22, 255));

                const auto time = static_cast<float>(sceneTime);
                const int tileSize = kAtlasSize / kAtlasTiles;
                const float origin = static_cast<float>(tileSize) * 0.5f;

                batch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                              &SamplerState::LinearClamp, nullptr, nullptr);
                for (const SpriteSeed& seed : seeds_)
                {
                    // Wrapped rather than bounced: a wrap is one fmod, a bounce is a branch per
                    // axis, and the scene must measure the renderer rather than its own motion
                    // model.
                    float x = std::fmod(seed.originX + seed.driftX * time, viewportWidth_);
                    float y = std::fmod(seed.originY + seed.driftY * time, viewportHeight_);
                    if (x < 0.0f)
                        x += viewportWidth_;
                    if (y < 0.0f)
                        y += viewportHeight_;

                    const Rectangle source((seed.tile % kAtlasTiles) * tileSize,
                                           (seed.tile / kAtlasTiles) * tileSize,
                                           tileSize, tileSize);
                    const float scale =
                        seed.scaleBase * (1.15f + 0.35f * std::sin(time * seed.scaleRate));

                    batch_->Draw(*atlas_, Vector2(x, y), std::optional<Rectangle>{source},
                                 seed.tint, time * seed.spinRate, Vector2(origin, origin), scale,
                                 SpriteEffects::None, 0.0f);
                }
                batch_->End();
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(spriteCount_);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"spritesPerFrame", static_cast<double>(spriteCount_)},
                    {"atlasTiles", static_cast<double>(kAtlasTiles * kAtlasTiles)},
                    {"spriteBatchesPerFrame", 1.0},
                };
            }

        private:
            std::unique_ptr<Texture2D> atlas_;
            std::unique_ptr<SpriteBatch> batch_;
            std::vector<SpriteSeed> seeds_;
            int spriteCount_ = 0;
            float viewportWidth_ = 1.0f;
            float viewportHeight_ = 1.0f;
        };
    }

    std::unique_ptr<Scene> CreateSpritesScene()
    {
        return std::make_unique<SpritesScene>();
    }
} // namespace CnaBenchmark
