#pragma once

#include <algorithm>
#include <cmath>

namespace midiforge
{
struct ComposerJudge
{
    // 0.75 Composer Judge 2.0:
    // A top-level coherence pass over the already-generated musical material.
    // It does not rewrite notes; it only evaluates how well the existing
    // subsystem decisions agree as one composition.
    struct Metrics
    {
        float phraseArc = 0.5f;
        float roleConsistency = 0.5f;
        float motifDevelopment = 0.5f;
        float expression = 0.5f;
        float harmony = 0.5f;
        float densitySpace = 0.5f;
        float rhythm = 0.5f;
        float registerScore = 0.5f;
        float closure = 0.5f;
        float noveltyBalance = 0.5f;
        float groove = 0.5f;
        float prosody = 0.5f;
        float development = 0.5f;
    };

    static float score (const Metrics& m)
    {
        const auto c = [] (float v) { return std::clamp (v, 0.0f, 1.0f); };

        // 0.86.x Judge cleanup: keep the top-level judge deliberately small.
        // The previous version added many partially correlated terms, which
        // rewarded "polished" averages too easily. These nine groups are meant
        // to answer different questions: does the idea exist, does it express
        // something, does harmony support it, does the rhythm have identity,
        // is the register usable, does the loop close, and is there useful novelty.
        const float idea = c (0.50f * m.motifDevelopment
                            + 0.30f * m.phraseArc
                            + 0.20f * m.roleConsistency);

        const float expression = c (0.65f * m.expression
                                  + 0.25f * m.prosody
                                  + 0.10f * m.development);

        const float harmony = c (0.72f * m.harmony
                              + 0.28f * m.closure);

        const float rhythm = c (0.58f * m.rhythm
                              + 0.24f * m.groove
                              + 0.18f * m.densitySpace);

        const float novelty = c (0.65f * m.noveltyBalance
                               + 0.20f * m.development
                               + 0.15f * m.motifDevelopment);

        const float register = c (m.registerScore);

        return std::clamp (
            0.22f * idea
            + 0.18f * expression
            + 0.16f * harmony
            + 0.13f * rhythm
            + 0.11f * novelty
            + 0.07f * register
            + 0.07f * c (m.closure)
            + 0.04f * c (m.densitySpace)
            + 0.02f * c (m.roleConsistency),
            0.0f, 1.0f);
    }
};
} // namespace midiforge
