// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file ResultWriter.hpp
 * @brief Turns a finished run into the suite's JSON result document.
 */

#include <string>

#include "Benchmark.hpp"
#include "RunInfo.hpp"

namespace CnaBenchmark
{
    /** @brief Version of the result schema this build writes. */
    inline constexpr int kResultSchemaVersion = 1;

    /**
     * @brief Renders a run as the suite's JSON document.
     * @param config Settings the run used; recorded so a result can be reproduced.
     * @param info   Machine, build and renderer context.
     * @param result The scored outcome.
     * @return An indented UTF-8 JSON document.
     */
    [[nodiscard]] std::string BuildResultJson(const RunConfig& config, const RunInfo& info,
                                              const RunResult& result);

    /**
     * @brief Writes @p json to @p path, creating the parent directory when it is missing.
     * @param path Destination file path.
     * @param json Document to write.
     * @throws System::Exception on an I/O failure.
     */
    void WriteResultFile(const std::string& path, const std::string& json);
} // namespace CnaBenchmark
