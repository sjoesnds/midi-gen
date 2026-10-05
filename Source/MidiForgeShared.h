#pragma once
#include <JuceHeader.h>
#include <cstdint>

inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline int creativeTextureFamily (uint32_t seed)
{
    return (int) (hash32 (seed ^ 0xC4E7A11Cu) % 8u);
}

inline constexpr int kStateMagic = 0x4D464752;
inline constexpr int kStateVersion = 3;

inline juce::File& settingsDirectoryOverride()
{
    static juce::File dir;
    return dir;
}

struct SoundProfile
{
    int laneShift;
    int laneCap;
    float legato;
    int minLen, maxLen;
    int velCenter;
    float velSpread;
    int minNotes, minHits;
    float densityMul;
    int maxLeap;
    int chordLen;
    bool chordTwoHits;
    float slideChance;
    float vibChance;
    bool bassOff;
    bool soloLine;
};

inline SoundProfile soundProfileFor (int id)
{
    switch (id)
    {
        case 1: return { 0, 100, 0.00f, 1, 2, 88, 0.45f, 5, 5, 1.10f, 12, 6, true, 0.00f, 0.0f, false, false };
        case 2: return { 2, 108, 0.94f, 2, 16, 96, 0.40f, 4, 5, 0.95f, 9, 16, false, 0.30f, 0.5f, false, false };
        case 3: return { 8, 112, 0.60f, 3, 6, 82, 0.60f, 3, 4, 0.72f, 12, 16, false, 0.00f, 0.0f, false, false };
        case 4: return { -4, 94, 1.00f, 4, 16, 76, 0.30f, 2, 3, 0.50f, 5, 16, false, 0.00f, 0.0f, false, false };
        case 5: return { -2, 102, 0.55f, 2, 6, 98, 0.70f, 4, 5, 0.90f, 7, 5, true, 0.00f, 0.0f, false, false };
        case 6: return { -30, 60, 0.85f, 2, 16, 100, 0.30f, 2, 3, 0.55f, 7, 16, false, 0.55f, 0.0f, true, true };
        case 7: return { -8, 96, 0.35f, 1, 6, 86, 0.70f, 4, 5, 1.00f, 9, 8, true, 0.20f, 0.0f, false, false };
        default: return { 0, 104, -1.00f, 1, 16, 80, 1.00f, 4, 5, 1.00f, 12, 16, false, 0.00f, 0.0f, false, false };
    }
}
