// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file Report.hpp
 * @brief Where the human-readable progress report goes.
 *
 * Standard output by default. With `--json-stdout` the report moves to standard error and
 * stdout carries the result document alone, so `cna-benchmark --json-stdout | jq ...` works --
 * which is the only reason that flag exists.
 */

#include <cstdio>

namespace CnaBenchmark
{
    /** @brief The stream the progress report and the summary are written to. */
    [[nodiscard]] std::FILE* ReportStream();

    /**
     * @brief Redirects the report.
     * @param stream Destination; must outlive the run. Ignored when null.
     */
    void SetReportStream(std::FILE* stream);
} // namespace CnaBenchmark
