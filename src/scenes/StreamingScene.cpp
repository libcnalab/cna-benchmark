// SPDX-License-Identifier: MS-PL
//
// Scene 7 -- Streaming.
//
// What it measures: how fast this renderer accepts data that changes every single frame. Two
// upload paths, both of which a real game leans on and neither of which the other six scenes
// touch:
//
//   * a full-surface `Texture2D::SetData` -- the video frame, the CPU-generated mask, the
//     scrolling atlas page;
//   * a `DynamicVertexBuffer::SetData` with `SetDataOptions::Discard` -- the CPU-built geometry
//     a particle system, a UI or a software-animated mesh rewrites per frame.
//
// Both are drawn after being uploaded, not merely written. An upload the frame never consumes
// can be deferred, coalesced or skipped by a driver, and a benchmark that measured only the
// write would be measuring how well the renderer postpones work rather than how fast it does
// it.
//
// `Discard` is the whole point of the vertex path: it is the promise that lets a renderer hand
// back fresh storage instead of stalling until the GPU has finished with the old contents. A
// renderer that ignores it is exactly what this scene is built to expose.
//
// The work unit is bytes, so the score reads as upload bandwidth, and the two paths are summed
// because a renderer can be fast at one and slow at the other.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "Benchmark.hpp"
#include "Procedural.hpp"
#include "SceneRegistry.hpp"
#include "scenes/SceneCommon.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Edge length of the streamed texture at workload scale 1. */
        constexpr int kNominalTextureSize = 512;
        /** @brief Vertices rewritten per frame at workload scale 1. */
        constexpr int kNominalVertexCount = 60000;
        /** @brief Bytes in one `VertexPositionColor`: three floats and a packed colour. */
        constexpr int kVertexStride = 16;

        class StreamingScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "streaming"; }
            [[nodiscard]] std::string_view Title() const override { return "Streaming"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "A full texture surface and a discarded dynamic vertex buffer rewritten "
                       "and redrawn every frame.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override { return "bytes"; }

            [[nodiscard]] std::vector<SceneRequirement> Requirements() const override
            {
                return {{CNA::GraphicsCapability::ThreeD,
                         "the streamed vertex buffer is drawn through the 3D pipeline"}};
            }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;

                // The texture is scaled on its edge so the byte count scales linearly, and
                // clamped to a multiple of four so the generated pattern tiles exactly.
                const int scaledEdge = ScaledCount(kNominalTextureSize,
                                                   std::sqrt(context.workloadScale));
                textureSize_ = std::max(4, (scaledEdge / 4) * 4);
                vertexCount_ = ScaledCount(kNominalVertexCount, context.workloadScale);
                // A triangle list needs whole triangles.
                vertexCount_ = std::max(3, (vertexCount_ / 3) * 3);

                texture_ = std::make_unique<Texture2D>(device, textureSize_, textureSize_);
                pixels_.assign(static_cast<std::size_t>(textureSize_) * textureSize_, Color());

                dynamicVertices_ = std::make_unique<DynamicVertexBuffer>(
                    device, VertexPositionColor::getVertexDeclarationStatic(), vertexCount_,
                    BufferUsage::WriteOnly);
                vertices_.assign(static_cast<std::size_t>(vertexCount_), VertexPositionColor());

                batch_ = std::make_unique<SpriteBatch>(device);
                effect_ = std::make_unique<BasicEffect>(device);
                effect_->setVertexColorEnabledProperty(true);
                effect_->setLightingEnabledProperty(false);
                effect_->setTextureEnabledProperty(false);

                viewportWidth_ = context.width;
                viewportHeight_ = context.height;
            }

            void Unload() override
            {
                effect_.reset();
                batch_.reset();
                dynamicVertices_.reset();
                texture_.reset();
                pixels_.clear();
                pixels_.shrink_to_fit();
                vertices_.clear();
                vertices_.shrink_to_fit();
            }

            void Render(const SceneContext& context, int frameIndex, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                ResetTo3DDefaults(device);
                device.Clear(Color(9, 11, 16, 255), 1.0f);

                const auto time = static_cast<float>(sceneTime);

                // --- Texture path -------------------------------------------------------
                // Every texel is recomputed: a partial update would let a renderer that
                // uploads the whole surface anyway look identical to one that does not.
                const float wave = time * 3.0f;
                for (int y = 0; y < textureSize_; ++y)
                {
                    const float fy = static_cast<float>(y) * 0.05f;
                    for (int x = 0; x < textureSize_; ++x)
                    {
                        const float fx = static_cast<float>(x) * 0.05f;
                        const float value = 0.5f + 0.5f * std::sin(fx + wave) *
                                                       std::cos(fy - wave * 0.7f);
                        const auto level = static_cast<std::uint8_t>(value * 255.0f);
                        pixels_[static_cast<std::size_t>(y) * textureSize_ + x] =
                            Color(level, static_cast<std::uint8_t>(255 - level),
                                  static_cast<std::uint8_t>(160),
                                  static_cast<std::uint8_t>(255));
                    }
                }
                texture_->SetData(pixels_.data(), static_cast<int>(pixels_.size()));

                // --- Dynamic vertex path ------------------------------------------------
                const int triangleCount = vertexCount_ / 3;
                for (int triangle = 0; triangle < triangleCount; ++triangle)
                {
                    const float t = static_cast<float>(triangle) /
                                    static_cast<float>(std::max(1, triangleCount));
                    const float angle = t * 2.0f * kPi * 12.0f + time * 0.8f;
                    const float radius = 2.0f + 16.0f * t;
                    const Vector3 centre(std::cos(angle) * radius,
                                         6.0f * std::sin(angle * 0.5f + time),
                                         std::sin(angle) * radius);
                    const float size = 0.20f + 0.20f * std::sin(t * 30.0f + time * 2.0f);
                    const Color tint(static_cast<std::uint8_t>(90 + 165 * t),
                                     static_cast<std::uint8_t>(200 - 120 * t),
                                     static_cast<std::uint8_t>(160),
                                     static_cast<std::uint8_t>(255));

                    const std::size_t base = static_cast<std::size_t>(triangle) * 3;
                    vertices_[base + 0] = VertexPositionColor(
                        Vector3(centre.X - size, centre.Y - size, centre.Z), tint);
                    vertices_[base + 1] = VertexPositionColor(
                        Vector3(centre.X, centre.Y + size, centre.Z), tint);
                    vertices_[base + 2] = VertexPositionColor(
                        Vector3(centre.X + size, centre.Y - size, centre.Z), tint);
                }
                dynamicVertices_->SetData(vertices_.data(), 0, vertexCount_,
                                          SetDataOptions::Discard);

                effect_->setWorldProperty(Matrix::getIdentityProperty());
                effect_->setViewProperty(OrbitView(sceneTime, 44.0f, 12.0f, 20.0f));
                effect_->setProjectionProperty(StandardProjection(device, 200.0f));

                device.SetVertexBuffer(dynamicVertices_.get());
                for (auto& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty())
                {
                    pass.Apply();
                    device.DrawPrimitives(PrimitiveType::TriangleList, 0, triangleCount);
                }

                // --- Consume the streamed texture --------------------------------------
                // Drawn last and over a quarter of the screen, so the upload is genuinely
                // sampled this frame rather than merely written.
                const int side = std::min(viewportWidth_, viewportHeight_) / 2;
                batch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                              &SamplerState::LinearClamp, nullptr, nullptr);
                batch_->Draw(*texture_,
                             Rectangle(viewportWidth_ - side, viewportHeight_ - side, side, side),
                             std::optional<Rectangle>{},
                             Color(static_cast<std::uint8_t>(255), static_cast<std::uint8_t>(255),
                                   static_cast<std::uint8_t>(255), static_cast<std::uint8_t>(255)));
                batch_->End();

                (void)frameIndex;
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return TextureBytesPerFrame() + VertexBytesPerFrame();
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"textureEdge", static_cast<double>(textureSize_)},
                    {"textureBytesPerFrame", TextureBytesPerFrame()},
                    {"dynamicVertices", static_cast<double>(vertexCount_)},
                    {"vertexBytesPerFrame", VertexBytesPerFrame()},
                };
            }

        private:
            [[nodiscard]] double TextureBytesPerFrame() const
            {
                return static_cast<double>(textureSize_) * static_cast<double>(textureSize_) * 4.0;
            }

            [[nodiscard]] double VertexBytesPerFrame() const
            {
                return static_cast<double>(vertexCount_) * static_cast<double>(kVertexStride);
            }

            std::unique_ptr<Texture2D> texture_;
            std::unique_ptr<DynamicVertexBuffer> dynamicVertices_;
            std::unique_ptr<SpriteBatch> batch_;
            std::unique_ptr<BasicEffect> effect_;
            std::vector<Color> pixels_;
            std::vector<VertexPositionColor> vertices_;
            int textureSize_ = 0;
            int vertexCount_ = 0;
            int viewportWidth_ = 1;
            int viewportHeight_ = 1;
        };
    }

    std::unique_ptr<Scene> CreateStreamingScene()
    {
        return std::make_unique<StreamingScene>();
    }
} // namespace CnaBenchmark
