#include "CreativeRange.h"

#include <algorithm>
#include <cmath>

namespace
{
static uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float unit (uint32_t x)
{
    return (float) (x % 1001u) / 1000.0f;
}

static int pick (uint32_t h, int count)
{
    return count > 0 ? (int) (h % (uint32_t) count) : 0;
}
}

namespace midiforge
{
CreativeRange::Plan CreativeRange::makePlan (int melodyType,
                                             int mood,
                                             int genre,
                                             float energy,
                                             float complexity,
                                             uint32_t identity)
{
    Plan p;
    const float e = std::clamp (energy, 0.0f, 1.0f);
    const float c = std::clamp (complexity, 0.0f, 1.0f);

    const uint32_t h0 = mix32 (identity ^ 0xC7219E31u);
    const uint32_t h1 = mix32 (identity ^ 0x9E3779B9u);
    const uint32_t h2 = mix32 (identity ^ 0x51ED270Bu);
    const uint32_t h3 = mix32 (identity ^ 0xA17E5EEDu);

    p.novelty = std::clamp (0.34f
        + 0.34f * unit (h0)
        + 0.18f * c
        + 0.10f * e, 0.16f, 0.92f);
    p.asymmetry = std::clamp (0.28f
        + 0.34f * unit (h1)
        + 0.16f * c
        + 0.08f * e, 0.10f, 0.90f);
    p.leapBias = std::clamp (0.18f
        + 0.34f * unit (h2)
        + 0.16f * c
        + 0.08f * e, 0.08f, 0.86f);
    p.repetition = std::clamp (0.38f
        + 0.28f * unit (h3)
        + 0.12f * (1.0f - c), 0.18f, 0.88f);
    p.harmonyColor = std::clamp (0.30f
        + 0.38f * unit (mix32 (h0 ^ h2)), 0.12f, 0.88f);
    p.durationContrast = std::clamp (0.30f
        + 0.44f * unit (mix32 (h1 ^ h3)), 0.12f, 0.90f);

    // Creative space is allowed to react to musical role, but never locks to a genre.
    if (melodyType == 0)          p.repetition += 0.10f; // Hook
    if (melodyType == 1)          p.durationContrast += 0.08f; // Vocal
    if (melodyType == 2)          p.leapBias += 0.12f; // Riff
    if (melodyType == 5)          p.repetition += 0.05f; // Counter
    if (melodyType == 6)          p.asymmetry += 0.08f; // Sparse lead
    if (melodyType == 7)          p.repetition -= 0.08f; // Phrase

    // Mood/genre only bend the distribution. The actual language remains identity-driven.
    if (mood == 0 || mood == 1) p.durationContrast += 0.06f;
    if (mood == 3 || mood == 7) p.leapBias += 0.08f;
    if (mood == 4 || mood == 6) p.repetition -= 0.05f;

    // Experimental / Hyperpop / Cinematic receive more creative degrees of freedom,
    // while Ambient / Lofi keep the freedom expressed through space and duration.
    if (genre == 14 || genre == 13 || genre == 6)
    {
        p.novelty += 0.08f;
        p.asymmetry += 0.07f;
        p.leapBias += 0.08f;
    }
    if (genre == 4 || genre == 15)
    {
        p.repetition += 0.05f;
        p.durationContrast += 0.08f;
    }

    p.novelty = std::clamp (p.novelty, 0.10f, 0.96f);
    p.asymmetry = std::clamp (p.asymmetry, 0.08f, 0.94f);
    p.leapBias = std::clamp (p.leapBias, 0.06f, 0.92f);
    p.repetition = std::clamp (p.repetition, 0.12f, 0.94f);
    p.harmonyColor = std::clamp (p.harmonyColor, 0.08f, 0.94f);
    p.durationContrast = std::clamp (p.durationContrast, 0.08f, 0.94f);

    // 18 contour languages: familiar shapes plus less symmetrical human gestures.
    p.contourFamily = pick (mix32 (h0 ^ (uint32_t) melodyType * 0x45d9f3bu), 18);

    // 12 interval vocabularies. The first ten align with the legacy engine; the last
    // two deliberately create different leap/recovery grammars.
    p.intervalFamily = pick (mix32 (h1 ^ 0x6C8E9CF5u), 12);

    // Rhythm family is separate from raw density. This means a dense line can still
    // be sparse in onset shape, and a sparse line can still have an unusual pocket.
    p.rhythmFamily = pick (mix32 (h2 ^ 0x7A3C19E5u), 12);
    static constexpr int biases[12] = { -11, 8, -5, 13, -17, 5, 11, -8, 16, -14, 20, -20 };
    p.rhythmBias = biases[p.rhythmFamily];

    p.repetitionStyle = pick (mix32 (h3 ^ 0x27D4EB2Du), 8);
    p.registerJourney = pick (mix32 (h0 ^ h3 ^ 0x94D049BBu), 8);
    p.harmonyPersonality = pick (mix32 (h1 ^ h2 ^ 0x2545F491u), 8);
    p.durationStyle = pick (mix32 (h0 ^ h2 ^ 0x5E2D58D8u), 6);

    // Register language is intentionally expressed as a bias plus a journey shape.
    // This lets the widened pitch lane remain optional rather than mandatory.
    static constexpr int registerBiasTable[8] = { 0, 3, -3, 5, -5, 2, -2, 1 };
    p.registerBias = (float) registerBiasTable[p.registerJourney];

    return p;
}
} // namespace midiforge
