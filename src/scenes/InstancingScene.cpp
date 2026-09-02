// SPDX-License-Identifier: MS-PL
//
// Scene 6 -- Instancing.
//
// What it measures: `GraphicsDevice::DrawInstancedPrimitives` -- one mesh, one draw call, tens
// of thousands of copies, each placed by its own world matrix read from a second vertex stream
// at instance frequency.
//
// The instance buffer is uploaded once and never touched again, and only the camera moves. That
// is deliberate: rewriting it every frame would fold a per-frame upload into the number and make
// this scene a slower duplicate of the streaming scene. What is left is exactly the question
// this scene exists to ask -- how fast can this renderer replay one mesh from a per-instance
// stream.
//
// The mesh is position-plus-colour and the effect has lighting off. That is the instanced shape
// CNA's own cross-renderer instancing tests pin down on every renderer that implements the path,
// so a result here reflects the renderer's instancing throughput rather than a disagreement
// about how an instanced vertex shader should light a normal.
//
// Renderers without hardware instancing skip this scene and say so. Reporting nothing is the
// honest answer: emulating instancing with a loop of ordinary draws would produce a number that
// looks comparable and measures something else entirely.

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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Instances drawn per frame at workload scale 1. */
        constexpr int kNominalInstanceCount = 20000;
        /** @brief Edge length of the instanced cube. */
        constexpr float kCubeEdge = 0.5f;
        /** @brief Radius of the shell the instances are scattered over. */
        constexpr float kFieldRadius = 40.0f;
        /** @brief Bytes in one per-instance record: a 4x4 float world matrix. */
        constexpr int kInstanceStride = 64;

        /** @brief One per-instance world matrix, laid out as four consecutive float4 rows. */
        struct InstanceRecord
        {
            float row0[4];
            float row1[4];
            float row2[4];
            float row3[4];
        };

        /**
         * @brief The per-instance stream's layout: four float4 elements at TEXCOORD1..4.
         *
         * This is the shape CNA's instancing renderers already consume -- the same declaration
         * its own instanced-draw conformance tests bind -- rather than a layout invented here.
         */
        [[nodiscard]] VertexDeclaration InstanceDeclaration()
        {
            return VertexDeclaration(
                kInstanceStride,
                {
                    VertexElement(0, VertexElementFormat::Vector4,
                                  VertexElementUsage::TextureCoordinate, 1),
                    VertexElement(16, VertexElementFormat::Vector4,
                                  VertexElementUsage::TextureCoordinate, 2),
                    VertexElement(32, VertexElementFormat::Vector4,
                                  VertexElementUsage::TextureCoordinate, 3),
                    VertexElement(48, VertexElementFormat::Vector4,
                                  VertexElementUsage::TextureCoordinate, 4),
                });
        }

        class InstancingScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "instancing"; }
            [[nodiscard]] std::string_view Title() const override { return "Instancing"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "Tens of thousands of cubes drawn from one mesh in a single "
                       "DrawInstancedPrimitives call, placed by a per-instance matrix stream.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override { return "instances"; }

            [[nodiscard]] std::vector<SceneRequirement> Requirements() const override
            {
                return {{CNA::GraphicsCapability::ThreeD,
                         "instanced drawing is part of the 3D pipeline"},
                        {CNA::GraphicsCapability::Instancing,
                         "the scene draws through DrawInstancedPrimitives, which this renderer "
                         "does not implement"}};
            }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                Rng rng(context.seed ^ 0x1257A9CEULL);

                instanceCount_ = ScaledCount(kNominalInstanceCount, context.workloadScale);

                // The cube, as position plus colour.
                const MeshData cube = BuildCube(kCubeEdge);
                std::vector<VertexPositionColor> vertices;
                vertices.reserve(cube.vertices.size());
                for (const auto& source : cube.vertices)
                {
                    // Face colour from the face normal, so the six faces stay distinguishable
                    // and a renderer that dropped the colour stream is visible immediately.
                    const auto r = static_cast<std::uint8_t>(
                        110.0f + 110.0f * std::fabs(source.Normal.X));
                    const auto g = static_cast<std::uint8_t>(
                        110.0f + 110.0f * std::fabs(source.Normal.Y));
                    const auto b = static_cast<std::uint8_t>(
                        110.0f + 110.0f * std::fabs(source.Normal.Z));
                    vertices.push_back(VertexPositionColor(
                        source.Position, Color(r, g, b, static_cast<std::uint8_t>(255))));
                }

                meshVertexCount_ = static_cast<int>(vertices.size());
                meshPrimitiveCount_ = cube.PrimitiveCount();

                meshBuffer_ = std::make_unique<VertexBuffer>(
                    device, VertexPositionColor::getVertexDeclarationStatic(), meshVertexCount_,
                    BufferUsage::None);
                meshBuffer_->SetData(vertices.data(), meshVertexCount_);

                indexBuffer_ = std::make_unique<IndexBuffer>(
                    device, static_cast<int>(cube.indices.size()));
                indexBuffer_->SetData(cube.indices.data(),
                                      static_cast<int>(cube.indices.size()));

                std::vector<InstanceRecord> records;
                records.reserve(static_cast<std::size_t>(instanceCount_));
                for (int i = 0; i < instanceCount_; ++i)
                {
                    // A spherical shell rather than a solid ball: it keeps the instances
                    // spread across the screen instead of piling depth complexity in the
                    // middle, so the scene stays a test of instance throughput and not of
                    // overdraw.
                    const float theta = rng.NextFloat(0.0f, 2.0f * kPi);
                    const float cosPhi = rng.NextFloat(-1.0f, 1.0f);
                    const float sinPhi = std::sqrt(std::max(0.0f, 1.0f - cosPhi * cosPhi));
                    const float radius = kFieldRadius * (0.75f + 0.25f * rng.NextFloat());

                    const Matrix world =
                        Matrix::CreateRotationY(rng.NextFloat(0.0f, 2.0f * kPi)) *
                        Matrix::CreateRotationX(rng.NextFloat(0.0f, 2.0f * kPi)) *
                        Matrix::CreateTranslation(radius * sinPhi * std::cos(theta),
                                                  radius * cosPhi,
                                                  radius * sinPhi * std::sin(theta));
                    records.push_back(ToRecord(world));
                }

                instanceBuffer_ = std::make_unique<VertexBuffer>(
                    device, InstanceDeclaration(), instanceCount_, BufferUsage::None);
                instanceBuffer_->SetDataRaw(records.data(), instanceCount_, kInstanceStride);

                effect_ = std::make_unique<BasicEffect>(device);
                effect_->setVertexColorEnabledProperty(true);
                effect_->setLightingEnabledProperty(false);
                effect_->setTextureEnabledProperty(false);
                effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            }

            void Unload() override
            {
                effect_.reset();
                instanceBuffer_.reset();
                indexBuffer_.reset();
                meshBuffer_.reset();
            }

            void Render(const SceneContext& context, int /*frameIndex*/, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                ResetTo3DDefaults(device);
                device.Clear(Color(10, 12, 18, 255), 1.0f);

                effect_->setWorldProperty(Matrix::getIdentityProperty());
                effect_->setViewProperty(OrbitView(sceneTime, kFieldRadius * 1.9f,
                                                   kFieldRadius * 0.35f, 26.0f));
                effect_->setProjectionProperty(StandardProjection(device, kFieldRadius * 6.0f));

                device.SetVertexBuffers({VertexBufferBinding(meshBuffer_.get(), 0, 0),
                                         VertexBufferBinding(instanceBuffer_.get(), 0, 1)});
                device.Indices(indexBuffer_.get());

                for (auto& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty())
                {
                    pass.Apply();
                    device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                   meshVertexCount_, 0, meshPrimitiveCount_,
                                                   instanceCount_);
                }

                // The next scene binds one stream; leave the second binding behind rather than
                // letting it survive into a draw that never declared it.
                device.SetVertexBuffers({VertexBufferBinding(meshBuffer_.get(), 0, 0)});
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(instanceCount_);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"instances", static_cast<double>(instanceCount_)},
                    {"trianglesPerInstance", static_cast<double>(meshPrimitiveCount_)},
                    {"trianglesPerFrame",
                     static_cast<double>(instanceCount_) *
                         static_cast<double>(meshPrimitiveCount_)},
                    {"drawCallsPerFrame", 1.0},
                };
            }

        private:
            /**
             * @brief Copies a `Matrix` into the four float4 rows the instance stream expects.
             *
             * Written out element by element rather than memcpy'd: the record's layout is a
             * contract with the vertex declaration above, and spelling it out is what makes a
             * later change to either side visible.
             */
            [[nodiscard]] static InstanceRecord ToRecord(const Matrix& matrix)
            {
                InstanceRecord record{};
                record.row0[0] = matrix.M11;
                record.row0[1] = matrix.M12;
                record.row0[2] = matrix.M13;
                record.row0[3] = matrix.M14;
                record.row1[0] = matrix.M21;
                record.row1[1] = matrix.M22;
                record.row1[2] = matrix.M23;
                record.row1[3] = matrix.M24;
                record.row2[0] = matrix.M31;
                record.row2[1] = matrix.M32;
                record.row2[2] = matrix.M33;
                record.row2[3] = matrix.M34;
                record.row3[0] = matrix.M41;
                record.row3[1] = matrix.M42;
                record.row3[2] = matrix.M43;
                record.row3[3] = matrix.M44;
                return record;
            }

            std::unique_ptr<VertexBuffer> meshBuffer_;
            std::unique_ptr<VertexBuffer> instanceBuffer_;
            std::unique_ptr<IndexBuffer> indexBuffer_;
            std::unique_ptr<BasicEffect> effect_;
            int instanceCount_ = 0;
            int meshVertexCount_ = 0;
            int meshPrimitiveCount_ = 0;
        };
    }

    std::unique_ptr<Scene> CreateInstancingScene()
    {
        return std::make_unique<InstancingScene>();
    }
} // namespace CnaBenchmark
