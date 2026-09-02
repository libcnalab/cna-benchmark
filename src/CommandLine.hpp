// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CommandLine.hpp
 * @brief Argument parsing, and the two commands that answer without running anything.
 */

#include <string>
#include <vector>

#include "Benchmark.hpp"

namespace CnaBenchmark
{
    /** @brief What the command line asked the program to do. */
    enum class CommandAction
    {
        /** @brief Run the benchmark with `config`. */
        Run,
        /** @brief Print help and exit successfully. */
        ShowHelp,
        /** @brief Print the version and exit successfully. */
        ShowVersion,
        /** @brief List the registered scenes and exit successfully. */
        ListScenes,
        /** @brief List the renderers compiled into this binary and exit successfully. */
        ListRenderers,
        /** @brief The arguments were wrong; `error` says how. */
        Invalid
    };

    /** @brief The parsed command line. */
    struct ParsedCommandLine
    {
        /** @brief What to do. */
        CommandAction action = CommandAction::Run;
        /** @brief The run's settings, when `action` is `Run`. */
        RunConfig config;
        /** @brief Why parsing failed, when `action` is `Invalid`. */
        std::string error;
    };

    /**
     * @brief Parses `argv`.
     *
     * Long options only, in both `--name value` and `--name=value` spellings. Unrecognised
     * options are rejected rather than ignored: a typo in a benchmark's settings silently
     * producing a different measurement is worse than a failed run.
     *
     * @param argc Argument count as given to `main`.
     * @param argv Argument vector as given to `main`.
     */
    [[nodiscard]] ParsedCommandLine ParseCommandLine(int argc, char** argv);

    /** @brief Returns the complete `--help` text. */
    [[nodiscard]] std::string HelpText();
} // namespace CnaBenchmark
