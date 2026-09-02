// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SceneCommon.hpp
 * @brief Helpers the 3D scenes share: the camera, and the render-state baseline.
 */

#include <cmath>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

namespace CnaBenchmark
{
    /** @brief Pi, to the precision a float carries. */
    inline constexpr float kPi = 3.14159265358979323846f;

    /**
     * @brief Restores the XNA 3D defaults: opaque blending, depth on, back faces culled.
     *
     * SpriteBatch installs its own render states and leaves them active after `End()`, exactly
     * as XNA does. A 3D scene that follows a 2D one -- which the suite's scene order guarantees
     * -- would otherwise inherit alpha blending and a disabled depth buffer and measure a
     * different workload than the same scene run first.
     *
     * @param device Device whose state is reset.
     */
    inline void ResetTo3DDefaults(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        device.setBlendStateProperty(Microsoft::Xna::Framework::Graphics::BlendState::Opaque);
        device.setDepthStencilStateProperty(
            Microsoft::Xna::Framework::Graphics::DepthStencilState::Default);
        device.setRasterizerStateProperty(
            Microsoft::Xna::Framework::Graphics::RasterizerState::CullCounterClockwise);
    }

    /**
     * @brief Returns the viewport's aspect ratio, or 1 for a degenerate viewport.
     * @param device Device whose active viewport is measured.
     */
    [[nodiscard]] inline float ViewportAspect(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        const int height = viewport.getHeightProperty();
        if (height <= 0)
            return 1.0f;
        return static_cast<float>(viewport.getWidthProperty()) / static_cast<float>(height);
    }

    /**
     * @brief An orbit camera looking at the origin, driven by the scene clock.
     *
     * Every 3D scene shares it so that "the renderer drew the same thing" is a property of the
     * suite rather than of each scene's own camera code.
     *
     * @param sceneTime Deterministic scene clock in seconds.
     * @param radius    Distance from the origin.
     * @param height    Camera height above the origin.
     * @param period    Seconds for one complete orbit.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Matrix OrbitView(double sceneTime,
                                                                     float radius, float height,
                                                                     float period)
    {
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Vector3;

        const float angle = 2.0f * kPi * static_cast<float>(sceneTime) / period;
        return Matrix::CreateLookAt(
            Vector3(std::cos(angle) * radius, height, std::sin(angle) * radius),
            Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    }

    /**
     * @brief The suite's standard perspective projection.
     * @param device   Device supplying the aspect ratio.
     * @param farPlane Far clip distance in world units.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Matrix StandardProjection(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, float farPlane)
    {
        return Microsoft::Xna::Framework::Matrix::CreatePerspectiveFieldOfView(
            kPi * 0.25f, ViewportAspect(device), 0.5f, farPlane);
    }

    /**
     * @brief Scales a nominal workload, never below one unit.
     * @param nominal The scored workload at `--workload-scale 1`.
     * @param scale   The run's workload scale.
     */
    [[nodiscard]] inline int ScaledCount(int nominal, double scale)
    {
        const double scaled = static_cast<double>(nominal) * scale;
        if (scaled < 1.0)
            return 1;
        return static_cast<int>(scaled + 0.5);
    }
} // namespace CnaBenchmark
