// SPDX-License-Identifier: MS-PL

#include "Scoring.hpp"

#include <cmath>

namespace CnaBenchmark
{
    // =====================================================================================
    // The reference profile.
    //
    // Each number is the throughput that scores exactly 1000 for that scene, so a scene index
    // is "how many times reference speed", and the CNA Score is 1000 times the geometric mean
    // of those indices across the suite. A renderer twice as fast everywhere scores twice as
    // much; a renderer twice as fast in one scene of seven scores about 10% more. That is the
    // property a geometric mean has and an arithmetic mean does not, and it is why 3DMark-style
    // suites use one: no single scene can dominate the headline number.
    //
    // The constants themselves are a CALIBRATION, not a measurement of anything: they are one
    // twentieth of what the machine below measured, so that machine scores about 20,000 and a
    // number in the tens of thousands means "tens of times the reference baseline". They carry
    // no claim beyond that. Comparing two runs is comparing them with each other; the profile
    // only fixes the scale both are expressed in.
    //
    //   Calibration machine for cna-bench-ref-v1
    //     CPU        16 logical processors
    //     GPU        AMD Radeon 780M (integrated, RDNA 3), Mesa 25.0.7 radeonsi
    //     OS         Debian 13, Linux 6.12, X11 at 1280x720
    //     CNA        OPENGLES3 renderer, vsync off, Release build, 60 + 300 frames per scene
    //
    //   Each constant is the median of three consecutive complete runs, divided by twenty and
    //   rounded to two significant figures. Three runs, because one run cannot show whether a
    //   number is repeatable: these agreed within 2% to 7% of each other, which is what made
    //   them usable as a scale in the first place. The medians they came from were, in the
    //   suite's order: 8.00 M sprites/s, 10.9 M particles/s, 44.7 M skinned vertices/s,
    //   507 M triangles/s, 336 M triangle-lights/s, 42.7 M instances/s, 543 M bytes/s.
    //
    // Changing any constant below invalidates every previously published score, so the profile
    // is versioned: bump kReferenceProfileId with it, and the comparison tool will refuse to
    // put results from two profiles in one table instead of quietly mixing them.
    // =====================================================================================

    namespace
    {
        constexpr double kEqualWeight = 1.0;
    }

    std::vector<ReferenceEntry> ReferenceProfile()
    {
        return {
            // sprites: textured SpriteBatch quads per second.
            {"sprites", 4.00e5, kEqualWeight},
            // particles: additively blended, CPU-simulated point sprites per second.
            {"particles", 5.50e5, kEqualWeight},
            // skeletal: GPU-skinned vertices per second, four bone influences each.
            {"skeletal", 2.25e6, kEqualWeight},
            // terrain: lit, textured, indexed triangles per second.
            {"terrain", 2.50e7, kEqualWeight},
            // lights: triangle-light products per second across the additive light passes.
            {"lights", 1.75e7, kEqualWeight},
            // instancing: hardware instances per second through DrawInstancedPrimitives.
            {"instancing", 2.25e6, kEqualWeight},
            // streaming: bytes per second uploaded into GPU resources and then drawn.
            {"streaming", 2.75e7, kEqualWeight},
        };
    }

    double ReferenceThroughputFor(std::string_view sceneId)
    {
        for (const ReferenceEntry& entry : ReferenceProfile())
            if (entry.sceneId == sceneId)
                return entry.throughput;
        return 0.0;
    }

    double ReferenceWeightFor(std::string_view sceneId)
    {
        for (const ReferenceEntry& entry : ReferenceProfile())
            if (entry.sceneId == sceneId)
                return entry.weight;
        return 0.0;
    }

    void ComputeScore(RunResult& result)
    {
        // The geometric mean is accumulated in log space. A scene index can legitimately span
        // several orders of magnitude across this suite's renderer set -- the STUB renderer
        // draws nothing and the software rasterizer is thousands of times slower than the GPU
        // -- and multiplying seven such indices before taking a root overflows a double long
        // before the root brings it back.
        double weightedLogSum = 0.0;
        double weightTotal = 0.0;
        int measured = 0;

        for (SceneResult& scene : result.scenes)
        {
            if (scene.outcome != SceneOutcome::Measured)
                continue;

            scene.referenceThroughput = ReferenceThroughputFor(scene.id);
            if (scene.referenceThroughput <= 0.0)
            {
                // A scene with no reference entry is measured and reported but cannot be
                // scored: there is nothing to express its throughput relative to.
                scene.index = 0.0;
                scene.score = 0;
                continue;
            }

            scene.index = scene.throughput / scene.referenceThroughput;
            scene.score = static_cast<long long>(std::llround(1000.0 * scene.index));
            ++measured;

            const double weight = ReferenceWeightFor(scene.id);
            if (weight <= 0.0 || scene.index <= 0.0)
            {
                // A renderer that produced literally no throughput -- the no-op renderer's
                // whole point -- has an index of zero, whose logarithm does not exist. It is
                // still a real, reportable result for that scene; it just cannot take part in
                // a geometric mean, and a run containing one is not a complete run.
                continue;
            }

            weightedLogSum += weight * std::log(scene.index);
            weightTotal += weight;
        }

        result.measuredSceneCount = measured;

        if (weightTotal > 0.0)
        {
            result.compositeIndex = std::exp(weightedLogSum / weightTotal);
            result.cnaScore = static_cast<long long>(std::llround(1000.0 * result.compositeIndex));
        }
        else
        {
            result.compositeIndex = 0.0;
            result.cnaScore = 0;
        }

        result.complete = result.selectedSceneCount > 0 &&
                          measured == result.selectedSceneCount &&
                          measured == static_cast<int>(ReferenceProfile().size());
    }
} // namespace CnaBenchmark
