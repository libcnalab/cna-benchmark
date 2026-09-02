// SPDX-License-Identifier: MS-PL

#include "ResultWriter.hpp"

#include <memory>

#include "Scoring.hpp"

#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include "System/IO/Directory.hpp"
#include "System/IO/File.hpp"
#include "System/IO/Path.hpp"
#include "System/Text/Json/JsonSerializerOptions.hpp"
#include "System/Text/Json/Nodes/JsonArray.hpp"
#include "System/Text/Json/Nodes/JsonObject.hpp"
#include "System/Text/Json/Nodes/JsonValue.hpp"

using System::Text::Json::JsonSerializerOptions;
using System::Text::Json::Nodes::JsonArray;
using System::Text::Json::Nodes::JsonNode;
using System::Text::Json::Nodes::JsonObject;
using System::Text::Json::Nodes::JsonValue;

namespace CnaBenchmark
{
    namespace
    {
        std::shared_ptr<JsonValue> Text(const std::string& value)
        {
            return JsonValue::Create(value);
        }

        std::shared_ptr<JsonValue> Number(double value)
        {
            return JsonValue::Create(value);
        }

        std::shared_ptr<JsonValue> Integer(long long value)
        {
            return JsonValue::Create(static_cast<SharpRuntime::longcs>(value));
        }

        std::shared_ptr<JsonValue> Boolean(bool value)
        {
            return JsonValue::Create(value);
        }

        /** @brief The stable spelling of a scene outcome in the result document. */
        std::string OutcomeName(SceneOutcome outcome)
        {
            switch (outcome)
            {
                case SceneOutcome::Measured:
                    return "measured";
                case SceneOutcome::Unsupported:
                    return "unsupported";
                case SceneOutcome::NotSelected:
                    return "not-selected";
                case SceneOutcome::Failed:
                    break;
            }
            return "failed";
        }

        std::shared_ptr<JsonObject> StatsObject(const FrameStats& stats)
        {
            auto object = std::make_shared<JsonObject>();
            object->Add("samples", Integer(stats.count));
            object->Add("meanMs", Number(stats.meanMs));
            object->Add("minMs", Number(stats.minMs));
            object->Add("medianMs", Number(stats.medianMs));
            object->Add("p95Ms", Number(stats.p95Ms));
            object->Add("p99Ms", Number(stats.p99Ms));
            object->Add("maxMs", Number(stats.maxMs));
            object->Add("stdDevMs", Number(stats.stdDevMs));
            return object;
        }

        std::shared_ptr<JsonArray> SampleArray(const std::vector<double>& samples)
        {
            auto array = std::make_shared<JsonArray>();
            for (const double sample : samples)
                array->Add(Number(sample));
            return array;
        }
    }

    std::string BuildResultJson(const RunConfig& config, const RunInfo& info,
                                const RunResult& result)
    {
        auto root = std::make_shared<JsonObject>();
        root->Add("schema", Integer(kResultSchemaVersion));
        root->Add("suite", Text("CNA Benchmark Suite"));
        root->Add("benchmarkVersion", Text(info.benchmarkVersion));
        root->Add("referenceProfile", Text(std::string(kReferenceProfileId)));
        root->Add("startedAt", Text(info.startedAt));

        // --- The headline ---------------------------------------------------------------
        auto score = std::make_shared<JsonObject>();
        score->Add("cnaScore", Integer(result.cnaScore));
        score->Add("compositeIndex", Number(result.compositeIndex));
        score->Add("measuredScenes", Integer(result.measuredSceneCount));
        score->Add("selectedScenes", Integer(result.selectedSceneCount));
        score->Add("complete", Boolean(result.complete));
        score->Add("comparable", Boolean(result.comparable));
        {
            auto notes = std::make_shared<JsonArray>();
            for (const std::string& note : result.comparabilityNotes)
                notes->Add(Text(note));
            score->Add("comparabilityNotes", notes);
        }
        score->Add("measuredSeconds", Number(result.measuredSeconds));
        root->Add("score", score);

        // --- Renderer -------------------------------------------------------------------
        auto renderer = std::make_shared<JsonObject>();
        renderer->Add("active", Text(info.activeRenderer));
        renderer->Add("buildDefault", Text(info.defaultRenderer));
        renderer->Add("multiRendererBuild", Boolean(info.multiRendererBuild));
        {
            auto compiled = std::make_shared<JsonArray>();
            for (const std::string& name : info.compiledRenderers)
                compiled->Add(Text(name));
            renderer->Add("compiledIn", compiled);
        }
        {
            auto features = std::make_shared<JsonArray>();
            for (const FeatureReport& feature : info.features)
            {
                auto entry = std::make_shared<JsonObject>();
                entry->Add("feature", Text(feature.name));
                entry->Add("support", Text(feature.support));
                if (!feature.note.empty())
                    entry->Add("note", Text(feature.note));
                features->Add(entry);
            }
            renderer->Add("features", features);
        }
        if (!info.additionalLimitations.empty())
            renderer->Add("additionalLimitations", Text(info.additionalLimitations));
        {
            auto probe = std::make_shared<JsonObject>();
            const char* verdict =
                info.outputProbe.result == OutputProbeResult::Verified          ? "verified"
                : info.outputProbe.result == OutputProbeResult::ProducedNothing ? "no-output"
                                                                                : "not-verifiable";
            probe->Add("verdict", Text(verdict));
            probe->Add("detail", Text(info.outputProbe.detail));
            renderer->Add("outputProbe", probe);
        }
        root->Add("renderer", renderer);

        // --- Environment ----------------------------------------------------------------
        auto environment = std::make_shared<JsonObject>();
        environment->Add("cnaVersion", Text(info.cnaVersion));
        environment->Add("sharpRuntimeVersion", Text(info.sharpRuntimeVersion));
        environment->Add("compiler", Text(info.compiler));
        environment->Add("buildType", Text(info.buildType));
        environment->Add("operatingSystem", Text(info.operatingSystem));
        environment->Add("processorCount", Integer(info.processorCount));
        environment->Add("processBits", Integer(info.processBits));
        if (!info.machineName.empty())
            environment->Add("machineName", Text(info.machineName));
        root->Add("environment", environment);

        // --- The settings the numbers depend on -----------------------------------------
        auto settings = std::make_shared<JsonObject>();
        settings->Add("width", Integer(config.width));
        settings->Add("height", Integer(config.height));
        settings->Add("measuredFrames", Integer(config.measuredFrames));
        settings->Add("warmupFrames", Integer(config.warmupFrames));
        settings->Add("workloadScale", Number(config.workloadScale));
        settings->Add("maxSceneSeconds", Number(config.maxSceneSeconds));
        settings->Add("vsync", Boolean(config.vsync));
        settings->Add("hud", Boolean(config.hud));
        settings->Add("seed", Integer(static_cast<long long>(config.seed)));
        if (!config.label.empty())
            settings->Add("label", Text(config.label));
        root->Add("settings", settings);

        // --- Per-scene ------------------------------------------------------------------
        auto scenes = std::make_shared<JsonArray>();
        for (const SceneResult& scene : result.scenes)
        {
            auto entry = std::make_shared<JsonObject>();
            entry->Add("id", Text(scene.id));
            entry->Add("title", Text(scene.title));
            entry->Add("outcome", Text(OutcomeName(scene.outcome)));
            if (!scene.note.empty())
                entry->Add("note", Text(scene.note));

            if (scene.outcome == SceneOutcome::Measured)
            {
                entry->Add("workUnit", Text(scene.workUnit));
                entry->Add("workUnitsPerFrame", Number(scene.workUnitsPerFrame));
                entry->Add("measuredFrames", Integer(scene.measuredFrames));
                entry->Add("warmupFrames", Integer(scene.warmupFrames));
                entry->Add("truncated", Boolean(scene.truncated));
                entry->Add("fps", Number(scene.fps));
                entry->Add("meanFps", Number(scene.meanFps));
                entry->Add("onePercentLowFps", Number(scene.onePercentLowFps));
                entry->Add("throughput", Number(scene.throughput));
                entry->Add("referenceThroughput", Number(scene.referenceThroughput));
                entry->Add("index", Number(scene.index));
                entry->Add("score", Integer(scene.score));
                entry->Add("frameTimeSpread", Number(scene.frameTimeSpread));
                entry->Add("frameTime", StatsObject(scene.endToEnd));
                entry->Add("submissionTime", StatsObject(scene.submission));

                auto detail = std::make_shared<JsonObject>();
                for (const SceneDetail& item : scene.detail)
                    detail->Add(item.first, Number(item.second));
                entry->Add("detail", detail);

                if (!scene.endToEndSamplesMs.empty() || !scene.submissionSamplesMs.empty())
                {
                    auto samples = std::make_shared<JsonObject>();
                    samples->Add("frameTimeMs", SampleArray(scene.endToEndSamplesMs));
                    samples->Add("submissionTimeMs", SampleArray(scene.submissionSamplesMs));
                    entry->Add("samples", samples);
                }
            }

            scenes->Add(entry);
        }
        root->Add("scenes", scenes);

        JsonSerializerOptions options;
        options.setWriteIndentedProperty(true);
        return root->ToJsonString(options);
    }

    void WriteResultFile(const std::string& path, const std::string& json)
    {
        const std::string directory = System::IO::Path::GetDirectoryName(path);
        if (!directory.empty() && !System::IO::Directory::Exists(directory))
            System::IO::Directory::CreateDirectory(directory);
        System::IO::File::WriteAllText(path, json);
    }
} // namespace CnaBenchmark
