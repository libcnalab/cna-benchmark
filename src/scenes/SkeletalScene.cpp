// SPDX-License-Identifier: MS-PL
//
// Scene 3 -- Skeletal animation.
//
// What it measures: the GPU skinning path end to end. Every frame, every character's bone chain
// is re-evaluated on the CPU, the resulting palette is uploaded through
// `SkinnedEffect::SetBoneTransforms`, and every vertex is transformed by four weighted bone
// matrices in the vertex shader.
//
// Four influences per vertex, not one: `SkinnedTube` gives every vertex four non-zero weights
// that sum to one, so a renderer cannot take a cheaper single-bone path and still draw the same
// silhouette. That is also why the scene animates the bones rather than the world matrix -- a
// rotating rigid mesh in a skinned vertex format would measure nothing the terrain scene does
// not already measure.
//
// The palette upload is deliberately inside the measured frame. It is a per-character, per-frame
// cost in every real skinned renderer, and a benchmark that hoisted it out would report a number
// no game can reach.

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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Animated characters at workload scale 1. */
        constexpr int kNominalCharacterCount = 24;
        /** @brief Bones per character. Stays under `SkinnedEffect::MaxBones`. */
        constexpr int kBoneCount = 64;
        /** @brief Rings along each character's mesh. */
        constexpr int kRings = 48;
        /** @brief Vertices around each ring. */
        constexpr int kSegments = 24;

        class SkeletalScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "skeletal"; }
            [[nodiscard]] std::string_view Title() const override { return "Skeletal animation"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "Characters whose 64-bone palettes are re-evaluated and uploaded every "
                       "frame, skinned on the GPU with four influences per vertex.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override
            {
                return "skinned vertices";
            }

            [[nodiscard]] std::vector<SceneRequirement> Requirements() const override
            {
                return {{CNA::GraphicsCapability::ThreeD,
                         "skeletal animation needs the 3D vertex/index pipeline and a skinning "
                         "vertex shader"}};
            }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                Rng rng(context.seed ^ 0x5CE1E7A1ULL);

                const SkinnedMeshData mesh =
                    BuildSkinnedTube(kRings, kSegments, kBoneCount, 6.0f, 0.55f);
                vertexCount_ = static_cast<int>(mesh.vertices.size());
                primitiveCount_ = mesh.PrimitiveCount();

                vertexBuffer_ = std::make_unique<VertexBuffer>(
                    device, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(),
                    vertexCount_, BufferUsage::None);
                vertexBuffer_->SetData(mesh.vertices.data(), vertexCount_);

                indexBuffer_ = std::make_unique<IndexBuffer>(
                    device, static_cast<int>(mesh.indices.size()));
                indexBuffer_->SetData(mesh.indices.data(), static_cast<int>(mesh.indices.size()));

                texture_ = CreateAtlasTexture(device, 128, 2, rng);

                effect_ = std::make_unique<SkinnedEffect>(device);
                effect_->setWeightsPerVertexProperty(4);
                effect_->setTextureProperty(texture_.get());
                effect_->EnableDefaultLighting();
                effect_->setDiffuseColorProperty(Vector3(0.85f, 0.86f, 0.92f));

                characterCount_ = ScaledCount(kNominalCharacterCount, context.workloadScale);
                characterPhase_.clear();
                characterPosition_.clear();
                characterPhase_.reserve(static_cast<std::size_t>(characterCount_));
                characterPosition_.reserve(static_cast<std::size_t>(characterCount_));

                // Laid out on a ring around the origin, so the shared orbit camera sees every
                // character at a comparable distance and the scene is not accidentally measuring
                // how well one renderer culls the far ones.
                for (int i = 0; i < characterCount_; ++i)
                {
                    const float angle =
                        2.0f * kPi * static_cast<float>(i) / static_cast<float>(characterCount_);
                    const float radius = 9.0f + 2.5f * static_cast<float>(i % 3);
                    characterPosition_.push_back(
                        Vector3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius));
                    characterPhase_.push_back(rng.NextFloat(0.0f, 2.0f * kPi));
                }

                palette_.assign(static_cast<std::size_t>(kBoneCount),
                                Matrix::getIdentityProperty());
            }

            void Unload() override
            {
                effect_.reset();
                texture_.reset();
                indexBuffer_.reset();
                vertexBuffer_.reset();
                characterPhase_.clear();
                characterPosition_.clear();
                palette_.clear();
            }

            void Render(const SceneContext& context, int /*frameIndex*/, double sceneTime) override
            {
                GraphicsDevice& device = *context.device;
                ResetTo3DDefaults(device);
                device.Clear(Color(16, 18, 26, 255), 1.0f);

                effect_->setViewProperty(OrbitView(sceneTime, 22.0f, 7.0f, 24.0f));
                effect_->setProjectionProperty(StandardProjection(device, 200.0f));

                device.SetVertexBuffer(vertexBuffer_.get());
                device.Indices(indexBuffer_.get());

                const auto time = static_cast<float>(sceneTime);
                for (int character = 0; character < characterCount_; ++character)
                {
                    const auto index = static_cast<std::size_t>(character);
                    BuildPalette(time, characterPhase_[index]);
                    effect_->SetBoneTransforms(palette_);
                    effect_->setWorldProperty(
                        Matrix::CreateTranslation(characterPosition_[index]));

                    for (auto& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty())
                    {
                        pass.Apply();
                        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                     vertexCount_, 0, primitiveCount_);
                    }
                }
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(characterCount_) * static_cast<double>(vertexCount_);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"characters", static_cast<double>(characterCount_)},
                    {"bonesPerCharacter", static_cast<double>(kBoneCount)},
                    {"verticesPerCharacter", static_cast<double>(vertexCount_)},
                    {"trianglesPerFrame",
                     static_cast<double>(characterCount_) * static_cast<double>(primitiveCount_)},
                    {"boneMatrixUploadsPerFrame",
                     static_cast<double>(characterCount_) * static_cast<double>(kBoneCount)},
                };
            }

        private:
            /**
             * @brief Re-evaluates the whole bone chain into `palette_`.
             *
             * A genuine chain, not 64 independent matrices: each bone's transform is composed
             * onto its parent's, so the cost and the numerical behaviour match a real skeleton
             * rather than a flat array a compiler could vectorise into something unrepresentative.
             */
            void BuildPalette(float time, float phase)
            {
                Matrix accumulated = Matrix::getIdentityProperty();
                const float segment = 6.0f / static_cast<float>(kBoneCount - 1);

                for (int bone = 0; bone < kBoneCount; ++bone)
                {
                    const float t = static_cast<float>(bone) / static_cast<float>(kBoneCount - 1);
                    const float bend = 0.22f * std::sin(time * 2.1f + phase + t * 5.0f);
                    const float twist = 0.14f * std::cos(time * 1.7f + phase + t * 3.0f);

                    const Matrix local = Matrix::CreateRotationZ(bend) *
                                         Matrix::CreateRotationY(twist) *
                                         Matrix::CreateTranslation(0.0f, segment, 0.0f);
                    accumulated = local * accumulated;

                    // The palette entry is the bone's world transform relative to the bind pose,
                    // which for this straight bind chain is the accumulated transform with the
                    // bind offset removed.
                    palette_[static_cast<std::size_t>(bone)] =
                        Matrix::CreateTranslation(0.0f, -segment * static_cast<float>(bone), 0.0f) *
                        accumulated;
                }
            }

            std::unique_ptr<VertexBuffer> vertexBuffer_;
            std::unique_ptr<IndexBuffer> indexBuffer_;
            std::unique_ptr<Texture2D> texture_;
            std::unique_ptr<SkinnedEffect> effect_;
            std::vector<Vector3> characterPosition_;
            std::vector<float> characterPhase_;
            std::vector<Matrix> palette_;
            int characterCount_ = 0;
            int vertexCount_ = 0;
            int primitiveCount_ = 0;
        };
    }

    std::unique_ptr<Scene> CreateSkeletalScene()
    {
        return std::make_unique<SkeletalScene>();
    }
} // namespace CnaBenchmark
