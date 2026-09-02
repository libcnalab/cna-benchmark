// SPDX-License-Identifier: MS-PL

#include "RunInfo.hpp"

#include <sstream>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Version.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "SharpRuntime/Version.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/Environment.hpp"
#include "System/OperatingSystem.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Splits the comma-separated renderer list CMake baked into the binary. */
        std::vector<std::string> SplitCompiledRenderers()
        {
            std::vector<std::string> names;
#ifdef CNA_BENCHMARK_COMPILED_RENDERERS
            std::istringstream stream(CNA_BENCHMARK_COMPILED_RENDERERS);
            std::string entry;
            while (std::getline(stream, entry, ','))
                if (!entry.empty())
                    names.push_back(entry);
#endif
            return names;
        }

        /** @brief The compiler that produced this binary, as its own macros report it. */
        std::string CompilerIdentification()
        {
#if defined(__clang__)
            return "Clang " __clang_version__;
#elif defined(__GNUC__)
            return "GCC " __VERSION__;
#elif defined(_MSC_VER)
            return "MSVC " + std::to_string(_MSC_VER);
#else
            return "unknown";
#endif
        }

        /** @brief The build configuration, as far as the preprocessor can tell. */
        std::string BuildType()
        {
#if defined(NDEBUG)
            return "Release";
#else
            return "Debug";
#endif
        }

        /** @brief The stable name of one feature-support classification. */
        std::string SupportName(CNA::RendererFeatureSupport support)
        {
            switch (support)
            {
                case CNA::RendererFeatureSupport::Supported:
                    return "Supported";
                case CNA::RendererFeatureSupport::Unsupported:
                    return "Unsupported";
                case CNA::RendererFeatureSupport::Restricted:
                    return "Restricted";
                case CNA::RendererFeatureSupport::Unknown:
                    break;
            }
            return "Unknown";
        }
    }

    RunInfo CollectStaticRunInfo(bool includeHost)
    {
        RunInfo info;
#ifdef CNA_BENCHMARK_VERSION
        info.benchmarkVersion = CNA_BENCHMARK_VERSION;
#else
        info.benchmarkVersion = "0.0.0";
#endif
        info.cnaVersion = std::string(CNA::getVersionString());
        info.sharpRuntimeVersion = std::string(SharpRuntime::getVersionString());
        info.compiledRenderers = SplitCompiledRenderers();
        info.multiRendererBuild = info.compiledRenderers.size() > 1;
        info.compiler = CompilerIdentification();
        info.buildType = BuildType();
        info.processBits = static_cast<int>(sizeof(void*) * 8);

        // The system block comes from sharp-runtime rather than from platform #ifdefs: it is
        // the project's own .NET-shaped portable runtime, and using it here is the same choice
        // any CNA application would make.
        info.operatingSystem = System::Environment::getOSVersionProperty().ToString();
        info.processorCount = static_cast<int>(System::Environment::getProcessorCountProperty());
        info.startedAt = System::DateTimeOffset::getNowProperty().ToString("o");
        if (includeHost)
            info.machineName = System::Environment::getMachineNameProperty();

        return info;
    }

    void CollectDeviceRunInfo(RunInfo& info, const GraphicsDevice& device)
    {
        info.activeRenderer = std::string(device.GetGraphicsRendererName());
        info.defaultRenderer = std::string(CNA::getCurrentGraphicsRendererName());

        const CNA::RendererCapabilityProfile& profile = device.GetRendererCapabilityProfileEXT();
        info.capabilityReport = std::string(profile.GetEnglishReport());
        info.additionalLimitations = std::string(profile.GetAdditionalLimitationsText());

        info.features.clear();
        for (const CNA::RendererFeature feature : CNA::AllRendererFeatures())
        {
            const CNA::RendererFeatureInfo& answer = profile.GetFeature(feature);
            FeatureReport report;
            report.name = std::string(CNA::GetRendererFeatureName(feature));
            report.support = SupportName(answer.support);
            report.note = answer.note;
            info.features.push_back(std::move(report));
        }
    }
} // namespace CnaBenchmark
