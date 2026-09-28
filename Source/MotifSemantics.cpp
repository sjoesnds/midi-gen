#include "MotifSemantics.h"

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

    const float energyBias = juce::jlimit (0.0f, 1.0f, energy);
    const float complexityBias = juce::jlimit (0.0f, 1.0f, complexity);

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

    // Soft context biases. These do not lock the result to a genre or mood;
    // they only nudge the semantic vocabulary toward useful musical behavior.
    if (mood == 1 || mood == 7) // dark / mysterious
        p.signatureLeap = (p.signatureLeap + 2) % 6;
    if (mood == 2 || mood == 5) // melancholic / dreamy
        p.endingGesture = (p.endingGesture + 1) % 7;
    if (mood == 3 || mood == 8) // euphoric / energetic
        p.peakGesture = (p.peakGesture + 2) % 7;
    if (melodyType == 1 || melodyType == 6) // vocal-like / sparse lead
        p.answerCell = (p.answerCell + 2) % 7;
    if (genre == 14) // experimental
        p.primaryMutation = (p.primaryMutation + 3) % 7;

    return p;
}
} // namespace midiforge
