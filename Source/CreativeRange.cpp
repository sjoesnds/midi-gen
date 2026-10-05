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

    // 0.86.x Mood Authority:
    // Mood must choose the actual melodic vocabulary before the generic creative
    // randomizer gets to shape the phrase. Previously contour / interval / rhythm /
    // register families were almost fully identity-random, so a declared mood could
    // easily lose to an unrelated melodic language.
    auto pickFrom = [] (uint32_t seed, const int* values, int count) -> int
    {
        return count > 0 ? values[mix32 (seed) % (uint32_t) count] : 0;
    };

    static constexpr int moodContour[9][6] =
    {
        { 0, 1, 2, 3, 10, 12 }, // Neutral
        { 9, 11, 15, 16, 17, 3 }, // Dark
        { 2, 5, 9, 11, 15, 3 }, // Melancholic
        { 1, 4, 12, 13, 14, 17 }, // Euphoric
        { 6, 7, 12, 16, 17, 10 }, // Aggressive
        { 3, 4, 5, 13, 14, 8 }, // Dreamy
        { 0, 2, 3, 10, 13, 15 }, // Nostalgic
        { 8, 9, 11, 15, 17, 6 }, // Mysterious
        { 1, 6, 12, 14, 16, 17 }  // Energetic
    };
    static constexpr int moodInterval[9][6] =
    {
        { 0, 1, 2, 4, 6, 8 }, // Neutral
        { 3, 7, 9, 10, 11, 4 }, // Dark
        { 1, 4, 7, 9, 11, 0 }, // Melancholic
        { 1, 2, 5, 6, 8, 10 }, // Euphoric
        { 3, 5, 6, 7, 10, 11 }, // Aggressive
        { 0, 1, 4, 7, 9, 2 }, // Dreamy
        { 0, 1, 4, 9, 2, 6 }, // Nostalgic
        { 3, 7, 9, 10, 11, 6 }, // Mysterious
        { 2, 5, 6, 8, 10, 1 }  // Energetic
    };
    static constexpr int moodRhythm[9][6] =
    {
        { 0, 2, 4, 17, 22, 25 }, // Neutral
        { 4, 5, 17, 18, 20, 24 }, // Dark
        { 4, 5, 6, 22, 28, 30 }, // Melancholic
        { 1, 3, 8, 14, 15, 23 }, // Euphoric
        { 8, 9, 10, 11, 13, 24 }, // Aggressive
        { 4, 5, 6, 22, 25, 29 }, // Dreamy
        { 0, 2, 4, 5, 17, 21 }, // Nostalgic
        { 2, 5, 13, 17, 20, 27 }, // Mysterious
        { 1, 3, 8, 9, 14, 15 }  // Energetic
    };
    static constexpr int moodRepeat[9][6] =
    {
        { 0, 1, 2, 3, 4, 5 },
        { 3, 4, 5, 6, 2, 1 },
        { 4, 5, 6, 2, 3, 1 },
        { 0, 1, 2, 3, 5, 4 },
        { 0, 1, 2, 5, 6, 3 },
        { 3, 4, 5, 6, 2, 1 },
        { 2, 3, 4, 5, 6, 1 },
        { 1, 2, 3, 5, 6, 0 },
        { 0, 1, 2, 3, 5, 6 }
    };
    static constexpr int moodRegister[9][6] =
    {
        { 0, 1, 2, 3, 4, 5 },
        { 2, 4, 6, 7, 2, 6 }, // darker / descending / sudden-peak
        { 2, 4, 6, 7, 2, 4 }, // mostly falling / peak-release
        { 1, 3, 5, 7, 1, 3 }, // rising / peak
        { 5, 6, 7, 3, 5, 6 }, // wide orbit / sudden peak
        { 3, 5, 7, 1, 3, 5 }, // floating / peak
        { 0, 1, 3, 5, 0, 3 }, // familiar / gently rising
        { 2, 6, 7, 4, 2, 6 }, // low / orbit / sudden peak
        { 1, 3, 5, 6, 7, 1 }  // rising / peak / orbit
    };
    static constexpr int moodDuration[9][6] =
    {
        { 0, 1, 2, 3, 4, 5 },
        { 2, 3, 4, 5, 2, 4 },
        { 3, 4, 5, 2, 3, 5 },
        { 0, 1, 2, 4, 5, 1 },
        { 0, 1, 2, 4, 5, 0 },
        { 3, 4, 5, 2, 3, 4 },
        { 2, 3, 4, 5, 2, 3 },
        { 2, 3, 5, 4, 2, 5 },
        { 0, 1, 2, 4, 5, 1 }
    };

    const int safeMood = std::clamp (mood, 0, 8);
    p.contourFamily = pickFrom (identity ^ 0xA01u, moodContour[safeMood], 6);
    p.intervalFamily = pickFrom (identity ^ 0xA02u, moodInterval[safeMood], 6);
    p.rhythmFamily = pickFrom (identity ^ 0xA03u, moodRhythm[safeMood], 6);
    p.repetitionStyle = pickFrom (identity ^ 0xA04u, moodRepeat[safeMood], 6);
    p.registerJourney = pickFrom (identity ^ 0xA05u, moodRegister[safeMood], 6);
    p.durationStyle = pickFrom (identity ^ 0xA06u, moodDuration[safeMood], 6);

    // Mood authority on continuous behavior. These are strong enough to be
    // audible, but still leave room for genre / melody type / seed variation.
    switch (safeMood)
    {
        case 1: // Dark
            p.space = 0.62f; p.leapBias = 0.54f; p.repetition = 0.64f;
            p.novelty = 0.54f; p.asymmetry = 0.62f; p.durationContrast = 0.68f;
            break;
        case 2: // Melancholic
            p.space = 0.72f; p.leapBias = 0.24f; p.repetition = 0.72f;
            p.novelty = 0.40f; p.asymmetry = 0.34f; p.durationContrast = 0.78f;
            break;
        case 3: // Euphoric
            p.space = 0.32f; p.leapBias = 0.40f; p.repetition = 0.50f;
            p.novelty = 0.62f; p.asymmetry = 0.50f; p.durationContrast = 0.54f;
            break;
        case 4: // Aggressive
            p.space = 0.28f; p.leapBias = 0.72f; p.repetition = 0.42f;
            p.novelty = 0.72f; p.asymmetry = 0.78f; p.durationContrast = 0.44f;
            break;
        case 5: // Dreamy
            p.space = 0.80f; p.leapBias = 0.20f; p.repetition = 0.62f;
            p.novelty = 0.46f; p.asymmetry = 0.28f; p.durationContrast = 0.86f;
            break;
        case 6: // Nostalgic
            p.space = 0.58f; p.leapBias = 0.28f; p.repetition = 0.76f;
            p.novelty = 0.34f; p.asymmetry = 0.24f; p.durationContrast = 0.60f;
            break;
        case 7: // Mysterious
            p.space = 0.70f; p.leapBias = 0.52f; p.repetition = 0.48f;
            p.novelty = 0.68f; p.asymmetry = 0.72f; p.durationContrast = 0.74f;
            break;
        case 8: // Energetic
            p.space = 0.24f; p.leapBias = 0.58f; p.repetition = 0.46f;
            p.novelty = 0.70f; p.asymmetry = 0.68f; p.durationContrast = 0.46f;
            break;
        default:
            break;
    }

    // Register language is intentionally expressed as a bias plus a journey shape.
    // This lets the widened pitch lane remain optional rather than mandatory.
    static constexpr int registerBiasTable[8] = { 0, 3, -3, 5, -5, 2, -2, 1 };
    p.registerBias = (float) registerBiasTable[p.registerJourney];

    return p;
}
} // namespace midiforge
