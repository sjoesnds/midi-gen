#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace midiforge
{
class RhythmGrammar
{
public:
    struct Note
    {
        int step = 0;
        int length = 1;
        int velocity = 80;
    };

    enum class Intent
    {
        Anchor,
        Build,
        Contrast,
        Release
    };

    enum class Shape
    {
        Straight,
        Syncopated,
        Pickup,
        Anticipation,
        CallResponse,
        SilenceGap,
        Repetitive,
        Burst
    };

    static void apply (std::vector<Note>& melody,
                       int bars,
                       double bpm,
                       int baseRhythm,
                       float complexity,
                       float energy,
                       uint32_t seed,
                       int melodyType);

    static float score (const std::vector<Note>& melody,
                        int bars,
                        double bpm,
                        int baseRhythm,
                        float complexity,
                        float energy);

    static Intent intentForBar (int barInPhrase)
    {
        switch (barInPhrase & 3)
        {
            case 0: return Intent::Anchor;
            case 1: return Intent::Build;
            case 2: return Intent::Contrast;
            default: return Intent::Release;
        }
    }

    static const char* intentName (Intent intent)
    {
        switch (intent)
        {
            case Intent::Anchor:   return "ANCHOR";
            case Intent::Build:    return "BUILD";
            case Intent::Contrast: return "CONTRAST";
            default:               return "RELEASE";
        }
    }

private:
    static uint32_t hash32 (uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static float similarity (const std::vector<int>& a, const std::vector<int>& b)
    {
        if (a.empty() || b.empty())
            return 0.0f;

        int hits = 0;
        for (const int x : a)
        {
            int best = 99;
            for (const int y : b)
                best = std::min (best, std::abs (x - y));
            if (best <= 1)
                ++hits;
        }

        const float hitFit = (float) hits / (float) std::max (a.size(), b.size());
        const float countFit = 1.0f
            - std::min (1.0f, (float) std::abs ((int) a.size() - (int) b.size()) / 4.0f);
        return std::clamp (0.72f * hitFit + 0.28f * countFit, 0.0f, 1.0f);
    }

    static float positionScore (int step,
                                Shape shape,
                                double bpm,
                                int baseRhythm,
                                float complexity,
                                float energy,
                                uint32_t salt)
    {
        const bool downbeat = (step % 4) == 0;
        const bool backbeat = (step % 8) == 4;
        const bool offbeat = !downbeat;
        const bool sixteenth = (step & 1) != 0;
        const bool fast = bpm >= 170.0;
        const bool slow = bpm <= 95.0;

        float s = 0.0f;
        switch (shape)
        {
            case Shape::Straight:
                s += downbeat ? 1.25f : (backbeat ? 0.82f : 0.46f);
                break;
            case Shape::Syncopated:
                s += offbeat ? 0.95f : 0.55f;
                if (step == 3 || step == 6 || step == 11 || step == 14) s += 0.55f;
                break;
            case Shape::Pickup:
                s += (step >= 12) ? 0.92f : (downbeat ? 0.96f : 0.38f);
                if (step >= 14) s += 0.48f;
                break;
            case Shape::Anticipation:
                s += (step == 7 || step == 15) ? 1.32f : (step % 4 == 3 ? 0.82f : 0.40f);
                if (downbeat) s += 0.30f;
                break;
            case Shape::CallResponse:
                s += ((step & 3) == 0 || step == 6 || step == 10 || step == 14) ? 1.0f : 0.36f;
                break;
            case Shape::SilenceGap:
                s += (step == 0 || step == 4 || step >= 12) ? 1.05f : 0.28f;
                if (step == 8 || step == 9) s -= 0.20f;
                break;
            case Shape::Repetitive:
                s += (step % 4 == 0) ? 1.18f : ((step % 4 == 2) ? 0.72f : 0.28f);
                break;
            case Shape::Burst:
                s += sixteenth ? 0.84f : 0.66f;
                if (step == 0 || step == 8) s += 0.38f;
                break;
        }

        switch (baseRhythm)
        {
            case 1: if (offbeat) s += 0.26f; break; // Syncopated
            case 2: if (step == 3 || step == 6 || step == 10 || step == 13) s += 0.22f; break; // Broken
            case 3: if (((step * 7) % 16) < 7) s += 0.17f; break; // Euclidean
            default: break;
        }

        if (fast && sixteenth) s += 0.28f + 0.22f * complexity;
        if (fast && offbeat) s += 0.10f * energy;
        if (slow && sixteenth) s -= 0.18f;
        if (slow && downbeat) s += 0.10f;
        s += 0.06f * ((float) ((hash32 (salt ^ (uint32_t) (step + 1)) % 100u)) / 100.0f - 0.5f);
        return s;
    }

    static std::vector<int> makePositions (int count,
                                            Shape shape,
                                            double bpm,
                                            int baseRhythm,
                                            float complexity,
                                            float energy,
                                            uint32_t seed)
    {
        count = std::max (1, std::min (12, count));
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve (16);

        for (int step = 0; step < 16; ++step)
            ranked.push_back ({
                positionScore (step, shape, bpm, baseRhythm, complexity, energy,
                               hash32 (seed ^ 0xA511E9B3u)),
                step
            });

        std::stable_sort (ranked.begin(), ranked.end(),
            [] (const auto& a, const auto& b)
            {
                if (std::abs (a.first - b.first) > 0.0001f)
                    return a.first > b.first;
                return a.second < b.second;
            });

        std::vector<int> positions;
        positions.reserve ((size_t) count);

        auto addPreferred = [&] (int step)
        {
            if (step < 0 || step >= 16)
                return;
            if (std::find (positions.begin(), positions.end(), step) == positions.end())
                positions.push_back (step);
        };

        if (shape == Shape::Pickup || shape == Shape::Anticipation)
        {
            addPreferred (0);
            addPreferred (8);
            addPreferred (shape == Shape::Pickup ? 14 : 15);
        }
        else if (shape == Shape::Repetitive)
        {
            addPreferred (0);
            addPreferred (4);
            addPreferred (8);
            addPreferred (12);
        }

        for (const auto& item : ranked)
        {
            if ((int) positions.size() >= count)
                break;
            addPreferred (item.second);
        }

        std::sort (positions.begin(), positions.end());
        return positions;
    }

    static int nearestFree (int target, const std::vector<int>& occupied)
    {
        target = std::clamp (target, 0, 15);
        if (std::find (occupied.begin(), occupied.end(), target) == occupied.end())
            return target;

        for (int d = 1; d <= 15; ++d)
        {
            const int a = target - d;
            const int b = target + d;
            if (a >= 0 && std::find (occupied.begin(), occupied.end(), a) == occupied.end()) return a;
            if (b < 16 && std::find (occupied.begin(), occupied.end(), b) == occupied.end()) return b;
        }
        return target;
    }

    static Shape chooseShape (Intent intent,
                              int baseRhythm,
                              float complexity,
                              float energy,
                              double bpm,
                              uint32_t seed,
                              int melodyType,
    {
        const uint32_t roll = hash32 (seed ^ 0x5F3759DFu) % 100u;
        const bool fast = bpm >= 170.0;
        const bool sparse = melodyType == 6; // Sparse Lead

        if (intent == Intent::Anchor)
        {
            if (roll < 44u) return Shape::Repetitive;
            return baseRhythm == 1 ? Shape::Straight : Shape::Repetitive;
        }

        if (intent == Intent::Build)
        {
            if (roll < (fast ? 50u : 34u)) return Shape::Syncopated;
            if (complexity > 0.62f) return Shape::Burst;
            return Shape::Anticipation;
        }

        if (intent == Intent::Contrast)
        {
            if (sparse && roll < 55u) return Shape::SilenceGap;
            if (roll < 36u) return Shape::CallResponse;
            if (complexity > 0.55f || energy > 0.70f) return Shape::Syncopated;
            return Shape::SilenceGap;
        }

        if (roll < 46u) return Shape::Pickup;
        if (roll < 72u) return Shape::Anticipation;
        return Shape::Repetitive;
    }

    static float blendOnset (int anchor, int target, float targetWeight)
    {
        return (1.0f - targetWeight) * (float) anchor + targetWeight * (float) target;
    }
};
}
