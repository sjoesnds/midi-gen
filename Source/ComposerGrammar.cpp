#include "ComposerGrammar.h"

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
}

namespace midiforge
{
ComposerGrammar::PhraseState ComposerGrammar::Plan::stateFor (int phrase) const
{
    if (phrases.empty())
        return {};

    const int safe = std::clamp (phrase, 0, (int) phrases.size() - 1);
    return phrases[(size_t) safe];
}

const char* ComposerGrammar::roleName (Role role)
{
    switch (role)
    {
        case Statement: return "STATEMENT";
        case Develop:   return "DEVELOP";
        case Build:     return "BUILD";
        case Contrast:  return "CONTRAST";
        case Peak:      return "PEAK";
        case Release:   return "RELEASE";
        case Return:    return "RETURN";
        default:        return "STATEMENT";
    }
}

ComposerGrammar::Plan ComposerGrammar::makePlan (int bars,
                                                 float energy,
                                                 float complexity,
                                                 int melodyType,
                                                 int mood,
                                                 int genre,
                                                 uint32_t identity)
{
    Plan plan;
    plan.bars = std::max (1, bars);

    const int phraseCount = std::max (1, (plan.bars + 3) / 4);
    plan.phrases.reserve ((size_t) phraseCount);

    const float e = std::clamp (energy, 0.0f, 1.0f);
    const float c = std::clamp (complexity, 0.0f, 1.0f);

    // The role sequence is the actual composition grammar. Four-bar loops keep
    // the familiar A -> A' -> B -> A'' sentence, while longer loops can breathe:
    // statement -> develop -> build -> peak -> release -> return.
    for (int i = 0; i < phraseCount; ++i)
    {
        Role role = Statement;

        if (i == 0)
            role = Statement;
        else if (phraseCount == 2)
            role = Return;
        else if (phraseCount == 3)
            role = (i == 1 ? (e + c > 1.0f ? Peak : Contrast) : Return);
        else
        {
            const float t = (float) i / (float) std::max (1, phraseCount - 1);

            if (i == phraseCount - 1)
                role = Return;
            else if (t < 0.34f)
                role = Develop;
            else if (t < 0.55f)
                role = Build;
            else if (t < 0.78f)
                role = (e + c > 1.02f ? Peak : Contrast);
            else
                role = Release;
        }

        PhraseState state;
        state.role = role;

        switch (role)
        {
            case Statement:
                state.tension = 0.23f;
                state.density = 0.48f;
                state.space = 0.60f;
                state.registerLift = 0.0f;
                state.velocityLift = 0.0f;
                state.sustainBias = 0.10f;
                state.cadencePull = 0.12f;
                state.motifStrength = 0.90f;
                state.legacyRole = 0;
                break;

            case Develop:
                state.tension = 0.40f;
                state.density = 0.55f;
                state.space = 0.53f;
                state.registerLift = 1.0f;
                state.velocityLift = 0.025f;
                state.sustainBias = 0.03f;
                state.cadencePull = 0.18f;
                state.motifStrength = 0.76f;
                state.legacyRole = 1;
                break;

            case Build:
                state.tension = 0.56f;
                state.density = 0.64f;
                state.space = 0.43f;
                state.registerLift = 2.5f;
                state.velocityLift = 0.055f;
                state.sustainBias = -0.03f;
                state.cadencePull = 0.14f;
                state.motifStrength = 0.62f;
                state.legacyRole = 1;
                break;

            case Contrast:
                state.tension = 0.67f;
                state.density = 0.58f;
                state.space = 0.44f;
                state.registerLift = 4.0f;
                state.velocityLift = 0.075f;
                state.sustainBias = -0.06f;
                state.cadencePull = 0.11f;
                state.motifStrength = 0.48f;
                state.legacyRole = 2;
                break;

            case Peak:
                state.tension = 0.80f;
                state.density = 0.72f;
                state.space = 0.30f;
                state.registerLift = 5.5f;
                state.velocityLift = 0.10f;
                state.sustainBias = -0.09f;
                state.cadencePull = 0.06f;
                state.motifStrength = 0.50f;
                state.legacyRole = 2;
                break;

            case Release:
                state.tension = 0.49f;
                state.density = 0.46f;
                state.space = 0.67f;
                state.registerLift = -0.8f;
                state.velocityLift = 0.015f;
                state.sustainBias = 0.12f;
                state.cadencePull = 0.42f;
                state.motifStrength = 0.70f;
                state.legacyRole = 3;
                break;

            case Return:
                state.tension = 0.33f;
                state.density = 0.42f;
                state.space = 0.70f;
                state.registerLift = -2.0f;
                state.velocityLift = -0.015f;
                state.sustainBias = 0.22f;
                state.cadencePull = 0.92f;
                state.motifStrength = 0.88f;
                state.legacyRole = 3;
                break;
        }

        state.tension += (e - 0.50f) * 0.18f;
        state.density += (e - 0.50f) * 0.12f + (c - 0.50f) * 0.06f;
        state.space -= (e - 0.50f) * 0.08f;
        state.registerLift += (c - 0.50f) * 1.6f;
        state.velocityLift += (e - 0.50f) * 0.045f;
        state.sustainBias += (0.50f - e) * 0.08f;

        if (melodyType == 1 || melodyType == 6)
        {
            state.density -= 0.05f;
            state.space += 0.07f;
            state.sustainBias += 0.035f;
        }
        else if (melodyType == 2 || melodyType == 4)
        {
            state.density += 0.04f;
            state.registerLift += 0.7f;
            state.tension += 0.025f;
        }

        if (mood == 0 || mood == 1)
            state.space += 0.04f;
        if (mood == 3 || mood == 7)
        {
            state.density += 0.04f;
            state.tension += 0.04f;
        }
        if (genre == 5 || genre == 12 || genre == 14)
            state.registerLift += 0.7f;

        const uint32_t h = mix32 (identity
                                  ^ (uint32_t) (i + 1) * 0x9e3779b9u
                                  ^ (uint32_t) (melodyType + 11) * 0x85ebca6bu);
        const float jitter = ((float) (h % 1001u) / 1000.0f - 0.5f);

        state.tension      = std::clamp (state.tension + jitter * 0.045f, 0.05f, 0.94f);
        state.density      = std::clamp (state.density + jitter * 0.055f, 0.16f, 0.88f);
        state.space        = std::clamp (state.space - jitter * 0.045f, 0.14f, 0.90f);
        state.registerLift = std::clamp (state.registerLift + jitter * 1.2f, -3.5f, 8.0f);
        state.velocityLift = std::clamp (state.velocityLift + jitter * 0.018f, -0.10f, 0.16f);
        state.sustainBias  = std::clamp (state.sustainBias + jitter * 0.035f, -0.20f, 0.28f);

        plan.phrases.push_back (state);
    }

    if (plan.phrases.size() > 1)
    {
        float movement = 0.0f;
        for (size_t i = 1; i < plan.phrases.size(); ++i)
            movement += std::abs (plan.phrases[i].tension - plan.phrases[i - 1].tension);

        plan.arc = std::clamp (movement / (float) (plan.phrases.size() - 1), 0.12f, 0.72f);
    }

    return plan;
}
}
