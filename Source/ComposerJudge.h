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

        const float phraseArc = c (m.phraseArc);
        const float role = c (m.roleConsistency);
        const float motif = c (m.motifDevelopment);
        const float expression = c (m.expression);
        const float harmony = c (m.harmony);
        const float densitySpace = c (m.densitySpace);
        const float rhythm = c (m.rhythm);
        const float reg = c (m.registerScore);
        const float closure = c (m.closure);
        const float novelty = c (m.noveltyBalance);
        const float groove = c (m.groove);
        const float prosody = c (m.prosody);
        const float development = c (m.development);

        // Cross-system agreement is intentionally low-level rather than a
        // new musical preference. It rewards systems that arrive at compatible
        // conclusions without requiring every subsystem to have the same score.
        const float agreement =
            1.0f - std::clamp (
                (std::abs (motif - development)
                 + std::abs (expression - prosody)
                 + std::abs (harmony - closure)
                 + std::abs (phraseArc - role)) * 0.25f,
                0.0f, 1.0f);

        return std::clamp (
            0.14f * phraseArc
            + 0.12f * role
            + 0.13f * motif
            + 0.10f * expression
            + 0.12f * harmony
            + 0.09f * densitySpace
            + 0.07f * rhythm
            + 0.07f * reg
            + 0.08f * closure
            + 0.05f * novelty
            + 0.03f * groove
            + 0.04f * prosody
            + 0.04f * agreement
            + 0.02f * development,
            0.0f, 1.0f);
    }
};
} // namespace midiforge
