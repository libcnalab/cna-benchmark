// SPDX-License-Identifier: MS-PL
//
// Scene 4 -- Terrain.
//
// What it measures: sustained indexed triangle throughput with real per-pixel work attached.
// The landscape is a tiled height field of large indexed chunks -- hundreds of thousands of
// lit, textured triangles per frame -- drawn with the stock `BasicEffect` and no culling of any
// kind.
//
// No culling is a deliberate choice, not an omission. A frustum culler would turn this into a
// benchmark of the culler, and the number would then depend on where the camera happened to
// point. Every chunk is submitted every frame, so the workload is exactly the same on every
// renderer and every run.
//
// The height field is a pure function of world position, which is what lets neighbouring chunks
// meet without a seam even though each is built independently, and why the vertex normals are
// the field's analytic gradient rather than a finite difference that would change with the
// tessellation.

#include <cmath>
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
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Chunks drawn at workload scale 1. */
        constexpr int kNominalChunkCount = 16;
        /** @brief Quads per chunk axis. 96 keeps a chunk's 9409 vertices inside 16-bit indices. */
        constexpr int kChunkCells = 96;
        /** @brief World edge length of one chunk. */
        constexpr float kChunkWorldSize = 30.0f;
        /** @brief Peak-to-trough height of the field. */
        constexpr float kAmplitude = 3.2f;

        /** @brief One uploaded terrain chunk. */
        struct Chunk
        {
            std::unique_ptr<VertexBuffer> vertices;
            std::unique_ptr<IndexBuffer> indices;
            int vertexCount = 0;
            int primitiveCount = 0;
        };

        class TerrainScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "terrain"; }
            [[nodiscard]] std::string_view Title() const override { return "Terrain"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "A tiled height field of large indexed chunks, lit and textured through "
                       "BasicEffect and submitted in full every frame with no culling.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override { return "triangles"; }

            [[nodiscard]] std::vector<SceneRequirement> Requirements() const override
            {
                return {{CNA::GraphicsCapability::ThreeD,
                         "the terrain is indexed 3D geometry drawn through vertex and index "
                         "buffers"}};
            }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                Rng rng(context.seed ^ 0x7E44A171ULL);

                chunkCount_ = ScaledCount(kNominalChunkCount, context.workloadScale);
                // Laid out as the squarest grid that holds the requested chunk count, so the
                // landscape stays roughly circular around the orbit camera at every scale.
                chunksPerAxis_ = static_cast<int>(std::ceil(std::sqrt(
                    static_cast<double>(chunkCount_))));

                texture_ = CreateAtlasTexture(device, 256, 2, rng);

                effect_ = std::make_unique<BasicEffect>(device);
                effect_->setTextureEnabledProperty(true);
                effect_->setTextureProperty(texture_.get());
                effect_->setLightingEnabledProperty(true);
                effect_->setPreferPerPixelLightingProperty(true);
                effect_->EnableDefaultLighting();
                effect_->setDiffuseColorProperty(Vector3(0.72f, 0.78f, 0.66f));

                chunks_.clear();
                totalPrimitives_ = 0;
                const float span = static_cast<float>(chunksPerAxis_) * kChunkWorldSize;
                for (int index = 0; index < chunkCount_; ++index)
                {
                    const int gridX = index % chunksPerAxis_;
                    const int gridZ = index / chunksPerAxis_;
                    const float originX =
                        static_cast<float>(gridX) * kChunkWorldSize - span * 0.5f;
                    const float originZ =
                        static_cast<float>(gridZ) * kChunkWorldSize - span * 0.5f;

                    const MeshData mesh = BuildTerrainChunk(kChunkCells, kChunkWorldSize,
                                                            originX, originZ, kAmplitude);
                    Chunk chunk;
                    chunk.vertexCount = static_cast<int>(mesh.vertices.size());
                    chunk.primitiveCount = mesh.PrimitiveCount();
                    chunk.vertices = std::make_unique<VertexBuffer>(
                        device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                        chunk.vertexCount, BufferUsage::None);
                    chunk.vertices->SetData(mesh.vertices.data(), chunk.vertexCount);
                    chunk.indices = std::make_unique<IndexBuffer>(
                        device, static_cast<int>(mesh.indices.size()));
                    chunk.indices->SetData(mesh.indices.data(),
                                           static_cast<int>(mesh.indices.size()));
                    totalPrimitives_ += chunk.primitiveCount;
                    chunks_.push_back(std::move(chunk));
                }
            }

            void Unload() override
            {
                chunks_.clear();
                effect_.reset();
                texture_.reset();
                totalPrimitives_ = 0;
            }

            void Render(const SceneContext& context, int /*frameIndex*/, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                ResetTo3DDefaults(device);
                // The height field is a two-sided surface at grazing angles; culling it would
                // make the measured triangle count depend on the camera.
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.Clear(Color(118, 152, 190, 255), 1.0f);

                const float span = static_cast<float>(chunksPerAxis_) * kChunkWorldSize;
                effect_->setWorldProperty(Matrix::getIdentityProperty());
                effect_->setViewProperty(OrbitView(sceneTime, span * 0.62f, span * 0.30f, 30.0f));
                effect_->setProjectionProperty(StandardProjection(device, span * 3.0f));

                for (auto& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty())
                {
                    pass.Apply();
                    for (const Chunk& chunk : chunks_)
                    {
                        device.SetVertexBuffer(chunk.vertices.get());
                        device.Indices(chunk.indices.get());
                        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                     chunk.vertexCount, 0, chunk.primitiveCount);
                    }
                }
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(totalPrimitives_);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"chunks", static_cast<double>(chunkCount_)},
                    {"cellsPerChunkAxis", static_cast<double>(kChunkCells)},
                    {"trianglesPerFrame", static_cast<double>(totalPrimitives_)},
                    {"drawCallsPerFrame", static_cast<double>(chunkCount_)},
                };
            }

        private:
            std::vector<Chunk> chunks_;
            std::unique_ptr<BasicEffect> effect_;
            std::unique_ptr<Texture2D> texture_;
            int chunkCount_ = 0;
            int chunksPerAxis_ = 1;
            int totalPrimitives_ = 0;
        };
    }

    std::unique_ptr<Scene> CreateTerrainScene()
    {
        return std::make_unique<TerrainScene>();
    }
} // namespace CnaBenchmark
