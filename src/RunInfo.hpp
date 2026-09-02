// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file RunInfo.hpp
 * @brief The context a score is only meaningful inside: which renderer, which build, which host.
 */

#include <string>
#include <vector>

#include "OutputProbe.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CnaBenchmark
{
    /** @brief One renderer feature and the answer the active renderer gave for it. */
    struct FeatureReport
    {
        /** @brief Stable feature identifier from `CNA::GetRendererFeatureName`. */
        std::string name;
        /** @brief `Supported`, `Unsupported`, `Restricted` or `Unknown`. */
        std::string support;
        /** @brief The renderer's own qualification, when it gave one. */
        std::string note;
    };

    /** @brief Everything about the machine, the build and the renderer behind one result. */
    struct RunInfo
    {
        /** @brief The benchmark's own version. */
        std::string benchmarkVersion;
        /** @brief CNA's release string. */
        std::string cnaVersion;
        /** @brief Sharp Runtime's release string. */
        std::string sharpRuntimeVersion;
        /** @brief Renderer identities compiled into this binary. */
        std::vector<std::string> compiledRenderers;
        /** @brief The renderer that actually ran. */
        std::string activeRenderer;
        /** @brief The renderer the build would have used with no `--renderer`. */
        std::string defaultRenderer;
        /** @brief True when the binary carries more than one renderer. */
        bool multiRendererBuild = false;
        /** @brief Compiler identification, e.g. `"GCC 14.2.0"`. */
        std::string compiler;
        /** @brief `"Debug"`, `"Release"` or whatever the build type was, when known. */
        std::string buildType;
        /** @brief Operating-system description from `System::Environment`. */
        std::string operatingSystem;
        /** @brief Logical processors reported by `System::Environment`. */
        int processorCount = 0;
        /** @brief Pointer width of the process in bits. */
        int processBits = 0;
        /** @brief Machine name; empty unless `--include-host` was given. */
        std::string machineName;
        /** @brief ISO-8601 timestamp of when the run started, with its UTC offset. */
        std::string startedAt;
        /** @brief The renderer's own English capability report. */
        std::string capabilityReport;
        /** @brief The renderer's per-feature answers, in CNA's stable feature order. */
        std::vector<FeatureReport> features;
        /** @brief Renderer-supplied limitations that are not machine-readable fields. */
        std::string additionalLimitations;
        /** @brief Whether this renderer was measured actually producing pixels. */
        OutputProbe outputProbe;
    };

    /**
     * @brief Collects everything knowable before a device exists.
     * @param includeHost Whether to record the machine name.
     */
    [[nodiscard]] RunInfo CollectStaticRunInfo(bool includeHost);

    /**
     * @brief Fills in the renderer half of @p info from a live device.
     * @param info   The record to complete.
     * @param device The device whose renderer and capability profile are read.
     */
    void CollectDeviceRunInfo(RunInfo& info,
                              const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
} // namespace CnaBenchmark
