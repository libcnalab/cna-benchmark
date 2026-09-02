// SPDX-License-Identifier: MS-PL

#include "Report.hpp"

namespace CnaBenchmark
{
    namespace
    {
        std::FILE* g_reportStream = nullptr;
    }

    std::FILE* ReportStream()
    {
        // Resolved on first use rather than at static-initialization time: `stdout` is not
        // guaranteed to be a constant expression, and a static initializer that reads it runs
        // in an order this translation unit does not control.
        return g_reportStream != nullptr ? g_reportStream : stdout;
    }

    void SetReportStream(std::FILE* stream)
    {
        if (stream != nullptr)
            g_reportStream = stream;
    }
} // namespace CnaBenchmark
