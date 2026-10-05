#include "LoopClosure.h"

#include <algorithm>
#include <cmath>

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
LoopClosure::Plan LoopClosure::makePlan (int melodyType,
                                         int mood,
                                                                                  float energy,
                                         float complexity,
                                         uint32_t identity)
{
    Plan p;

    const uint32_t base = mix32 (
        identity
        ^ (uint32_t) (melodyType + 3) * 0x9e3779b9u
        ^ (uint32_t) (mood + 13) * 0x85ebca6bu
        ^ 0xc2b2ae35u);

    p.bridgeStyle = pick (base ^ 0x11111111u, 6);
    p.targetStrategy = pick (base ^ 0x22222222u, 6);
    p.pickupStyle = pick (base ^ 0x33333333u, 5);
    p.releaseStyle = pick (base ^ 0x44444444u, 5);

    const float e = std::clamp (energy, 0.0f, 1.0f);
    const float c = std::clamp (complexity, 0.0f, 1.0f);

    p.returnStrength = 0.58f
        + 0.20f * (1.0f - c)
        + 0.10f * unit (base ^ 0x55u);

    p.boundaryDistance = 0.28f
        + 0.22f * c
        + 0.10f * unit (base ^ 0x66u);

    p.pickupBias = 0.18f
        + 0.22f * e
        + 0.10f * unit (base ^ 0x77u);

    p.sustainBias = 0.26f
        + 0.24f * (1.0f - c)
        + 0.08f * unit (base ^ 0x88u);

    p.unresolvedBias = 0.12f
        + 0.12f * e
        + 0.10f * c
        + 0.06f * unit (base ^ 0x99u);

    // Soft musical-context nudges. The planner remains free to choose any
    // closure language, but these contexts slightly change what is useful.
    if (mood == 1 || mood == 7) // dark / mysterious
        p.unresolvedBias += 0.08f;

    if (mood == 3 || mood == 8) // euphoric / energetic
        p.pickupBias += 0.08f;

    if (melodyType == 1 || melodyType == 6) // vocal-like / sparse lead
        p.boundaryDistance += 0.06f;

    p.unresolvedBias = std::clamp (p.unresolvedBias, 0.08f, 0.42f);
    p.pickupBias = std::clamp (p.pickupBias, 0.10f, 0.52f);

    return p;
}
} // namespace midiforge
