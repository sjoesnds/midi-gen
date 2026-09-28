#include "ComposerJudge.h"

#include <algorithm>

namespace midiforge
{
ComposerJudge::Thresholds ComposerJudge::thresholdsFor (int bars, bool sparse)
{
    Thresholds t;

    if (bars <= 2)
    {
        t.macroArc = 0.22f;
        t.loopClosure = 0.24f;
    }
    else if (bars == 4)
    {
        t.macroArc = 0.30f;
        t.loopClosure = 0.34f;
    }

    if (sparse)
    {
        t.expression -= 0.04f;
        t.rhythm -= 0.04f;
        t.humanity -= 0.05f;
    }

    return t;
}

ComposerJudge::Result ComposerJudge::evaluate (const std::array<float, 7>& axes,
                                               int bars,
                                               bool sparse)
{
    Result r;
    r.axes = axes;

    const auto t = thresholdsFor (bars, sparse);

    static constexpr std::array<float, 7> weights =
    {
        0.18f, // identity
        0.15f, // expression
        0.16f, // harmony
        0.14f, // rhythm
        0.12f, // macro arc
        0.13f, // loop closure
        0.12f  // humanity
    };

    float weighted = 0.0f;
    float weakest = 1.0f;

    for (size_t i = 0; i < r.axes.size(); ++i)
    {
        const float value = std::clamp (r.axes[i], 0.0f, 1.0f);
        weighted += value * weights[i];
        weakest = std::min (weakest, value);
    }

    const std::array<float, 7> minimums =
    {
        t.identity, t.expression, t.harmony, t.rhythm,
        t.macroArc, t.loopClosure, t.humanity
    };

    for (size_t i = 0; i < r.axes.size(); ++i)
        if (r.axes[i] + 0.0001f >= minimums[i])
            ++r.gatesPassed;
        else
            ++r.gateFailures;

    // Judge 2.0 is deliberately not a pure average:
    // a beautiful melody cannot fully hide a broken rhythm/harmony/closure axis.
    const float floorContribution = weakest * 0.42f;
    r.overall = std::clamp (0.58f * weighted + floorContribution, 0.0f, 1.0f);

    const bool corePassed =
        r.axes[0] >= t.identity
        && r.axes[2] >= t.harmony
        && r.axes[3] >= t.rhythm;

    r.passed = corePassed && r.gateFailures <= 1;

    return r;
}
} // namespace midiforge
