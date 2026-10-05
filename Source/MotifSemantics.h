#pragma once

#include <cstdint>

namespace midiforge
{
struct MotifSemantics
{
    struct Plan
    {
        int rhythmicCore = 0;
        int intervalCore = 0;
        int startingAnchor = 0;
        int peakGesture = 0;
        int endingGesture = 0;
        int signatureLeap = 0;
        int answerCell = 0;

        int primaryMutation = 0;
        int secondaryMutation = 1;

        float mutationStrength = 0.55f;
        float contrastStrength = 0.62f;
        float returnStrength = 0.78f;
    };

    static Plan makePlan (int melodyType,
                          int mood,
                          float energy,
                          float complexity,
                          uint32_t identity);
};
} // namespace midiforge
