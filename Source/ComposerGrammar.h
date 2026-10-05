#pragma once

#include <cstdint>
#include <vector>

namespace midiforge
{
class ComposerGrammar
{
public:
    enum Role
    {
        Statement = 0,
        Develop,
        Build,
        Contrast,
        Peak,
        Release,
        Return
    };

    struct PhraseState
    {
        Role role = Statement;
        int legacyRole = 0;       // 0=A, 1=A', 2=B/contrast, 3=A''/return
        float tension = 0.30f;
        float density = 0.50f;
        float space = 0.50f;
        float registerLift = 0.0f;
        float velocityLift = 0.0f;
        float sustainBias = 0.0f;
        float cadencePull = 0.15f;
        float motifStrength = 0.70f;
    };

    struct Plan
    {
        int bars = 4;
        std::vector<PhraseState> phrases;
        float arc = 0.5f;

        PhraseState stateFor (int phrase) const;
    };

    static Plan makePlan (int bars,
                          float energy,
                          float complexity,
                          int melodyType,
                          int mood,
                          uint32_t identity);

    static const char* roleName (Role role);
};
}
