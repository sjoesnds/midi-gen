#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace midiforge
{

// 0.76 Variation Intelligence:
// matches each discovered musical idea to the transformation where its
// existing character is most useful. The assignment is one-to-one so the
// final bank stays intentionally varied instead of applying arbitrary
// transformations to arbitrary source loops.
enum class VariationMode : int
{
    Original = 0,
    Tight = 1,
    Sparse = 2,
    Dark = 3,
    Bigger = 4,
    Weird = 5,
    TightWeird = 6,
    SparseDark = 7
};

struct VariationTraits
{
    float density = 0.5f;
    float space = 0.5f;
    float rhythm = 0.5f;
    float motif = 0.5f;
    float leap = 0.3f;
    float reg = 0.5f;
    float surprise = 0.3f;
    float context = 0.5f;
    float loop = 0.5f;
    float groove = 0.5f;
    float memory = 0.5f;
    float phraseArc = 0.5f;
    float tension = 0.5f;
    float development = 0.5f;
};

class VariationIntelligence
{
public:
    static float fit (int mode, const VariationTraits& t)
    {
        const auto inv = [] (float v) { return 1.0f - clamp (v); };
        const auto target = [] (float value, float wanted, float width)
        {
            return 1.0f - clamp (std::abs (value - wanted) / std::max (0.01f, width));
        };

        float score = 0.5f;

        switch (mode)
        {
            case (int) VariationMode::Original:
                score = 0.30f * t.loop
                      + 0.22f * t.memory
                      + 0.18f * t.motif
                      + 0.14f * t.phraseArc
                      + 0.10f * t.development
                      + 0.06f * target (t.density, 0.58f, 0.42f);
                break;

            case (int) VariationMode::Tight:
                score = 0.28f * t.groove
                      + 0.22f * t.rhythm
                      + 0.18f * t.density
                      + 0.12f * inv (t.space)
                      + 0.10f * t.loop
                      + 0.10f * t.phraseArc;
                break;

            case (int) VariationMode::Sparse:
                score = 0.34f * t.space
                      + 0.28f * inv (t.density)
                      + 0.14f * t.context
                      + 0.10f * t.motif
                      + 0.08f * t.loop
                      + 0.06f * inv (t.groove);
                break;

            case (int) VariationMode::Dark:
                score = 0.30f * inv (t.reg)
                      + 0.22f * t.tension
                      + 0.18f * inv (t.density)
                      + 0.10f * t.surprise
                      + 0.10f * t.memory
                      + 0.10f * t.context;
                break;

            case (int) VariationMode::Bigger:
                score = 0.24f * t.reg
                      + 0.22f * t.leap
                      + 0.20f * t.density
                      + 0.14f * t.phraseArc
                      + 0.10f * t.tension
                      + 0.10f * t.motif;
                break;

            case (int) VariationMode::Weird:
                score = 0.34f * t.surprise
                      + 0.22f * t.leap
                      + 0.16f * inv (t.motif)
                      + 0.12f * t.context
                      + 0.10f * t.tension
                      + 0.06f * inv (t.memory);
                break;

            case (int) VariationMode::TightWeird:
                score = 0.20f * t.surprise
                      + 0.18f * t.groove
                      + 0.18f * t.rhythm
                      + 0.14f * t.leap
                      + 0.12f * t.density
                      + 0.10f * t.loop
                      + 0.08f * t.tension;
                break;

            case (int) VariationMode::SparseDark:
                score = 0.30f * t.space
                      + 0.24f * inv (t.density)
                      + 0.20f * inv (t.reg)
                      + 0.10f * t.tension
                      + 0.09f * t.context
                      + 0.07f * t.memory;
                break;

            default:
                break;
        }

        return clamp (score);
    }

    // Returns candidate-index per transformation mode. A small dynamic program
    // finds the global best one-to-one assignment instead of greedily picking
    // the locally best source for every mode.
    static std::array<int, 8> assign (const std::vector<VariationTraits>& traits)
    {
        std::array<int, 8> result {};
        result.fill (-1);

        const int count = std::min (8, (int) traits.size());
        if (count <= 0)
            return result;

        std::array<float, 256> dp {};
        std::array<int, 256> parentMask {};
        std::array<int, 256> parentCandidate {};
        for (auto& v : dp) v = -1.0e9f;
        parentMask.fill (-1);
        parentCandidate.fill (-1);
        dp[0] = 0.0f;

        for (int mask = 0; mask < 256; ++mask)
        {
            if (dp[(size_t) mask] < -1.0e8f) continue;
            const int mode = popcount (mask);
            if (mode >= 8 || mode >= count) continue;

            for (int candidate = 0; candidate < count; ++candidate)
            {
                const int bit = 1 << candidate;
                if ((mask & bit) != 0) continue;

                const int nextMask = mask | bit;
                const float value = dp[(size_t) mask]
                                  + fit (mode, traits[(size_t) candidate]);

                if (value > dp[(size_t) nextMask])
                {
                    dp[(size_t) nextMask] = value;
                    parentMask[(size_t) nextMask] = mask;
                    parentCandidate[(size_t) nextMask] = candidate;
                }
            }
        }

        int finalMask = 0;
        float best = -1.0e9f;
        for (int mask = 0; mask < 256; ++mask)
        {
            if (popcount (mask) != count) continue;
            if (dp[(size_t) mask] > best)
            {
                best = dp[(size_t) mask];
                finalMask = mask;
            }
        }

        for (int mode = count - 1; mode >= 0; --mode)
        {
            result[(size_t) mode] = parentCandidate[(size_t) finalMask];
            finalMask = parentMask[(size_t) finalMask];
        }

        return result;
    }

private:
    static float clamp (float v)
    {
        return std::max (0.0f, std::min (1.0f, v));
    }

    static int popcount (int value)
    {
        int count = 0;
        while (value != 0)
        {
            value &= value - 1;
            ++count;
        }
        return count;
    }
};

} // namespace midiforge
