// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Procedural.hpp
 * @brief The suite's content: textures and meshes generated from a seed, never loaded.
 *
 * A benchmark that ships assets measures the asset pipeline as much as the renderer, and it
 * cannot be run from a build tree alone. Everything here is derived from `RunConfig::seed`, so
 * two runs on two renderers draw byte-identical geometry and sample byte-identical texels.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CnaBenchmark
{
    /**
     * @brief The suite's own generator: xorshift64*, seeded per run.
     *
     * Deliberately not `std::mt19937` or `rand()`. The first differs in size and cost between
     * standard libraries, and the second differs in *output* between them -- either would make
     * two platforms draw two different scenes and call the results comparable.
     */
    class Rng
    {
    public:
        /** @brief Seeds the generator. @param seed Any non-zero value; zero is replaced. */
        explicit Rng(std::uint64_t seed);
        /** @brief Returns the next 64-bit value. */
        std::uint64_t NextUInt64();
        /** @brief Returns a value in [0, 1). */
        float NextFloat();
        /** @brief Returns a value in [@p min, @p max). */
        float NextFloat(float min, float max);
        /** @brief Returns a value in [0, @p exclusiveMax). @param exclusiveMax Must be positive. */
        int NextInt(int exclusiveMax);

    private:
        std::uint64_t state_;
    };

    /** @brief A CPU-side indexed triangle mesh with 16-bit indices. */
    struct MeshData
    {
        /** @brief Vertices, at most 65536 so the indices stay 16-bit. */
        std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture> vertices;
        /** @brief Triangle-list indices into `vertices`. */
        std::vector<std::uint16_t> indices;
        /** @brief Triangles in this mesh. */
        [[nodiscard]] int PrimitiveCount() const { return static_cast<int>(indices.size() / 3); }
    };

    /** @brief A CPU-side indexed mesh whose vertices carry bone weights and indices. */
    struct SkinnedMeshData
    {
        /** @brief Vertices with four bone influences each. */
        std::vector<
            Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned>
            vertices;
        /** @brief Triangle-list indices into `vertices`. */
        std::vector<std::uint16_t> indices;
        /** @brief Triangles in this mesh. */
        [[nodiscard]] int PrimitiveCount() const { return static_cast<int>(indices.size() / 3); }
    };

    /**
     * @brief Builds an RGBA atlas of @p tiles by @p tiles distinctly coloured checker tiles.
     *
     * Distinct per tile on purpose: a scene that draws from several sub-rectangles of one
     * texture is the case that separates a renderer batching correctly from one flushing per
     * sprite, and identical tiles would hide the difference.
     *
     * @param device Device that owns the texture.
     * @param size   Atlas edge length in pixels; must be a positive multiple of @p tiles.
     * @param tiles  Tiles per axis.
     * @param rng    Generator for the per-tile colours.
     */
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
    CreateAtlasTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                       int size, int tiles, Rng& rng);

    /**
     * @brief Builds a soft radial RGBA sprite, opaque in the middle and transparent at the rim.
     * @param device Device that owns the texture.
     * @param size   Edge length in pixels.
     */
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
    CreateParticleTexture(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int size);

    /**
     * @brief Builds a unit cube centred on the origin, with per-face normals and UVs.
     * @param edge Edge length in world units.
     */
    [[nodiscard]] MeshData BuildCube(float edge);

    /**
     * @brief Builds one square terrain chunk as a lit, textured height-field grid.
     * @param cells      Quads per axis; the chunk has (cells+1)^2 vertices, so at most 255.
     * @param worldSize  Edge length of the chunk in world units.
     * @param originX    World X of the chunk's lower corner, so chunks share a height field.
     * @param originZ    World Z of the chunk's lower corner.
     * @param amplitude  Peak-to-trough height of the field.
     */
    [[nodiscard]] MeshData BuildTerrainChunk(int cells, float worldSize,
                                             float originX, float originZ, float amplitude);

    /**
     * @brief Builds a bone-weighted tube: a closed cylinder skinned to a chain of bones.
     *
     * Every vertex carries four influences with weights that sum to one, so the scene measures
     * the full four-weight skinning path rather than a rigid mesh wearing a skinned vertex
     * layout.
     *
     * @param rings    Rings along the tube's axis; each ring belongs between two bones.
     * @param segments Vertices around the tube's circumference.
     * @param bones    Bones in the chain; must be at least two.
     * @param length   Tube length in world units.
     * @param radius   Tube radius in world units.
     */
    [[nodiscard]] SkinnedMeshData BuildSkinnedTube(int rings, int segments, int bones,
                                                   float length, float radius);
} // namespace CnaBenchmark
