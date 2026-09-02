// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file OutputProbe.hpp
 * @brief Asks the one question a capability query cannot answer: does this renderer draw?
 *
 * CNA's headless renderer reports the complete 3D pipeline as supported, and it is not lying:
 * it accepts every draw call, validates every argument and maintains every counter the
 * contract promises. What it does not do is produce pixels. Benchmarked without a check, it
 * runs every scene, finishes every frame in microseconds and tops the comparison table -- a
 * result that is arithmetically correct and completely misleading.
 *
 * The probe settles it by measurement rather than by renderer name: render one known colour
 * into a small off-screen target, read the target back, and see whether the colour arrived.
 */

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CnaBenchmark
{
    /** @brief What the probe found. */
    enum class OutputProbeResult
    {
        /** @brief The drawn colour was read back: this renderer really rasterizes. */
        Verified,
        /**
         * @brief The probe could not run -- no render target, or no readback path.
         *
         * Not a defect and not a reason to distrust a score: several perfectly real renderers
         * cannot read a surface back. It only means this particular evidence is unavailable.
         */
        NotVerifiable,
        /** @brief The readback worked and the drawn colour was not there. */
        ProducedNothing
    };

    /** @brief The probe's verdict, with the English explanation that travels with it. */
    struct OutputProbe
    {
        /** @brief What the probe found. */
        OutputProbeResult result = OutputProbeResult::NotVerifiable;
        /** @brief Stable English detail, including the colour read back when there was one. */
        std::string detail;
    };

    /**
     * @brief Runs the probe on @p device.
     *
     * Never throws: an exception anywhere inside is itself an answer (`NotVerifiable`), and a
     * benchmark must not fail to start because an optional piece of evidence was unavailable.
     *
     * @param device The device to probe. Its render-target binding is restored before returning.
     */
    [[nodiscard]] OutputProbe ProbeRendererOutput(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
} // namespace CnaBenchmark
