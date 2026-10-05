#pragma once

#include <cstdint>

namespace midiforge
{
struct CreativeRange
{
    struct Plan
    {
        int contourFamily = 0;
        int intervalFamily = 0;
        int rhythmFamily = 0;
        int repetitionStyle = 0;
        int registerJourney = 0;
        int harmonyPersonality = 0;
        int durationStyle = 0;

        float novelty = 0.50f;
        float asymmetry = 0.50f;
        float leapBias = 0.30f;
        float repetition = 0.55f;
        float registerBias = 0.0f;
        float harmonyColor = 0.50f;
        float durationContrast = 0.50f;
        int rhythmBias = 0;
    };

    static Plan makePlan (int melodyType,
                          int mood,
                          float energy,
                          float complexity,
                          uint32_t identity);
};
} // namespace midiforge
