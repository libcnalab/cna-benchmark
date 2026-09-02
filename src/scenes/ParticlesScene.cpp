// SPDX-License-Identifier: MS-PL
//
// Scene 2 -- Particles.
//
// What it measures: two costs at once, on purpose, because a particle system pays both. The CPU
// side integrates tens of thousands of positions and velocities every frame and streams the
// results into a batch; the GPU side blends every one of those quads additively over what is
// already there, which is overdraw the renderer cannot cull.
//
// The simulation is a fixed-step Euler integration -- one step per rendered frame, never a
// wall-clock delta. A variable step would make a fast renderer simulate a different scene than
// a slow one, and the two results would not be comparable no matter how carefully they were
// timed.
//
// Additive blending is the point of the scene, and CNA reports per renderer whether its
// additive path is the real one (`GraphicsCapability::AdditiveBlending`). The scene runs either
// way -- a renderer that silently degrades to source-over is still doing real blending work --
// but it records the answer, because two runs that blended differently are not a fair pair.

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "Benchmark.hpp"
#include "Procedural.hpp"
#include "SceneRegistry.hpp"
#include "scenes/SceneCommon.hpp"

#include "CNA/GraphicsCapability.hpp"
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
        /** @brief Live particles at workload scale 1. */
        constexpr int kNominalParticleCount = 20000;
        /** @brief Edge length of the soft particle sprite. */
        constexpr int kParticleTextureSize = 32;
        /** @brief The simulation's fixed time step: one sixtieth of a second per frame. */
        constexpr float kSimulationStep = 1.0f / 60.0f;
        /** @brief Emitters the particles are recycled through. */
        constexpr int kEmitterCount = 6;

        /** @brief One simulated particle. Kept small so the update stays memory-bound, as a
         *         real particle system's update is. */
        struct Particle
        {
            float x;
            float y;
            float velocityX;
            float velocityY;
            float age;
            float lifetime;
            int emitter;
        };

        class ParticlesScene final : public Scene
        {
        public:
            [[nodiscard]] std::string_view Id() const override { return "particles"; }
            [[nodiscard]] std::string_view Title() const override { return "Particles"; }
            [[nodiscard]] std::string_view Description() const override
            {
                return "A fixed-step CPU particle simulation whose every particle is drawn as "
                       "an additively blended, alpha-faded sprite.";
            }
            [[nodiscard]] std::string_view WorkUnitName() const override { return "particles"; }

            void Load(const SceneContext& context) override
            {
                GraphicsDevice& device = *context.device;
                rng_ = std::make_unique<Rng>(context.seed ^ 0x9A1F2B3CULL);
                texture_ = CreateParticleTexture(device, kParticleTextureSize);
                batch_ = std::make_unique<SpriteBatch>(device);

                viewportWidth_ = static_cast<float>(context.width);
                viewportHeight_ = static_cast<float>(context.height);

                genuineAdditive_ =
                    device.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending);

                for (int i = 0; i < kEmitterCount; ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(kEmitterCount);
                    emitterX_[i] = viewportWidth_ * (0.15f + 0.7f * t);
                    emitterY_[i] = viewportHeight_ * (i % 2 == 0 ? 0.80f : 0.62f);
                    emitterTint_[i] = Color(static_cast<std::uint8_t>(120 + 22 * i),
                                            static_cast<std::uint8_t>(220 - 18 * i),
                                            static_cast<std::uint8_t>(160 + 15 * i),
                                            static_cast<std::uint8_t>(255));
                }

                particleCount_ = ScaledCount(kNominalParticleCount, context.workloadScale);
                particles_.clear();
                particles_.reserve(static_cast<std::size_t>(particleCount_));
                for (int i = 0; i < particleCount_; ++i)
                {
                    Particle particle{};
                    Respawn(particle, i % kEmitterCount);
                    // Spread the initial ages so the very first frame already shows a settled
                    // system rather than one dense flash that would skew the warm-up.
                    particle.age = rng_->NextFloat(0.0f, particle.lifetime);
                    particles_.push_back(particle);
                }
            }

            void Unload() override
            {
                batch_.reset();
                texture_.reset();
                particles_.clear();
                rng_.reset();
            }

            void Render(const SceneContext& context, int /*frameIndex*/,
                        double /*sceneTime*/) override
            {
                GraphicsDevice& device = *context.device;
                device.Clear(Color(6, 7, 12, 255));

                for (Particle& particle : particles_)
                {
                    particle.velocityY += 90.0f * kSimulationStep;
                    particle.x += particle.velocityX * kSimulationStep;
                    particle.y += particle.velocityY * kSimulationStep;
                    particle.age += kSimulationStep;
                    if (particle.age >= particle.lifetime)
                        Respawn(particle, particle.emitter);
                }

                const float origin = static_cast<float>(kParticleTextureSize) * 0.5f;

                batch_->Begin(SpriteSortMode::Deferred, BlendState::Additive,
                              &SamplerState::LinearClamp, nullptr, nullptr);
                for (const Particle& particle : particles_)
                {
                    const float life = particle.age / particle.lifetime;
                    const float fade = 1.0f - life;
                    const Color base = emitterTint_[particle.emitter];
                    const Color tint(static_cast<std::uint8_t>(base.getRProperty() * fade),
                                     static_cast<std::uint8_t>(base.getGProperty() * fade),
                                     static_cast<std::uint8_t>(base.getBProperty() * fade),
                                     static_cast<std::uint8_t>(255.0f * fade));
                    batch_->Draw(*texture_, Vector2(particle.x, particle.y),
                                 std::optional<Rectangle>{}, tint, 0.0f,
                                 Vector2(origin, origin), 0.35f + 0.65f * life,
                                 SpriteEffects::None, 0.0f);
                }
                batch_->End();
            }

            [[nodiscard]] double WorkUnitsPerFrame() const override
            {
                return static_cast<double>(particleCount_);
            }

            [[nodiscard]] std::vector<SceneDetail> Detail() const override
            {
                return {
                    {"particlesPerFrame", static_cast<double>(particleCount_)},
                    {"emitters", static_cast<double>(kEmitterCount)},
                    {"genuineAdditiveBlending", genuineAdditive_ ? 1.0 : 0.0},
                };
            }

        private:
            void Respawn(Particle& particle, int emitter)
            {
                particle.emitter = emitter;
                particle.x = emitterX_[emitter];
                particle.y = emitterY_[emitter];
                particle.velocityX = rng_->NextFloat(-90.0f, 90.0f);
                particle.velocityY = rng_->NextFloat(-260.0f, -110.0f);
                particle.age = 0.0f;
                particle.lifetime = rng_->NextFloat(1.4f, 3.2f);
            }

            std::unique_ptr<Texture2D> texture_;
            std::unique_ptr<SpriteBatch> batch_;
            std::unique_ptr<Rng> rng_;
            std::vector<Particle> particles_;
            float emitterX_[kEmitterCount]{};
            float emitterY_[kEmitterCount]{};
            Color emitterTint_[kEmitterCount]{};
            int particleCount_ = 0;
            float viewportWidth_ = 1.0f;
            float viewportHeight_ = 1.0f;
            bool genuineAdditive_ = false;
        };
    }

    std::unique_ptr<Scene> CreateParticlesScene()
    {
        return std::make_unique<ParticlesScene>();
    }
} // namespace CnaBenchmark
