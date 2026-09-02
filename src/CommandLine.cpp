// SPDX-License-Identifier: MS-PL

#include "CommandLine.hpp"

#include <cstdlib>
#include <sstream>

#include "SceneRegistry.hpp"

namespace CnaBenchmark
{
    namespace
    {
        /** @brief Splits @p value on commas, dropping empty fields. */
        std::vector<std::string> SplitList(const std::string& value)
        {
            std::vector<std::string> parts;
            std::istringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
                if (!part.empty())
                    parts.push_back(part);
            return parts;
        }

        /** @brief Parses a decimal integer, rejecting trailing characters. */
        bool ParseInt(const std::string& text, int& out)
        {
            if (text.empty())
                return false;
            char* end = nullptr;
            const long value = std::strtol(text.c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
                return false;
            out = static_cast<int>(value);
            return true;
        }

        /** @brief Parses a decimal floating-point value, rejecting trailing characters. */
        bool ParseDouble(const std::string& text, double& out)
        {
            if (text.empty())
                return false;
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == nullptr || *end != '\0')
                return false;
            out = value;
            return true;
        }

        /** @brief Parses an unsigned 64-bit seed, accepting decimal and 0x-prefixed hex. */
        bool ParseSeed(const std::string& text, std::uint64_t& out)
        {
            if (text.empty())
                return false;
            char* end = nullptr;
            const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
            if (end == nullptr || *end != '\0')
                return false;
            out = static_cast<std::uint64_t>(value);
            return true;
        }
    }

    std::string HelpText()
    {
        std::ostringstream text;
        text <<
R"(cna-benchmark -- the CNA Benchmark Suite

Runs seven fixed graphics workloads against one CNA renderer, scores each against
the suite's reference profile, and reduces them to a single CNA Score. The result
is written as JSON so several renderers can be compared afterwards.

Usage:
  cna-benchmark [options]

Selecting what runs:
  --renderer=NAME        Renderer to use, from the set compiled into this binary.
                         Defaults to the build's own default renderer.
  --scenes=A,B,C         Run only these scenes. Default: all of them.
                         A partial run is reported but is never comparable.

Measurement:
  --frames=N             Measured frames per scene (default 300).
  --warmup=N             Untimed frames before each scene's measured window (default 60).
  --width=N              Back-buffer width in pixels (default 1280).
  --height=N             Back-buffer height in pixels (default 720).
  --workload-scale=F     Multiply every scene's workload (default 1.0). Anything
                         other than 1.0 makes the run non-comparable.
  --max-scene-seconds=N  Cap the total time one scene may take, warm-up included
                         (default 0, no limit). For renderers where the full
                         window is impractical -- the CPU rasterizer draws some
                         scenes at about one frame a second. A truncated scene
                         says so and the run is reported as non-comparable.
  --seed=N               Seed for the suite's generator (decimal or 0x hex).
  --vsync                Wait for vertical retrace. Caps the frame rate, so a run
                         with it on is reported as non-comparable.
  --quick                Shorthand for --frames=90 --warmup=20. Non-comparable.

Output:
  --json=PATH            Where to write the result document
                         (default cna-benchmark-result.json). Use --json= to write none.
  --json-stdout          Print the result document to standard output. The progress
                         report moves to standard error, so the output can be piped
                         straight into a JSON reader.
  --samples              Include every per-frame sample in the result document.
  --label=TEXT           Free-text note carried into the result, e.g. a hardware
                         description.
  --include-host         Record the machine name in the result. Off by default.
  --hud                  Draw the on-screen overlay. It is drawn inside the frames
                         being measured, so a run with it on is non-comparable.
  --strict               Exit non-zero unless every selected scene was measured.

Information:
  --list-scenes          Print the scenes and what each one measures.
  --list-renderers       Print the renderers compiled into this binary.
  --version              Print version information.
  --help                 Print this text.

Exit status:
  0  the run finished (and, with --strict, every selected scene was measured)
  1  the arguments were wrong, the run failed, or --strict was not satisfied
)";
        return text.str();
    }

    ParsedCommandLine ParseCommandLine(int argc, char** argv)
    {
        ParsedCommandLine parsed;

        auto fail = [&parsed](std::string message) {
            parsed.action = CommandAction::Invalid;
            parsed.error = std::move(message);
            return parsed;
        };

        for (int i = 1; i < argc; ++i)
        {
            std::string argument = argv[i];
            std::string name = argument;
            std::string value;
            bool hasValue = false;

            const std::size_t equals = argument.find('=');
            if (equals != std::string::npos)
            {
                name = argument.substr(0, equals);
                value = argument.substr(equals + 1);
                hasValue = true;
            }

            // Accept the separated spelling too, for every option that takes a value.
            auto takeValue = [&](const char* optionName) -> bool {
                if (hasValue)
                    return true;
                if (i + 1 < argc)
                {
                    value = argv[++i];
                    hasValue = true;
                    return true;
                }
                parsed.action = CommandAction::Invalid;
                parsed.error = std::string(optionName) + " needs a value";
                return false;
            };

            if (name == "--help" || name == "-h")
            {
                parsed.action = CommandAction::ShowHelp;
                return parsed;
            }
            if (name == "--version")
            {
                parsed.action = CommandAction::ShowVersion;
                return parsed;
            }
            if (name == "--list-scenes")
            {
                parsed.action = CommandAction::ListScenes;
                return parsed;
            }
            if (name == "--list-renderers")
            {
                parsed.action = CommandAction::ListRenderers;
                return parsed;
            }

            if (name == "--renderer")
            {
                if (!takeValue("--renderer"))
                    return parsed;
                parsed.config.rendererRequest = value;
            }
            else if (name == "--scenes")
            {
                if (!takeValue("--scenes"))
                    return parsed;
                parsed.config.sceneIds = SplitList(value);
                if (parsed.config.sceneIds.empty())
                    return fail("--scenes was given no scene names");
                for (const std::string& id : parsed.config.sceneIds)
                    if (!IsKnownSceneId(id))
                        return fail("unknown scene '" + id +
                                    "'; run --list-scenes to see the registered ones");
            }
            else if (name == "--frames")
            {
                if (!takeValue("--frames"))
                    return parsed;
                if (!ParseInt(value, parsed.config.measuredFrames) ||
                    parsed.config.measuredFrames < 1)
                    return fail("--frames needs a positive whole number");
            }
            else if (name == "--warmup")
            {
                if (!takeValue("--warmup"))
                    return parsed;
                if (!ParseInt(value, parsed.config.warmupFrames) ||
                    parsed.config.warmupFrames < 0)
                    return fail("--warmup needs a whole number of zero or more");
            }
            else if (name == "--width")
            {
                if (!takeValue("--width"))
                    return parsed;
                if (!ParseInt(value, parsed.config.width) || parsed.config.width < 64)
                    return fail("--width needs a whole number of at least 64");
            }
            else if (name == "--height")
            {
                if (!takeValue("--height"))
                    return parsed;
                if (!ParseInt(value, parsed.config.height) || parsed.config.height < 64)
                    return fail("--height needs a whole number of at least 64");
            }
            else if (name == "--max-scene-seconds")
            {
                if (!takeValue("--max-scene-seconds"))
                    return parsed;
                if (!ParseDouble(value, parsed.config.maxSceneSeconds) ||
                    parsed.config.maxSceneSeconds < 0.0)
                    return fail("--max-scene-seconds needs a number of zero or more");
            }
            else if (name == "--workload-scale")
            {
                if (!takeValue("--workload-scale"))
                    return parsed;
                if (!ParseDouble(value, parsed.config.workloadScale) ||
                    parsed.config.workloadScale <= 0.0)
                    return fail("--workload-scale needs a positive number");
            }
            else if (name == "--seed")
            {
                if (!takeValue("--seed"))
                    return parsed;
                if (!ParseSeed(value, parsed.config.seed))
                    return fail("--seed needs a whole number, decimal or 0x-prefixed");
            }
            else if (name == "--json")
            {
                if (!takeValue("--json"))
                    return parsed;
                parsed.config.jsonPath = value;
            }
            else if (name == "--label")
            {
                if (!takeValue("--label"))
                    return parsed;
                parsed.config.label = value;
            }
            else if (name == "--vsync")
            {
                parsed.config.vsync = true;
            }
            else if (name == "--hud")
            {
                parsed.config.hud = true;
            }
            else if (name == "--no-hud")
            {
                parsed.config.hud = false;
            }
            else if (name == "--json-stdout")
            {
                parsed.config.jsonToStdout = true;
            }
            else if (name == "--samples")
            {
                parsed.config.emitSamples = true;
            }
            else if (name == "--strict")
            {
                parsed.config.strict = true;
            }
            else if (name == "--include-host")
            {
                parsed.config.includeHost = true;
            }
            else if (name == "--quick")
            {
                parsed.config.measuredFrames = 90;
                parsed.config.warmupFrames = 20;
            }
            else
            {
                return fail("unknown option '" + name + "'; run --help for the list");
            }
        }

        return parsed;
    }
} // namespace CnaBenchmark
