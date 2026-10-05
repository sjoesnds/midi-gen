#include "MelodyDecision.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
static uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float unit (uint32_t x)
{
    return (float) (x % 1001u) / 1000.0f;
}

static float fit (float actual, float target, float tolerance)
{
    return 1.0f - std::clamp (std::abs (actual - target)
                               / std::max (0.08f, tolerance), 0.0f, 1.0f);
}
}

namespace midiforge
{
MelodyDecision::ComplexityClass MelodyDecision::classify (
    float complexity,
    float simpleProbability,
    float complexProbability,
    uint32_t identity)
{
    const float c = std::clamp (complexity, 0.0f, 1.0f);
    const float simple = std::clamp (simpleProbability, 0.18f, 0.74f);
    const float complex = std::clamp (complexProbability, 0.08f, 0.40f);
    const float roll = unit (mix32 (identity ^ 0xD1E5A9B3u));

    // Complexity still acts as the main control, while the intent probabilities
    // make the output population broad enough that low-density phrases survive.
    const float simpleCut = std::clamp (simple + 0.18f * (0.55f - c), 0.14f, 0.76f);
    const float complexCut = std::clamp (simpleCut + complex + 0.14f * (c - 0.55f), simpleCut + 0.05f, 0.94f);

    if (roll < simpleCut)
        return Simple;
    if (roll < complexCut)
        return Complex;
    return Medium;
}

MelodyDecision::Evaluation MelodyDecision::evaluate (
    const std::vector<NoteView>& notes,
    int bars,
    ComplexityClass complexityClass,
    float preferredRegisterCenter,
    int preferredMaxLeap)
{
    Evaluation out;
    if (notes.empty() || bars <= 0)
        return out;

    std::vector<NoteView> ordered = notes;
    std::stable_sort (ordered.begin(), ordered.end(),
        [] (const NoteView& a, const NoteView& b)
        {
            if (a.step != b.step) return a.step < b.step;
            return a.note < b.note;
        });

    const int safeBars = std::max (1, bars);
    const float notesPerBar = (float) ordered.size() / (float) safeBars;
    const float density = std::clamp ((notesPerBar - 1.5f) / 7.0f, 0.0f, 1.0f);

    std::set<int> intervalMagnitudeSet;
    std::set<int> rhythmClassSet;
    int stepwise = 0;
    int samePitch = 0;
    int strongCount = 0;
    int strongChord = 0;
    int longCount = 0;
    float registerMean = 0.0f;

    int previous = ordered.front().note;
    int maxLeap = 0;

    std::vector<std::vector<int>> barOnsets ((size_t) safeBars);

    for (const auto& n : ordered)
    {
        registerMean += (float) n.note;
        rhythmClassSet.insert (n.step % 8);

        const int bar = std::clamp (n.step / 16, 0, safeBars - 1);
        barOnsets[(size_t) bar].push_back (n.step % 16);

        const bool strong = (n.step % 4) == 0 || n.length >= 3;
        if (strong)
        {
            ++strongCount;
            if (n.chordTone)
                ++strongChord;
        }
        if (n.length >= 3)
            ++longCount;

        if (n.note == previous)
            ++samePitch;

        if (&n != &ordered.front())
        {
            const int delta = n.note - previous;
            const int absDelta = std::abs (delta);
            intervalMagnitudeSet.insert (std::min (24, absDelta));
            maxLeap = std::max (maxLeap, absDelta);
        }

        previous = n.note;
    }

    registerMean /= (float) ordered.size();
    const float centreTolerance = 18.0f;
    const float registerFit = fit (registerMean, preferredRegisterCenter, centreTolerance);

    float stepwiseRatio = 0.0f;
    int intervalCount = 0;
    if (ordered.size() >= 2)
    {
        stepwise = 0;
        for (size_t i = 1; i < ordered.size(); ++i)
        {
            const int d = std::abs (ordered[i].note - ordered[i - 1].note);
            maxLeap = std::max (maxLeap, d);
            if (d > 0 && d <= 7) ++stepwise;
            if (d > 0) ++intervalCount;
        }
        stepwiseRatio = intervalCount > 0
            ? (float) stepwise / (float) intervalCount
            : 0.0f;
    }

    const float rhythmVariety = std::clamp ((float) rhythmClassSet.size() / 5.0f, 0.0f, 1.0f);
    const float intervalVariety = std::clamp ((float) intervalMagnitudeSet.size() / 7.0f, 0.0f, 1.0f);
    const float repeatRatio = std::clamp (
        (float) samePitch / (float) std::max<size_t> (1, ordered.size() - 1), 0.0f, 1.0f);
    const float longRatio = (float) longCount / (float) ordered.size();
    const float strongChordRatio = strongCount > 0
        ? (float) strongChord / (float) strongCount : 0.65f;

    float targetDensity = 0.48f;
    float targetIntervals = 0.45f;
    float targetRhythm = 0.48f;
    float targetRepeats = 0.46f;

    if (complexityClass == Simple)
    {
        targetDensity = 0.22f;
        targetIntervals = 0.24f;
        targetRhythm = 0.26f;
        targetRepeats = 0.68f;
    }
    else if (complexityClass == Complex)
    {
        targetDensity = 0.70f;
        targetIntervals = 0.68f;
        targetRhythm = 0.68f;
        targetRepeats = 0.24f;
    }

    out.complexityFit =
          0.34f * fit (density, targetDensity, 0.33f)
        + 0.23f * fit (intervalVariety, targetIntervals, 0.38f)
        + 0.20f * fit (rhythmVariety, targetRhythm, 0.38f)
        + 0.13f * fit (repeatRatio, targetRepeats, 0.42f)
        + 0.10f * fit (longRatio, complexityClass == Simple ? 0.30f
                           : complexityClass == Complex ? 0.22f : 0.26f, 0.34f);

    // Strong positions carry more harmonic responsibility than weak positions.
    // Weak notes are allowed to be colorful; the line should still feel anchored.
    out.structuralFit =
          0.42f * strongChordRatio
        + 0.22f * registerFit
        + 0.16f * fit (stepwiseRatio, complexityClass == Complex ? 0.52f : 0.66f, 0.38f)
        + 0.20f * fit ((float) std::min (24, maxLeap),
                       complexityClass == Complex ? 10.0f : 7.0f, 4.5f);

    // Genericity = repeated bars with nearly identical onset skeletons and
    // directional motion. Simple phrases are allowed to repeat more.
    int comparableBars = 0;
    int duplicateBars = 0;
    for (int a = 0; a < safeBars; ++a)
    {
        if (barOnsets[(size_t) a].empty()) continue;
        for (int b = a + 1; b < safeBars; ++b)
        {
            if (barOnsets[(size_t) b].empty()) continue;
            ++comparableBars;
            if (barOnsets[(size_t) a] == barOnsets[(size_t) b])
            {
                ++duplicateBars;
            }
        }
    }

    out.genericity = comparableBars > 0
        ? (float) duplicateBars / (float) comparableBars
        : 0.0f;

    const float repetitionAllowance = complexityClass == Simple ? 0.62f : 0.36f;
    const float genericityPenalty = std::clamp (
        (out.genericity - repetitionAllowance) / 0.38f, 0.0f, 1.0f);

    const float repeatedPitchPenalty = complexityClass == Simple
        ? std::clamp ((repeatRatio - 0.82f) / 0.18f, 0.0f, 1.0f)
        : std::clamp ((repeatRatio - 0.58f) / 0.24f, 0.0f, 1.0f);

    out.memorability = std::clamp (
          0.35f * fit (repeatRatio, complexityClass == Simple ? 0.58f : 0.34f, 0.44f)
        + 0.28f * rhythmVariety
        + 0.22f * fit (strongChordRatio, 0.70f, 0.38f)
        + 0.15f * fit (registerFit, 0.78f, 0.32f),
        0.0f, 1.0f);

    out.score = std::clamp (
          0.40f * out.complexityFit
        + 0.34f * out.structuralFit
        + 0.17f * out.memorability
        - 0.15f * genericityPenalty
        - 0.09f * repeatedPitchPenalty,
        0.0f, 1.0f);

    return out;
}
} // namespace midiforge
