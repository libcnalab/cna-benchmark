// SPDX-License-Identifier: MS-PL
//
// cna-benchmark -- entry point.
//
// The renderer is chosen here, before anything creates a GraphicsDevice: CNA latches the
// selection the moment the device exists, and a request afterwards is an error rather than a
// silent no-op. That ordering is the whole reason one binary can benchmark several renderers.

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

#include "BenchmarkRunner.hpp"
#include "CommandLine.hpp"
#include "Format.hpp"
#include "Report.hpp"
#include "ResultWriter.hpp"
#include "SceneRegistry.hpp"
#include "Scoring.hpp"

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Version.hpp"
#include "SharpRuntime/Version.hpp"
#include "System/Environment.hpp"

namespace
{
    using namespace CnaBenchmark;

    void PrintVersion()
    {
#ifdef CNA_BENCHMARK_VERSION
        std::printf("cna-benchmark %s\n", CNA_BENCHMARK_VERSION);
#else
        std::printf("cna-benchmark (unversioned build)\n");
#endif
        std::printf("  reference profile : %s\n", std::string(kReferenceProfileId).c_str());
        std::printf("  result schema     : %d\n", kResultSchemaVersion);
        std::printf("  CNA               : %s\n", std::string(CNA::getVersionString()).c_str());
        std::printf("  Sharp Runtime     : %s\n",
                    std::string(SharpRuntime::getVersionString()).c_str());
        std::printf("  default renderer  : %s\n",
                    std::string(CNA::getCurrentGraphicsRendererName()).c_str());
    }

    void PrintScenes()
    {
        std::printf("Scenes, in the order the suite runs them:\n\n");
        for (const std::unique_ptr<Scene>& scene : CreateAllScenes())
        {
            std::printf("  %-12s %s\n", std::string(scene->Id()).c_str(),
                        std::string(scene->Title()).c_str());
            std::printf("  %-12s   measures : %s\n", "",
                        std::string(scene->Description()).c_str());
            std::printf("  %-12s   unit     : %s\n", "",
                        std::string(scene->WorkUnitName()).c_str());
            const std::vector<SceneRequirement> requirements = scene->Requirements();
            if (requirements.empty())
            {
                std::printf("  %-12s   needs    : nothing beyond 2D; runs on every renderer\n", "");
            }
            else
            {
                for (const SceneRequirement& requirement : requirements)
                    std::printf("  %-12s   needs    : %s\n", "",
                                std::string(requirement.reason).c_str());
            }
            std::printf("\n");
        }
    }

    void PrintRenderers()
    {
        const auto available = CNA::GraphicsRendererSelection::GetAvailable();
        const std::string buildDefault(CNA::getCurrentGraphicsRendererName());

        std::printf("Renderers compiled into this binary:\n\n");
        for (const CNA::GraphicsRendererType type : available)
        {
            const std::string name(CNA::getGraphicsRendererName(type));
            std::printf("  %-14s%s\n", name.c_str(),
                        name == buildDefault ? "  (build default)" : "");
        }
        if (available.size() <= 1)
        {
            std::printf(
                "\nThis is a single-renderer build. To compare renderers from one binary, "
                "configure with\n  -DCNA_BENCHMARK_RENDERERS=\"OPENGLES3;SDL_GPU;SOFTWARE\"\n"
                "and select one per run with --renderer.\n");
        }
        else
        {
            std::printf("\nSelect one per run with --renderer=NAME.\n");
        }
    }

    /** @brief Prints the per-scene table and the headline score. */
    void PrintSummary(const RunResult& result, const RunInfo& info)
    {
        std::fprintf(ReportStream(), "\n");
        static constexpr const char* kRule =
            "-----------------------------------------------------------------------------------";
        static constexpr const char* kHeading =
            " Scene          Outcome            Throughput                     Index   Scene score";

        std::fprintf(ReportStream(), "%s\n%s\n%s\n", kRule, kHeading, kRule);

        for (const SceneResult& scene : result.scenes)
        {
            if (scene.outcome == SceneOutcome::Measured)
            {
                std::fprintf(ReportStream(), " %-14s measured    %9s %-20s %8.3f %13s\n",
                             scene.id.c_str(), FormatCount(scene.throughput).c_str(),
                             (scene.workUnit + "/s").c_str(), scene.index,
                             FormatGrouped(scene.score).c_str());
            }
            else
            {
                const char* outcome = scene.outcome == SceneOutcome::Unsupported ? "unsupported"
                                      : scene.outcome == SceneOutcome::Failed    ? "failed"
                                                                                 : "not run";
                std::fprintf(ReportStream(), " %-14s %-11s %s\n", scene.id.c_str(), outcome,
                             scene.note.c_str());
            }
        }

        std::fprintf(ReportStream(), "%s\n\n", kRule);

        std::fprintf(ReportStream(), "  CNA Score: %s\n",
                     FormatGrouped(result.cnaScore).c_str());
        std::fprintf(ReportStream(), "  Renderer:  %s\n", info.activeRenderer.c_str());
        std::fprintf(ReportStream(), "  Scenes:    %d of %d measured%s\n",
                     result.measuredSceneCount, result.selectedSceneCount,
                     result.complete ? " (complete run)" : "");

        if (!result.comparable)
        {
            std::fprintf(ReportStream(),
                         "\n  This score is NOT comparable with published results:\n");
            for (const std::string& note : result.comparabilityNotes)
                std::fprintf(ReportStream(), "    - %s\n", note.c_str());
        }
        std::fprintf(ReportStream(), "\n");
    }
}

int main(int argc, char** argv)
{
    using namespace CnaBenchmark;

    // Gives System::Environment the real command line, so a result document can record how the
    // run was invoked rather than only what the parser made of it.
    System::Environment::InitializeCommandLine(argc, argv);

    const ParsedCommandLine parsed = ParseCommandLine(argc, argv);
    switch (parsed.action)
    {
        case CommandAction::ShowHelp:
            std::printf("%s", HelpText().c_str());
            return 0;
        case CommandAction::ShowVersion:
            PrintVersion();
            return 0;
        case CommandAction::ListScenes:
            PrintScenes();
            return 0;
        case CommandAction::ListRenderers:
            PrintRenderers();
            return 0;
        case CommandAction::Invalid:
            std::fprintf(stderr, "cna-benchmark: %s\n", parsed.error.c_str());
            return 1;
        case CommandAction::Run:
            break;
    }

    const RunConfig& config = parsed.config;

    // With --json-stdout the document must be the only thing on stdout, or the flag cannot be
    // piped into anything that reads JSON -- which is what it is for.
    if (config.jsonToStdout)
        SetReportStream(stderr);

    if (!config.rendererRequest.empty())
    {
        try
        {
            CNA::GraphicsRendererSelection::SetPreferred(config.rendererRequest);
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "cna-benchmark: %s\n", error.what());
            std::fprintf(stderr,
                         "cna-benchmark: run --list-renderers to see what this binary contains.\n");
            return 1;
        }
    }

    std::fprintf(ReportStream(), "CNA Benchmark Suite %s -- reference profile %s\n\n",
#ifdef CNA_BENCHMARK_VERSION
                CNA_BENCHMARK_VERSION,
#else
                "(unversioned)",
#endif
                std::string(kReferenceProfileId).c_str());

    try
    {
        BenchmarkRunner runner(config);
        runner.Run();

        const RunResult& result = runner.Result();
        const RunInfo& info = runner.Info();

        PrintSummary(result, info);

        const std::string json = BuildResultJson(config, info, result);
        if (config.jsonToStdout)
            std::printf("%s\n", json.c_str());
        if (!config.jsonPath.empty())
        {
            WriteResultFile(config.jsonPath, json);
            std::fprintf(ReportStream(), "  Results written to %s\n\n",
                         config.jsonPath.c_str());
        }

        if (config.strict && result.measuredSceneCount != result.selectedSceneCount)
        {
            std::fprintf(stderr,
                         "cna-benchmark: --strict was given and %d of %d selected scenes were "
                         "not measured.\n",
                         result.selectedSceneCount - result.measuredSceneCount,
                         result.selectedSceneCount);
            return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "\ncna-benchmark: the run failed: %s\n", error.what());
        return 1;
    }
}
