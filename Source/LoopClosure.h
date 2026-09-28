#pragma once

#include <cstdint>

namespace midiforge
{
struct LoopClosure
{
    struct Plan
    {
        int bridgeStyle = 0;
        int targetStrategy = 0;
        int pickupStyle = 0;
        int releaseStyle = 0;

        float returnStrength = 0.72f;
        float boundaryDistance = 0.42f;
        float pickupBias = 0.34f;
        float sustainBias = 0.42f;
        float unresolvedBias = 0.22f;
    };

    static Plan makePlan (int melodyType,
                          int mood,
                          int genre,
                          float energy,
                          float complexity,
                          uint32_t identity);
};
} // namespace midiforge
