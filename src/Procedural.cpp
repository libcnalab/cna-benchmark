// SPDX-License-Identifier: MS-PL

#include "Procedural.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;

namespace CnaBenchmark
{
    Rng::Rng(std::uint64_t seed)
        : state_(seed != 0 ? seed : 0x9E3779B97F4A7C15ULL)
    {
    }

    std::uint64_t Rng::NextUInt64()
    {
        // xorshift64*: three shifts and a multiply, identical on every platform and compiler.
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

    float Rng::NextFloat()
    {
        // The top 24 bits, which is exactly a float's mantissa: taking the low bits of an
        // xorshift generator samples its weakest bits.
        return static_cast<float>(NextUInt64() >> 40) / 16777216.0f;
    }

    float Rng::NextFloat(float min, float max)
    {
        return min + (max - min) * NextFloat();
    }

    int Rng::NextInt(int exclusiveMax)
    {
        if (exclusiveMax <= 0)
            throw std::invalid_argument("Rng::NextInt requires a positive bound");
        return static_cast<int>(NextUInt64() % static_cast<std::uint64_t>(exclusiveMax));
    }

    std::unique_ptr<Texture2D> CreateAtlasTexture(GraphicsDevice& device, int size, int tiles,
                                                  Rng& rng)
    {
        if (size <= 0 || tiles <= 0 || size % tiles != 0)
            throw std::invalid_argument(
                "CreateAtlasTexture: size must be a positive multiple of tiles");

        const int tileSize = size / tiles;
        const int checkerSize = std::max(2, tileSize / 8);

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
        for (int tileY = 0; tileY < tiles; ++tileY)
        {
            for (int tileX = 0; tileX < tiles; ++tileX)
            {
                const auto baseR = static_cast<std::uint8_t>(rng.NextInt(160) + 64);
                const auto baseG = static_cast<std::uint8_t>(rng.NextInt(160) + 64);
                const auto baseB = static_cast<std::uint8_t>(rng.NextInt(160) + 64);

                for (int y = 0; y < tileSize; ++y)
                {
                    for (int x = 0; x < tileSize; ++x)
                    {
                        const bool dark = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;
                        const int px = tileX * tileSize + x;
                        const int py = tileY * tileSize + y;
                        const std::size_t offset =
                            (static_cast<std::size_t>(py) * size + px) * 4;
                        const float shade = dark ? 0.55f : 1.0f;
                        rgba[offset + 0] = static_cast<std::uint8_t>(baseR * shade);
                        rgba[offset + 1] = static_cast<std::uint8_t>(baseG * shade);
                        rgba[offset + 2] = static_cast<std::uint8_t>(baseB * shade);
                        rgba[offset + 3] = 255;
                    }
                }
            }
        }

        return std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, size, size, rgba));
    }

    std::unique_ptr<Texture2D> CreateParticleTexture(GraphicsDevice& device, int size)
    {
        if (size <= 0)
            throw std::invalid_argument("CreateParticleTexture: size must be positive");

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
        const float centre = static_cast<float>(size - 1) * 0.5f;
        const float maxRadius = centre;

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float dx = static_cast<float>(x) - centre;
                const float dy = static_cast<float>(y) - centre;
                const float distance = std::sqrt(dx * dx + dy * dy) / maxRadius;
                // A squared falloff rather than a linear one: it keeps a bright core, which is
                // what makes the additive-blending cost of the particle scene visible.
                const float falloff = std::clamp(1.0f - distance, 0.0f, 1.0f);
                const auto alpha = static_cast<std::uint8_t>(falloff * falloff * 255.0f);
                const std::size_t offset = (static_cast<std::size_t>(y) * size + x) * 4;
                rgba[offset + 0] = 255;
                rgba[offset + 1] = 255;
                rgba[offset + 2] = 255;
                rgba[offset + 3] = alpha;
            }
        }

        return std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, size, size, rgba));
    }

    MeshData BuildCube(float edge)
    {
        const float h = edge * 0.5f;

        // Six independent faces: a shared-corner cube cannot carry per-face normals, and a
        // lighting scene built on smoothed cube corners measures the wrong thing.
        struct Face
        {
            Vector3 normal;
            Vector3 corner0;
            Vector3 uEdge;
            Vector3 vEdge;
        };

        const Face faces[6] = {
            {{0, 0, 1}, {-h, -h, h}, {edge, 0, 0}, {0, edge, 0}},
            {{0, 0, -1}, {h, -h, -h}, {-edge, 0, 0}, {0, edge, 0}},
            {{1, 0, 0}, {h, -h, h}, {0, 0, -edge}, {0, edge, 0}},
            {{-1, 0, 0}, {-h, -h, -h}, {0, 0, edge}, {0, edge, 0}},
            {{0, 1, 0}, {-h, h, h}, {edge, 0, 0}, {0, 0, -edge}},
            {{0, -1, 0}, {-h, -h, -h}, {edge, 0, 0}, {0, 0, edge}},
        };

        MeshData mesh;
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);

        for (const Face& face : faces)
        {
            const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
            for (int corner = 0; corner < 4; ++corner)
            {
                const float u = (corner == 1 || corner == 2) ? 1.0f : 0.0f;
                const float v = (corner == 2 || corner == 3) ? 1.0f : 0.0f;
                Vector3 position = face.corner0;
                position.X += face.uEdge.X * u + face.vEdge.X * v;
                position.Y += face.uEdge.Y * u + face.vEdge.Y * v;
                position.Z += face.uEdge.Z * u + face.vEdge.Z * v;
                mesh.vertices.push_back(
                    VertexPositionNormalTexture(position, face.normal, Vector2(u, v)));
            }
            const std::uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
            for (const std::uint16_t index : quad)
                mesh.indices.push_back(static_cast<std::uint16_t>(base + index));
        }

        return mesh;
    }

    namespace
    {
        /**
         * @brief A cheap, continuous, seed-free height field.
         *
         * Continuous across chunk boundaries because it is a pure function of world position,
         * which is what lets the terrain scene tile several chunks into one landscape without
         * seams or a shared generator.
         */
        float TerrainHeight(float x, float z)
        {
            return std::sin(x * 0.35f) * std::cos(z * 0.27f) +
                   0.45f * std::sin(x * 0.91f + z * 0.63f) +
                   0.20f * std::cos(x * 2.10f - z * 1.70f);
        }
    }

    MeshData BuildTerrainChunk(int cells, float worldSize, float originX, float originZ,
                               float amplitude)
    {
        if (cells <= 0 || cells > 255)
            throw std::invalid_argument("BuildTerrainChunk: cells must be within 1..255");

        const int side = cells + 1;
        const float step = worldSize / static_cast<float>(cells);

        MeshData mesh;
        mesh.vertices.reserve(static_cast<std::size_t>(side) * side);
        mesh.indices.reserve(static_cast<std::size_t>(cells) * cells * 6);

        for (int z = 0; z < side; ++z)
        {
            for (int x = 0; x < side; ++x)
            {
                const float worldX = originX + static_cast<float>(x) * step;
                const float worldZ = originZ + static_cast<float>(z) * step;
                const float height = TerrainHeight(worldX, worldZ) * amplitude;

                // The analytic gradient of the same field, so the normals are exact rather
                // than a finite difference that would change with the tessellation.
                const float dx =
                    amplitude *
                    (0.35f * std::cos(worldX * 0.35f) * std::cos(worldZ * 0.27f) +
                     0.45f * 0.91f * std::cos(worldX * 0.91f + worldZ * 0.63f) -
                     0.20f * 2.10f * std::sin(worldX * 2.10f - worldZ * 1.70f));
                const float dz =
                    amplitude *
                    (-0.27f * std::sin(worldX * 0.35f) * std::sin(worldZ * 0.27f) +
                     0.45f * 0.63f * std::cos(worldX * 0.91f + worldZ * 0.63f) +
                     0.20f * 1.70f * std::sin(worldX * 2.10f - worldZ * 1.70f));
                Vector3 normal(-dx, 1.0f, -dz);
                normal.Normalize();

                mesh.vertices.push_back(VertexPositionNormalTexture(
                    Vector3(worldX, height, worldZ), normal,
                    Vector2(static_cast<float>(x) * 0.25f, static_cast<float>(z) * 0.25f)));
            }
        }

        for (int z = 0; z < cells; ++z)
        {
            for (int x = 0; x < cells; ++x)
            {
                const auto topLeft = static_cast<std::uint16_t>(z * side + x);
                const auto topRight = static_cast<std::uint16_t>(topLeft + 1);
                const auto bottomLeft = static_cast<std::uint16_t>(topLeft + side);
                const auto bottomRight = static_cast<std::uint16_t>(bottomLeft + 1);
                mesh.indices.push_back(topLeft);
                mesh.indices.push_back(bottomLeft);
                mesh.indices.push_back(topRight);
                mesh.indices.push_back(topRight);
                mesh.indices.push_back(bottomLeft);
                mesh.indices.push_back(bottomRight);
            }
        }

        return mesh;
    }

    SkinnedMeshData BuildSkinnedTube(int rings, int segments, int bones, float length,
                                     float radius)
    {
        if (rings < 2 || segments < 3 || bones < 2)
            throw std::invalid_argument(
                "BuildSkinnedTube: needs >=2 rings, >=3 segments, >=2 bones");

        const int ringCount = rings + 1;
        if (static_cast<long long>(ringCount) * segments > 65536)
            throw std::invalid_argument(
                "BuildSkinnedTube: vertex count exceeds the 16-bit index range");

        SkinnedMeshData mesh;
        mesh.vertices.reserve(static_cast<std::size_t>(ringCount) * segments);
        mesh.indices.reserve(static_cast<std::size_t>(rings) * segments * 6);

        constexpr float kTwoPi = 6.28318530717958647692f;

        for (int ring = 0; ring < ringCount; ++ring)
        {
            const float t = static_cast<float>(ring) / static_cast<float>(rings);
            const float y = (t - 0.5f) * length;

            // Where this ring sits in the bone chain. The two neighbouring bones take the bulk
            // of the weight and the next two take a small share, so all four influences carry
            // a non-zero weight and the skinning path is exercised in full.
            const float bonePosition = t * static_cast<float>(bones - 1);
            const int baseBone = std::min(static_cast<int>(bonePosition), bones - 2);
            const float blend = bonePosition - static_cast<float>(baseBone);

            const int boneIndices[4] = {
                baseBone,
                baseBone + 1,
                std::min(baseBone + 2, bones - 1),
                std::max(baseBone - 1, 0),
            };
            float weights[4] = {
                (1.0f - blend) * 0.90f,
                blend * 0.90f,
                0.06f,
                0.04f,
            };
            const float weightSum = weights[0] + weights[1] + weights[2] + weights[3];
            for (float& weight : weights)
                weight /= weightSum;

            for (int segment = 0; segment < segments; ++segment)
            {
                const float angle =
                    kTwoPi * static_cast<float>(segment) / static_cast<float>(segments);
                const float nx = std::cos(angle);
                const float nz = std::sin(angle);

                mesh.vertices.push_back(VertexPositionNormalTextureSkinned(
                    Vector3(nx * radius, y, nz * radius),
                    Vector3(nx, 0.0f, nz),
                    Vector2(static_cast<float>(segment) / static_cast<float>(segments), t),
                    Vector4(weights[0], weights[1], weights[2], weights[3]),
                    {static_cast<std::uint8_t>(boneIndices[0]),
                     static_cast<std::uint8_t>(boneIndices[1]),
                     static_cast<std::uint8_t>(boneIndices[2]),
                     static_cast<std::uint8_t>(boneIndices[3])}));
            }
        }

        for (int ring = 0; ring < rings; ++ring)
        {
            for (int segment = 0; segment < segments; ++segment)
            {
                const int next = (segment + 1) % segments;
                const auto a = static_cast<std::uint16_t>(ring * segments + segment);
                const auto b = static_cast<std::uint16_t>(ring * segments + next);
                const auto c = static_cast<std::uint16_t>((ring + 1) * segments + segment);
                const auto d = static_cast<std::uint16_t>((ring + 1) * segments + next);
                mesh.indices.push_back(a);
                mesh.indices.push_back(c);
                mesh.indices.push_back(b);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
                mesh.indices.push_back(d);
            }
        }

        return mesh;
    }
} // namespace CnaBenchmark
