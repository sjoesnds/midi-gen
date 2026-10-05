#include "MotifSemantics.h"

#include <algorithm>

namespace
{
uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int pick (uint32_t seed, int count)
{
    return count > 0 ? (int) (mix32 (seed) % (uint32_t) count) : 0;
}

float unit (uint32_t seed)
{
    return (float) (mix32 (seed) % 1000u) / 999.0f;
}
}

namespace midiforge
{
MotifSemantics::Plan MotifSemantics::makePlan (int melodyType,
                                               int mood,
                                               int genre,
                                               float energy,
                                               float complexity,
                                               uint32_t identity)
{
    Plan p;

    const uint32_t base = mix32 (
        identity
        ^ (uint32_t) (melodyType + 1) * 0x9e3779b9u
        ^ (uint32_t) (mood + 11) * 0x85ebca6bu
        ^ (uint32_t) (genre + 17) * 0xc2b2ae35u);

    p.rhythmicCore = pick (base ^ 0x13579bdu, 8);
    p.intervalCore = pick (base ^ 0x2468aceu, 8);
    p.startingAnchor = pick (base ^ 0x1020304u, 7);
    p.peakGesture = pick (base ^ 0x55667788u, 7);
    p.endingGesture = pick (base ^ 0x90abcdefu, 7);
    p.signatureLeap = pick (base ^ 0x31415926u, 6);
    p.answerCell = pick (base ^ 0x27182818u, 7);

    // The same seven semantic parts are intentionally allowed to combine
    // freely. Only the primary/secondary mutation roles are constrained so
    // A' changes one idea and B changes another.
    p.primaryMutation = pick (base ^ 0xdeadbeefu, 7);
    p.secondaryMutation = (p.primaryMutation + 1 + pick (base ^ 0xabcdef01u, 6)) % 7;

    const float energyBias = std::clamp (energy, 0.0f, 1.0f);
    const float complexityBias = std::clamp (complexity, 0.0f, 1.0f);

    p.mutationStrength = 0.38f
        + 0.24f * complexityBias
        + 0.12f * unit (base ^ 0x11u);

    p.contrastStrength = 0.50f
        + 0.18f * energyBias
        + 0.12f * complexityBias
        + 0.10f * unit (base ^ 0x22u);

    p.returnStrength = 0.68f
        + 0.16f * (1.0f - complexityBias)
        + 0.08f * unit (base ^ 0x33u);

    // 0.86.x Context Language: mood selects a coordinated semantic
    // vocabulary. The fields below are deliberately discrete so mood changes
    // can alter the actual phrase grammar, not only its scalar scoring.
    switch (mood)
    {
        case 1: // Dark
            p.rhythmicCore = (p.rhythmicCore + 1) % 8;
            p.intervalCore = (p.intervalCore + 3) % 8;
            p.signatureLeap = (p.signatureLeap + 2) % 6;
            p.endingGesture = (p.endingGesture + 2) % 7;
            p.returnStrength = std::max (0.62f, p.returnStrength - 0.08f);
            break;
        case 2: // Melancholic
            p.intervalCore = (p.intervalCore + 5) % 8;
            p.startingAnchor = (p.startingAnchor + 3) % 7;
            p.endingGesture = (p.endingGesture + 1) % 7;
            p.answerCell = (p.answerCell + 4) % 7;
            p.mutationStrength *= 0.90f;
            p.returnStrength = std::min (0.92f, p.returnStrength + 0.06f);
            break;
        case 3: // Euphoric
            p.rhythmicCore = (p.rhythmicCore + 2) % 8;
            p.peakGesture = (p.peakGesture + 2) % 7;
            p.startingAnchor = (p.startingAnchor + 1) % 7;
            p.endingGesture = (p.endingGesture + 4) % 7;
            p.contrastStrength = std::min (0.94f, p.contrastStrength + 0.08f);
            break;
        case 4: // Aggressive
            p.rhythmicCore = (p.rhythmicCore + 5) % 8;
            p.intervalCore = (p.intervalCore + 4) % 8;
            p.signatureLeap = (p.signatureLeap + 3) % 6;
            p.peakGesture = (p.peakGesture + 4) % 7;
            p.answerCell = (p.answerCell + 3) % 7;
            p.mutationStrength = std::min (0.92f, p.mutationStrength + 0.10f);
            p.contrastStrength = std::min (0.96f, p.contrastStrength + 0.10f);
            break;
        case 5: // Dreamy
            p.rhythmicCore = (p.rhythmicCore + 6) % 8;
            p.intervalCore = (p.intervalCore + 1) % 8;
            p.peakGesture = (p.peakGesture + 5) % 7;
            p.endingGesture = (p.endingGesture + 5) % 7;
            p.returnStrength = std::min (0.95f, p.returnStrength + 0.05f);
            break;
        case 6: // Nostalgic
            p.rhythmicCore = (p.rhythmicCore + 3) % 8;
            p.startingAnchor = (p.startingAnchor + 2) % 7;
            p.peakGesture = (p.peakGesture + 1) % 7;
            p.answerCell = (p.answerCell + 6) % 7;
            p.returnStrength = std::min (0.94f, p.returnStrength + 0.08f);
            break;
        case 7: // Mysterious
            p.intervalCore = (p.intervalCore + 5) % 8;
            p.signatureLeap = (p.signatureLeap + 2) % 6;
            p.endingGesture = (p.endingGesture + 3) % 7;
            p.answerCell = (p.answerCell + 1) % 7;
            p.contrastStrength = std::min (0.90f, p.contrastStrength + 0.06f);
            break;
        case 8: // Energetic
            p.rhythmicCore = (p.rhythmicCore + 4) % 8;
            p.peakGesture = (p.peakGesture + 2) % 7;
            p.signatureLeap = (p.signatureLeap + 2) % 6;
            p.answerCell = (p.answerCell + 2) % 7;
            p.mutationStrength = std::min (0.94f, p.mutationStrength + 0.06f);
            p.contrastStrength = std::min (0.94f, p.contrastStrength + 0.08f);
            break;
        default:
            break;
    }

    if (melodyType == 1 || melodyType == 6) // vocal-like / sparse lead
        p.answerCell = (p.answerCell + 2) % 7;
    if (genre == 14) // experimental
        p.primaryMutation = (p.primaryMutation + 3) % 7;

    return p;
}
} // namespace midiforge
