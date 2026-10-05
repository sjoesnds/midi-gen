#include "MelodyIntent.h"

#include <algorithm>

namespace
{
static int clampType (int value) { return std::clamp (value, 0, 7); }
static int clampMood (int value) { return std::clamp (value, 0, 8); }

static const midiforge::MelodyIntent::Character characterAt (int index)
{
    static constexpr midiforge::MelodyIntent::Character pool[] =
    {
        { 0.26f, -0.16f, -0.10f, -0.12f, 0.16f, 0.24f, 0.24f, -0.08f, -0.08f,  0,  0,  4, 0, 0 },
        {-0.10f,  0.18f,  0.04f,  0.18f, 0.08f, -0.20f, 0.10f,  0.04f,  0.12f,  5,  3, 7, 5, 2 },
        { 0.14f, -0.06f, -0.08f, -0.02f, 0.34f, 0.08f, 0.42f, -0.02f, -0.10f,  1,  0, 0, 0, 0 },
        { 0.20f, -0.12f, -0.14f, -0.08f, 0.08f, 0.30f, 0.18f,  0.02f,  0.02f,  3,  1, 4, 2, 1 },
        { 0.12f, -0.02f,  0.02f,  0.26f, 0.02f, 0.02f, 0.06f,  0.00f,  0.22f, 10, 8, 9, 5, 4 },
        {-0.02f,  0.10f,  0.16f,  0.02f, 0.12f,-0.02f, 0.00f,  0.12f,  0.28f, 13, 6, 2, 4, 6 },
        { 0.08f, -0.02f,  0.10f, -0.04f, 0.18f, 0.10f, 0.06f, -0.10f, -0.02f,  9, 7, 5, 2, 1 },
        {-0.04f,  0.08f, 0.28f, 0.18f, -0.08f,-0.08f,-0.06f,  0.16f,  0.30f,  7,10, 8, 6, 6 },
        { 0.30f, -0.20f, -0.12f, -0.18f, -0.06f, 0.34f, 0.12f,  0.18f, -0.16f, 14, 2, 4, 7, 0 },
        {-0.12f,  0.22f,  0.18f,  0.22f, 0.04f,-0.18f, 0.02f,  0.04f,  0.14f,  4, 9, 6, 1, 5 },
        { 0.24f, -0.18f, -0.04f, -0.12f, 0.12f, 0.38f, 0.26f,  0.10f, -0.12f, 11, 4, 3, 3, 0 },
        {-0.16f,  0.20f,  0.24f,  0.24f, -0.02f,-0.22f,-0.08f, -0.02f,  0.20f, 16,11,10, 6, 7 }
    };

    constexpr int count = (int) (sizeof (pool) / sizeof (pool[0]));
    return pool[std::clamp (index, 0, count - 1)];
}

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
}

namespace midiforge
{
MelodyIntent MelodyIntent::makePlan (int bars,
                                      int melodyType,
                                      int mood,
                                      float energy,
                                      float complexity,
                                      uint32_t identity,
                                      int nativeArchetype)
{
    MelodyIntent intent;

    const int type = clampType (melodyType);
    const int safeMood = clampMood (mood);
    const float e = std::clamp (energy, 0.0f, 1.0f);
    const float c = std::clamp (complexity, 0.0f, 1.0f);

    intent.nativeArchetype = std::clamp (nativeArchetype, 0, 7);
    // The caller owns the identity derivation. Keep it intact so this wrapper
    // does not silently change seeded musical results.
    intent.language = CreativeRange::makePlan (type, safeMood, e, c, identity);
    intent.grammar = ComposerGrammar::makePlan (
        bars, e, c, type, safeMood, mix32 (identity ^ 0xC0A70970u));

    switch (safeMood)
    {
        case 1: intent.moodSpace=.08f; intent.moodLeap=.10f; intent.moodDensity=-.04f; intent.moodTension=.24f; break;
        case 2: intent.moodSpace=.16f; intent.moodLeap=-.05f; intent.moodDensity=-.08f; intent.moodTension=.18f; break;
        case 3: intent.moodSpace=-.10f; intent.moodLeap=.10f; intent.moodDensity=.10f; intent.moodTension=-.08f; break;
        case 4: intent.moodSpace=-.08f; intent.moodLeap=.24f; intent.moodDensity=.14f; intent.moodTension=.12f; break;
        case 5: intent.moodSpace=.24f; intent.moodLeap=-.10f; intent.moodDensity=-.12f; intent.moodTension=.04f; break;
        case 6: intent.moodSpace=.08f; intent.moodLeap=-.02f; intent.moodDensity=-.02f; intent.moodTension=.10f; break;
        case 7: intent.moodSpace=.18f; intent.moodLeap=.12f; intent.moodDensity=-.06f; intent.moodTension=.22f; break;
        case 8: intent.moodSpace=-.14f; intent.moodLeap=.16f; intent.moodDensity=.18f; intent.moodTension=-.02f; break;
        default: break;
    }

    static constexpr float typeSpace[]   = {-.04f,.16f,-.02f,.18f,.02f,.10f,.28f,.04f};
    static constexpr float typeDensity[] = {.04f,-.10f,.06f,-.08f,.12f,-.04f,-.18f,.02f};
    static constexpr float typeLeap[]    = {.02f,.04f,.18f,-.02f,.08f,.12f,.06f,.10f};
    static constexpr float typeMotif[]   = {.16f,.12f,.10f,.18f,.04f,.08f,.14f,.10f};

    intent.roleSpace = typeSpace[type];
    intent.roleDensity = typeDensity[type];
    intent.roleLeap = typeLeap[type];
    intent.roleMotif = typeMotif[type];

    intent.dnaSpace = std::clamp (
        0.28f + 0.58f * intent.language.durationContrast,
        0.0f, 1.0f);
    intent.dnaLeap = intent.language.leapBias;
    intent.dnaSync = std::clamp (
        0.16f + 0.06f * (float) (intent.language.rhythmFamily % 12),
        0.0f, 1.0f);
    intent.dnaDensity = std::clamp (
        0.32f + 0.34f * (1.0f - intent.language.repetition)
        + 0.16f * intent.language.asymmetry,
        0.0f, 1.0f);
    intent.dnaRegister = std::clamp (
        0.50f + 0.05f * intent.language.registerBias,
        0.0f, 1.0f);
    intent.dnaMotif = intent.language.repetition;
    intent.dnaRhythmBias = std::max (0, intent.language.rhythmFamily % 8);

    constexpr float kModernSpaceBias = 0.04f;
    intent.dnaSpace = std::clamp (
        intent.dnaSpace + intent.moodSpace + intent.roleSpace + kModernSpaceBias,
        0.0f, 1.0f);
    intent.dnaLeap = std::clamp (
        intent.dnaLeap + intent.moodLeap + intent.roleLeap,
        0.0f, 1.0f);
    intent.dnaDensity = std::clamp (
        intent.dnaDensity + intent.moodDensity + intent.roleDensity,
        0.0f, 1.0f);
    intent.dnaMotif = std::clamp (
        intent.dnaMotif + intent.roleMotif,
        0.0f, 1.0f);

    const int characterCount = 12;
    const int characterRotation = (int) (
        mix32 (identity
               ^ (uint32_t) type * 0x85ebca6bu
               ^ 0xC4A11CE5u) % (uint32_t) characterCount);
    intent.characterIndex = (intent.nativeArchetype + characterRotation) % characterCount;
    intent.character = characterAt (intent.characterIndex);

    intent.dnaSpace = std::clamp (intent.dnaSpace + intent.character.spaceBias, 0.0f, 1.0f);
    intent.dnaDensity = std::clamp (intent.dnaDensity + intent.character.densityBias, 0.0f, 1.0f);
    intent.dnaLeap = std::clamp (intent.dnaLeap + intent.character.leapBias, 0.0f, 1.0f);
    intent.dnaSync = std::clamp (intent.dnaSync + intent.character.syncBias, 0.0f, 1.0f);
    intent.dnaMotif = std::clamp (intent.dnaMotif + intent.character.motifBias, 0.0f, 1.0f);
    intent.dnaRegister = std::clamp (intent.dnaRegister + intent.character.registerBias, 0.0f, 1.0f);

    intent.simpleProbability =
        0.46f - 0.16f * c;
    intent.complexProbability =
        0.10f + 0.17f * c;

    switch (intent.nativeArchetype)
    {
        case 0: intent.simpleProbability += 0.03f; intent.complexProbability -= 0.02f; break;
        case 1: intent.simpleProbability -= 0.01f; break;
        case 2: intent.simpleProbability += 0.01f; break;
        case 3: intent.simpleProbability += 0.05f; intent.complexProbability -= 0.02f; break;
        case 4: intent.simpleProbability += 0.13f; intent.complexProbability -= 0.06f; break;
        case 5: intent.simpleProbability -= 0.10f; intent.complexProbability += 0.10f; break;
        case 6: intent.simpleProbability += 0.04f; break;
        default: intent.complexProbability += 0.03f; break;
    }

    intent.simpleProbability = std::clamp (intent.simpleProbability, 0.18f, 0.68f);
    intent.complexProbability = std::clamp (intent.complexProbability, 0.08f, 0.38f);
    if (intent.simpleProbability + intent.complexProbability > 0.90f)
        intent.complexProbability = std::max (0.08f, 0.90f - intent.simpleProbability);

    intent.phraseStyle = intent.language.contourFamily;
    intent.intervalLanguage = intent.language.intervalFamily;
    intent.registerProfile = intent.language.registerJourney;
    intent.rhythmicLanguage = intent.language.rhythmFamily;

    intent.tensionProfile = (int) (
        mix32 (identity
               ^ 0x7f4a7c15u
               ^ (uint32_t) intent.language.harmonyPersonality
               ^ (uint32_t) (intent.character.tensionBias + 17) * 0x9e3779b9u) % 8u);

    return intent;
}
} // namespace midiforge