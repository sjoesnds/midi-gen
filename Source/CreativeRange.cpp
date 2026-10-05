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

    /*
        0.86.x Melody Language Pass:
        The old plan rolled almost every melodic axis independently. That gave us
        combinations that were numerically different but musically incoherent:
        e.g. a rising contour + angular interval grammar + sparse rhythm + high
        register could all come from unrelated random picks.

        A CreativeRange is now one coherent phrase language. Identity still chooses
        among languages, but the contour / interval / rhythm / repetition /
        register / duration decisions come from the same profile. Context only bends
        that profile slightly.
    */
    struct Language
    {
        float novelty;
        float asymmetry;
        float leapBias;
        float repetition;
        float harmonyColor;
        float durationContrast;
        int contourFamily;
        int intervalFamily;
        int rhythmFamily;
        int repetitionStyle;
        int registerJourney;
        int harmonyPersonality;
        int durationStyle;
    };

    static constexpr Language languages[12] =
    {
        // memorable hook / chant
        { 0.42f, 0.24f, 0.16f, 0.78f, 0.28f, 0.42f,  1, 1,  0, 7, 0, 0, 4 },
        // call -> response / conversational
        { 0.54f, 0.48f, 0.26f, 0.58f, 0.42f, 0.50f, 10, 7, 11, 1, 1, 3, 2 },
        // lyrical / emotional arc
        { 0.48f, 0.30f, 0.20f, 0.68f, 0.36f, 0.72f, 14, 1,  4, 4, 2, 2, 3 },
        // pocket / groove
        { 0.50f, 0.54f, 0.22f, 0.52f, 0.30f, 0.30f,  0, 1, 14, 2, 0, 1, 1 },
        // riff / angular hook
        { 0.66f, 0.58f, 0.54f, 0.46f, 0.52f, 0.38f,  6, 3,  8, 5, 3, 5, 5 },
        // minimal / spacious
        { 0.30f, 0.18f, 0.12f, 0.82f, 0.24f, 0.74f,  8, 0,  5, 0, 0, 0, 4 },
        // descending / dark
        { 0.58f, 0.46f, 0.38f, 0.56f, 0.66f, 0.60f, 11, 9, 10, 6, 2, 6, 5 },
        // wide / dramatic
        { 0.76f, 0.62f, 0.68f, 0.34f, 0.58f, 0.46f, 17, 5, 12, 3, 5, 7, 1 },
        // playful / rebound
        { 0.62f, 0.50f, 0.32f, 0.64f, 0.34f, 0.54f,  3, 2,  7, 5, 1, 2, 2 },
        // vocal / held statement
        { 0.38f, 0.28f, 0.18f, 0.74f, 0.40f, 0.78f,  2, 1,  4, 7, 0, 1, 3 },
        // ostinato / driving cell
        { 0.44f, 0.34f, 0.20f, 0.86f, 0.22f, 0.26f,  0, 0, 15, 7, 0, 0, 1 },
        // late peak / phrase
        { 0.60f, 0.42f, 0.30f, 0.60f, 0.46f, 0.66f, 13, 4,  5, 4, 4, 4, 3 }
    };

    static constexpr int preferredLanguageByType[8] =
    {
        0,  // Hook
        9,  // Vocal
        4,  // Riff
        10, // Ostinato
        7,  // Arp
        1,  // Counter
        5,  // Sparse Lead
        11  // Phrase
    };

    const int preferred = preferredLanguageByType[std::clamp (melodyType, 0, 7)];
    const bool usePreferred =
        unit (mix32 (h0 ^ (uint32_t) (melodyType + 17) * 0x45d9f3bu)) < 0.62f;

    const int randomLanguage = pick (mix32 (h1 ^ h2 ^ 0x6C8E9CF5u), 12);
    const int languageIndex = usePreferred ? preferred : randomLanguage;
    const auto& language = languages[languageIndex];

    const float identityJitterA = unit (mix32 (h0 ^ h3 ^ 0x94D049BBu)) - 0.5f;
    const float identityJitterB = unit (mix32 (h1 ^ h2 ^ 0x2545F491u)) - 0.5f;
    const float identityJitterC = unit (mix32 (h0 ^ h2 ^ 0x5E2D58D8u)) - 0.5f;

    p.novelty = std::clamp (
        language.novelty + 0.08f * identityJitterA
        + 0.08f * c + 0.04f * e, 0.12f, 0.90f);

    p.asymmetry = std::clamp (
        language.asymmetry + 0.08f * identityJitterB
        + 0.10f * c + 0.03f * e, 0.08f, 0.90f);

    p.leapBias = std::clamp (
        language.leapBias + 0.07f * identityJitterC
        + 0.12f * c + 0.05f * e, 0.06f, 0.86f);

    p.repetition = std::clamp (
        language.repetition - 0.05f * c + 0.06f * (1.0f - c)
        + 0.06f * identityJitterA, 0.16f, 0.90f);

    p.harmonyColor = std::clamp (
        language.harmonyColor + 0.07f * identityJitterB
        + 0.04f * c, 0.10f, 0.86f);

    p.durationContrast = std::clamp (
        language.durationContrast + 0.08f * identityJitterC
        + 0.04f * c, 0.10f, 0.90f);

    // Role tweaks remain intentionally small. They refine a language instead of
    // replacing it with independent random axes.
    if (melodyType == 0)          p.repetition += 0.08f; // Hook
    if (melodyType == 1)          p.durationContrast += 0.06f; // Vocal
    if (melodyType == 2)          p.leapBias += 0.08f; // Riff
    if (melodyType == 5)          p.repetition += 0.04f; // Counter
    if (melodyType == 6)          p.asymmetry += 0.06f; // Sparse lead
    if (melodyType == 7)          p.repetition -= 0.06f; // Phrase

    // Mood remains a soft coloration layer, as before.
    if (mood == 0 || mood == 1) p.durationContrast += 0.05f;
    if (mood == 3 || mood == 7) p.leapBias += 0.06f;
    if (mood == 4 || mood == 6) p.repetition -= 0.04f;

    // No genre conditioning. Creativity comes from the language profile,
    // identity, mood, role, energy and complexity.

    p.novelty = std::clamp (p.novelty, 0.10f, 0.94f);
    p.asymmetry = std::clamp (p.asymmetry, 0.08f, 0.92f);
    p.leapBias = std::clamp (p.leapBias, 0.06f, 0.90f);
    p.repetition = std::clamp (p.repetition, 0.12f, 0.94f);
    p.harmonyColor = std::clamp (p.harmonyColor, 0.08f, 0.92f);
    p.durationContrast = std::clamp (p.durationContrast, 0.08f, 0.94f);

    p.contourFamily = language.contourFamily;
    p.intervalFamily = language.intervalFamily;
    p.rhythmFamily = language.rhythmFamily;

    static constexpr int biases[12] = { -11, 8, -5, 13, -17, 5, 11, -8, 16, -14, 20, -20 };
    p.rhythmBias = biases[p.rhythmFamily];

    p.repetitionStyle = language.repetitionStyle;
    p.registerJourney = language.registerJourney;
    p.harmonyPersonality = language.harmonyPersonality;
    p.durationStyle = language.durationStyle;

    static constexpr int registerBiasTable[8] = { 0, 3, -3, 5, -5, 2, -2, 1 };
    p.registerBias = (float) registerBiasTable[p.registerJourney];

    return p;
}
} // namespace midiforge
