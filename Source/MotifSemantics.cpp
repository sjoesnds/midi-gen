#include "MotifSemantics.h"

#include <algorithm>

namespace
{
uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int pick (uint32_t seed, int count)
{
    return count > 0 ? (int) (mix32 (seed) % (uint32_t) count) : 0;
}

float unit (uint32_t seed)
{
    return (float) (mix32 (seed) % 1000u) / 999.0f;
}
}

namespace midiforge
{
MotifSemantics::Plan MotifSemantics::makePlan (int melodyType,
                                               int mood,
                                               int genre,
                                               float energy,
                                               float complexity,
                                               uint32_t identity)
{
    Plan p;

    const float e = std::clamp (energy, 0.0f, 1.0f);
    const float c = std::clamp (complexity, 0.0f, 1.0f);

    // One semantic family controls all late phrase mutations. The old version
    // rolled every axis independently, so the semantic pass could destroy the
    // musical language already chosen by CreativeRange + addMelody().
    static constexpr int rhythmic[8][2] =
    {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 5 },
        { 3, 4 }, { 1, 2 }, { 4, 5 }, { 1, 5 }
    };
    static constexpr int interval[8][2] =
    {
        { 0, 1 }, { 1, 1 }, { 2, 3 }, { 0, 0 },
        { 2, 4 }, { 3, 4 }, { 0, 1 }, { 1, 5 }
    };
    static constexpr int anchor[8][2] =
    {
        { 0, 1 }, { 0, 1 }, { 2, 3 }, { 0, 0 },
        { 1, 2 }, { 2, 3 }, { 0, 1 }, { 0, 2 }
    };
    static constexpr int peak[8][2] =
    {
        { 1, 2 }, { 3, 4 }, { 3, 5 }, { 0, 1 },
        { 2, 4 }, { 2, 3 }, { 1, 3 }, { 2, 4 }
    };
    static constexpr int ending[8][2] =
    {
        { 0, 1 }, { 1, 3 }, { 2, 3 }, { 4, 4 },
        { 0, 1 }, { 1, 5 }, { 4, 4 }, { 0, 4 }
    };
    static constexpr int signature[8][2] =
    {
        { 0, 1 }, { 0, 1 }, { 2, 3 }, { 0, 0 },
        { 1, 2 }, { 2, 3 }, { 0, 1 }, { 1, 2 }
    };
    static constexpr int answer[8][2] =
    {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 2, 3 },
        { 1, 3 }, { 0, 4 }, { 2, 3 }, { 0, 3 }
    };
    static constexpr int primary[8][2] =
    {
        { 4, 6 }, { 0, 4 }, { 1, 5 }, { 0, 4 },
        { 3, 5 }, { 2, 6 }, { 4, 0 }, { 1, 4 }
    };
    static constexpr int secondary[8][2] =
    {
        { 0, 2 }, { 2, 6 }, { 3, 6 }, { 2, 4 },
        { 1, 4 }, { 0, 5 }, { 2, 5 }, { 3, 6 }
    };

    const int typeFamily[8] = { 0, 7, 2, 3, 4, 1, 6, 7 };
    int family = typeFamily[juce::jlimit (0, 7, melodyType)];

    // Identity may switch to one neighboring semantic family, but never
    // completely reshuffles every axis independently.
    const uint32_t familyHash = mix32 (
        identity ^ (uint32_t) (melodyType + 1) * 0x9e3779b9u
        ^ (uint32_t) (genre + 17) * 0x85ebca6bu);
    if ((familyHash % 100u) < 28u)
        family = (family + 1 + (int) ((familyHash >> 8) & 1u)) % 8;

    if (genre == 14 && (familyHash % 100u) < 52u)
        family = (family + 2) % 8;

    const int variant = (int) ((familyHash >> 16) & 1u);

    p.rhythmicCore = rhythmic[family][variant];
    p.intervalCore = interval[family][variant];
    p.startingAnchor = anchor[family][variant];
    p.peakGesture = peak[family][variant];
    p.endingGesture = ending[family][variant];
    p.signatureLeap = signature[family][variant];
    p.answerCell = answer[family][variant];
    p.primaryMutation = primary[family][variant];
    p.secondaryMutation = secondary[family][variant];

    // Strength is intentionally modest. The semantic pass should expose the
    // phrase's identity, not replace it.
    p.mutationStrength = std::clamp (
        0.20f + 0.10f * c + 0.04f * unit (familyHash ^ 0x11u),
        0.18f, 0.34f);

    p.contrastStrength = std::clamp (
        0.30f + 0.10f * e + 0.08f * c
        + 0.05f * unit (familyHash ^ 0x22u),
        0.26f, 0.52f);

    p.returnStrength = std::clamp (
        0.52f + 0.08f * (1.0f - c)
        + 0.05f * unit (familyHash ^ 0x33u),
        0.48f, 0.66f);

    // Context bends one semantic dimension instead of replacing the family.
    if (mood == 1 || mood == 7)
        p.signatureLeap = (p.signatureLeap + 1) % 6;

    if (mood == 2 || mood == 5)
        p.endingGesture = (p.endingGesture + 1) % 7;

    if (mood == 3 || mood == 8)
        p.peakGesture = (p.peakGesture + 1) % 7;

    if (melodyType == 6)
    {
        p.rhythmicCore = (p.rhythmicCore + 1) % 8;
        p.answerCell = (p.answerCell + 2) % 7;
        p.mutationStrength *= 0.72f;
    }

    return p;
}
}
} // namespace midiforge
