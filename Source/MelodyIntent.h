#pragma once

#include "CreativeRange.h"
#include "ComposerGrammar.h"
#include "MotifSemantics.h"

#include <cstdint>

namespace midiforge
{
// One authoritative description of a melody before notes are written.
// Existing grammar modules remain implementation details of this intent;
// callers should consume this plan instead of independently rolling
// CreativeRange + ComposerGrammar + latent Character state.
struct MelodyIntent
{
    struct Character
    {
        float spaceBias = 0.0f;
        float densityBias = 0.0f;
        float leapBias = 0.0f;
        float syncBias = 0.0f;
        float motifBias = 0.0f;
        float sustainBias = 0.0f;
        float repetitionBias = 0.0f;
        float registerBias = 0.0f;
        float contrastBias = 0.0f;
        int contourBias = 0;
        int intervalBias = 0;
        int rhythmBias = 0;
        int registerJourneyBias = 0;
        int tensionBias = 0;
    };

    CreativeRange::Plan language {};
    ComposerGrammar::Plan grammar {};
    MotifSemantics::Plan motif {};
    Character character {};

    int characterIndex = 0;
    int nativeArchetype = 0;

    float moodSpace = 0.0f;
    float moodLeap = 0.0f;
    float moodDensity = 0.0f;
    float moodTension = 0.0f;

    float roleSpace = 0.0f;
    float roleDensity = 0.0f;
    float roleLeap = 0.0f;
    float roleMotif = 0.0f;

    float dnaSpace = 0.50f;
    float dnaLeap = 0.30f;
    float dnaSync = 0.16f;
    float dnaDensity = 0.50f;
    float dnaRegister = 0.50f;
    float dnaMotif = 0.50f;
    int dnaRhythmBias = 0;

    float simpleProbability = 0.46f;
    float complexProbability = 0.18f;

    int phraseStyle = 0;
    int intervalLanguage = 0;
    int tensionProfile = 0;
    int registerProfile = 0;
    int rhythmicLanguage = 0;

    static MelodyIntent makePlan (int bars,
                                  int melodyType,
                                  int mood,
                                  float energy,
                                  float complexity,
                                  uint32_t identity,
                                  int nativeArchetype);
};
} // namespace midiforge