// SPDX-License-Identifier: MS-PL
//
// Scene 5 -- Lights.
//
// What it measures: the cost of many dynamic lights on the same geometry, which is a different
// question from the terrain scene's raw triangle rate. The whole cube field is re-submitted
// once per light group and the results are accumulated additively, so the pixel and vertex work
// scales with the light count while the geometry stays fixed.
//
// Why multi-pass and not one big shader: `BasicEffect` is XNA's stock effect and it carries
// exactly three directional lights, on every renderer CNA has. Twelve lights therefore means
// four passes -- which is precisely how a fixed-light-count forward renderer does it, and it is
// expressible in the public API on every renderer with a 3D pipeline. A scene built on a custom
// twelve-light shader would run on the handful of renderers that compile custom effects and
// would have to be skipped everywhere else, which is the opposite of what a comparison suite
// needs.
//
// The accumulation is the classic one: the first pass writes depth and colour opaquely, and the
// later passes test depth without writing it and add their contribution. Ambient is applied in
// the first pass only, so the total is one ambient term plus twelve light terms rather than four
// ambient terms.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "Benchmark.hpp"
#include "Procedural.hpp"
#include "SceneRegistry.hpp"
#include "scenes/SceneCommon.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Cubes merged into the field at workload scale 1. */
        constexpr int kNominalCubeCount = 1200;
        /** @brief Dynamic lights. A multiple of three: BasicEffect carries three per pass. */
        constexpr int kLightCount = 12;
        /** @brief Lights BasicEffect applies in one pass. */
        constexpr int kLightsPerPass = 3;
        /** @brief Cubes per merged buffer, bounded by the 16-bit index range (24 vertices each). */
        constexpr int kCubesPerBatch = 2000;
        /** @brief Edge length of one cube. */
        constexpr float kCubeEdge = 1.1f;
        /** @brief Radius of the field the cubes are scattered over. */
        constexpr float kFieldRadius = 26.0f;

        /** @brief One merged buffer holding up to `kCubesPerBatch` cubes. */
        struct CubeBatch
        {
            std::unique_ptr<VertexBuffer> vertices;
            std::unique_ptr<IndexBuffer> indices;
            int vertexCount = 0;
            int primitiveCount = 0;
        };

        class LightsScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "lights"; }
            [[nodiscard]] std::string_view Title() const override { return "Lights"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "A cube field re-lit by twelve moving directional lights, accumulated "
                       "over four additive BasicEffect passes.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override
            {
                return "triangle-lights";
            }

            [[nodiscard]] std::vector<SceneRequirement> Requirements() const override
            {
                return {{CNA::GraphicsCapability::ThreeD,
                         "the light accumulation draws 3D geometry through vertex and index "
                         "buffers"},
                        {CNA::GraphicsCapability::DepthStencilBuffer,
                         "the additive passes must test the depth the first pass wrote"}};
            }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                Rng rng(context.seed ^ 0x11A57ULL);

                cubeCount_ = ScaledCount(kNominalCubeCount, context.workloadScale);
                texture_ = CreateAtlasTexture(device, 128, 2, rng);

                effect_ = std::make_unique<BasicEffect>(device);
                effect_->setTextureEnabledProperty(true);
                effect_->setTextureProperty(texture_.get());
                effect_->setLightingEnabledProperty(true);
                effect_->setPreferPerPixelLightingProperty(true);
                effect_->setDiffuseColorProperty(Vector3(0.80f, 0.80f, 0.84f));
                effect_->setSpecularColorProperty(Vector3(0.35f, 0.35f, 0.38f));
                effect_->setSpecularPowerProperty(24.0f);

                const MeshData unitCube = BuildCube(kCubeEdge);
                batches_.clear();
                totalPrimitives_ = 0;

                int remaining = cubeCount_;
                while (remaining > 0)
                {
                    const int inBatch = std::min(remaining, kCubesPerBatch);
                    remaining -= inBatch;

                    std::vector<VertexPositionNormalTexture> vertices;
                    std::vector<std::uint16_t> indices;
                    vertices.reserve(unitCube.vertices.size() * static_cast<std::size_t>(inBatch));
                    indices.reserve(unitCube.indices.size() * static_cast<std::size_t>(inBatch));

                    for (int cube = 0; cube < inBatch; ++cube)
                    {
                        // Scattered inside a disc rather than on a grid: a regular grid lets a
                        // tiled rasterizer hit an unrepresentatively friendly access pattern.
                        const float angle = rng.NextFloat(0.0f, 2.0f * kPi);
                        const float radius = kFieldRadius * std::sqrt(rng.NextFloat());
                        const Vector3 offset(std::cos(angle) * radius,
                                             rng.NextFloat(-3.0f, 3.0f),
                                             std::sin(angle) * radius);

                        const auto base = static_cast<std::uint16_t>(vertices.size());
                        for (const VertexPositionNormalTexture& vertex : unitCube.vertices)
                        {
                            VertexPositionNormalTexture moved = vertex;
                            moved.Position.X += offset.X;
                            moved.Position.Y += offset.Y;
                            moved.Position.Z += offset.Z;
                            vertices.push_back(moved);
                        }
                        for (const std::uint16_t index : unitCube.indices)
                            indices.push_back(static_cast<std::uint16_t>(base + index));
                    }

                    CubeBatch batch;
                    batch.vertexCount = static_cast<int>(vertices.size());
                    batch.primitiveCount = static_cast<int>(indices.size() / 3);
                    batch.vertices = std::make_unique<VertexBuffer>(
                        device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                        batch.vertexCount, BufferUsage::None);
                    batch.vertices->SetData(vertices.data(), batch.vertexCount);
                    batch.indices = std::make_unique<IndexBuffer>(
                        device, static_cast<int>(indices.size()));
                    batch.indices->SetData(indices.data(), static_cast<int>(indices.size()));
                    totalPrimitives_ += batch.primitiveCount;
                    batches_.push_back(std::move(batch));
                }
            }

            void Unload() override
            {
                batches_.clear();
                effect_.reset();
                texture_.reset();
                totalPrimitives_ = 0;
            }

            void Render(const SceneContext& context, int /*frameIndex*/, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                ResetTo3DDefaults(device);
                device.Clear(Color(8, 9, 14, 255), 1.0f);

                effect_->setWorldProperty(Matrix::getIdentityProperty());
                effect_->setViewProperty(OrbitView(sceneTime, 44.0f, 16.0f, 28.0f));
                effect_->setProjectionProperty(StandardProjection(device, 220.0f));

                const auto time = static_cast<float>(sceneTime);
                const int passCount = kLightCount / kLightsPerPass;

                for (int pass = 0; pass < passCount; ++pass)
                {
                    if (pass == 0)
                    {
                        device.setBlendStateProperty(BlendState::Opaque);
                        device.setDepthStencilStateProperty(DepthStencilState::Default);
                        effect_->setAmbientLightColorProperty(Vector3(0.06f, 0.06f, 0.08f));
                    }
                    else
                    {
                        device.setBlendStateProperty(BlendState::Additive);
                        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
                        effect_->setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                    }

                    ConfigureLights(pass, time);

                    auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
                    for (auto& effectPass : passes)
                    {
                        effectPass.Apply();
                        for (const CubeBatch& batch : batches_)
                        {
                            device.SetVertexBuffer(batch.vertices.get());
                            device.Indices(batch.indices.get());
                            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                         batch.vertexCount, 0,
                                                         batch.primitiveCount);
                        }
                    }
                }

                // Leave the device as the next scene expects to find it, rather than with the
                // last additive pass's blend and depth state still installed.
                ResetTo3DDefaults(device);
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(totalPrimitives_) * static_cast<double>(kLightCount);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"cubes", static_cast<double>(cubeCount_)},
                    {"lights", static_cast<double>(kLightCount)},
                    {"lightPasses", static_cast<double>(kLightCount / kLightsPerPass)},
                    {"trianglesPerFrame",
                     static_cast<double>(totalPrimitives_) *
                         static_cast<double>(kLightCount / kLightsPerPass)},
                    {"drawCallsPerFrame",
                     static_cast<double>(batches_.size()) *
                         static_cast<double>(kLightCount / kLightsPerPass)},
                };
            }

        private:
            /** @brief Points this pass's three directional lights, all of them moving. */
            void ConfigureLights(int pass, float time)
            {
                DirectionalLight* lights[kLightsPerPass] = {
                    &effect_->getDirectionalLight0Property(),
                    &effect_->getDirectionalLight1Property(),
                    &effect_->getDirectionalLight2Property(),
                };

                for (int slot = 0; slot < kLightsPerPass; ++slot)
                {
                    const int lightIndex = pass * kLightsPerPass + slot;
                    const float phase =
                        2.0f * kPi * static_cast<float>(lightIndex) /
                            static_cast<float>(kLightCount) +
                        time * 0.35f;

                    Vector3 direction(std::cos(phase), -0.45f - 0.35f * std::sin(phase * 0.7f),
                                      std::sin(phase));
                    direction.Normalize();

                    // Each light gets its own hue so a dropped pass is visible rather than
                    // merely dimmer, and the whole scene's contribution stays near white.
                    const float hue = static_cast<float>(lightIndex) /
                                      static_cast<float>(kLightCount);
                    const Vector3 colour(0.45f + 0.35f * std::cos(2.0f * kPi * hue),
                                         0.45f + 0.35f * std::cos(2.0f * kPi * (hue + 0.33f)),
                                         0.45f + 0.35f * std::cos(2.0f * kPi * (hue + 0.66f)));

                    lights[slot]->setEnabledProperty(true);
                    lights[slot]->setDirectionProperty(direction);
                    lights[slot]->setDiffuseColorProperty(colour);
                    lights[slot]->setSpecularColorProperty(
                        Vector3(colour.X * 0.5f, colour.Y * 0.5f, colour.Z * 0.5f));
                }
            }

            std::vector<CubeBatch> batches_;
            std::unique_ptr<BasicEffect> effect_;
            std::unique_ptr<Texture2D> texture_;
            int cubeCount_ = 0;
            int totalPrimitives_ = 0;
        };
    }

    std::unique_ptr<Scene> CreateLightsScene()
    {
        return std::make_unique<LightsScene>();
    }
} // namespace CnaBenchmark
