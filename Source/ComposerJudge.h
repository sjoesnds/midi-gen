#pragma once

#include <algorithm>
#include <cmath>
#include "MidiForgeAblation.h"

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

        const float registerFit = c (m.registerScore);

        // Headless ablation excludes one top-level group and renormalizes the
        // remainder. Normal QA and production builds keep the exact original path.
        const bool groupAblated =
            midiforge::qa::isAblated ("judge:idea")
            || midiforge::qa::isAblated ("judge:expression")
            || midiforge::qa::isAblated ("judge:harmony")
            || midiforge::qa::isAblated ("judge:rhythm")
            || midiforge::qa::isAblated ("judge:novelty")
            || midiforge::qa::isAblated ("judge:register_fit")
            || midiforge::qa::isAblated ("judge:closure")
            || midiforge::qa::isAblated ("judge:density_space")
            || midiforge::qa::isAblated ("judge:role_consistency");
        if (groupAblated)
        {
            float weighted = 0.0f;
            float totalWeight = 0.0f;
            const auto addGroup = [&] (const char* name, float weight, float value)
            {
                if (! midiforge::qa::isAblated (name))
                {
                    weighted += weight * value;
                    totalWeight += weight;
                }
            };
            addGroup ("judge:idea", 0.22f, idea);
            addGroup ("judge:expression", 0.18f, expression);
            addGroup ("judge:harmony", 0.16f, harmony);
            addGroup ("judge:rhythm", 0.13f, rhythm);
            addGroup ("judge:novelty", 0.11f, novelty);
            addGroup ("judge:register_fit", 0.07f, registerFit);
            addGroup ("judge:closure", 0.07f, c (m.closure));
            addGroup ("judge:density_space", 0.04f, c (m.densitySpace));
            addGroup ("judge:role_consistency", 0.02f, c (m.roleConsistency));
            return totalWeight > 0.0f
                ? std::clamp (weighted / totalWeight, 0.0f, 1.0f)
                : 0.5f;
        }

        return std::clamp (
            0.22f * idea
            + 0.18f * expression
            + 0.16f * harmony
            + 0.13f * rhythm
            + 0.11f * novelty
            + 0.07f * registerFit
            + 0.07f * c (m.closure)
            + 0.04f * c (m.densitySpace)
            + 0.02f * c (m.roleConsistency),
            0.0f, 1.0f);
    }
};
} // namespace midiforge
