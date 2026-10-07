#pragma once

#include <cstdint>
#include <vector>

namespace midiforge
{
struct MelodyDecision
{
    enum ComplexityClass
    {
        Simple = 0,
        Medium = 1,
        Complex = 2
    };

    struct NoteView
    {
        int step = 0;
        int length = 1;
        int note = 60;
        bool chordTone = false;
    };

    struct Evaluation
    {
        float complexityFit = 0.5f;
        float structuralFit = 0.5f;
        float genericity = 0.0f;
        float memorability = 0.5f;
        float phraseIntegrity = 0.5f;
        float cadenceFit = 0.5f;
        float contourFit = 0.5f;
        float score = 0.5f;
    };

    static ComplexityClass classify (float complexity, float simpleProbability,
                                      float complexProbability, uint32_t identity);

    static Evaluation evaluate (const std::vector<NoteView>& notes,
                                int bars,
                                ComplexityClass complexityClass,
                                float preferredRegisterCenter,
                                int preferredMaxLeap);
};
} // namespace midiforge
