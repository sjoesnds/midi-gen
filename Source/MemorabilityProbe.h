#pragma once

// Read-only diagnostics for finished MIDI. Included by the MIDIFORGE_HEADLESS
// QA target only; the production VST3 target does not include this header.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <vector>

namespace midiforge
{
namespace memorability
{
struct Note
{
    int step = 0;
    int length = 1;
    int pitch = 60;
    int channel = 3;
};

struct Metrics
{
    bool hookPresent = false;
    int hookCount = 0;
    int hookLengthNotes = 0;
    float hookSpreadBars = -1.0f;

    int returnCandidates = 0;
    int returnExactCount = 0;
    int returnVariedCount = 0;
    int returnRandomCount = 0;
    float returnExact = -1.0f;
    float returnVaried = -1.0f;
    float returnRandom = -1.0f;
    float returnScore = -1.0f;
    std::vector<int> returnDistances;

    float contrastDensity = 0.0f;
    float contrastRegister = 0.0f;
    bool contrastPresent = false;

    int closureMelodyLeap = -1;
    float closureMelodyLengthDelta = -1.0f;
    int closureMelodyFirstPitch = -1;
    int closureMelodyLastPitch = -1;
    bool closureMelodyTonicOrDominant = false;
    bool closureMelodyOk = false;

    int closureChordLeap = -1;
    float closureChordLengthDelta = -1.0f;
    int closureChordFirstPitch = -1;
    int closureChordLastPitch = -1;
    bool closureChordTonicOrDominant = false;
    bool closureChordOk = false;
};

inline int pitchClass (int pitch)
{
    return ((pitch % 12) + 12) % 12;
}

inline std::vector<Note> sortedLane (const std::vector<Note>& source, int channel)
{
    std::vector<Note> result;
    for (const auto& note : source)
        if (note.channel == channel && note.step >= 0 && note.length > 0)
            result.push_back (note);

    std::stable_sort (result.begin(), result.end(),
        [] (const Note& a, const Note& b)
        {
            if (a.step != b.step) return a.step < b.step;
            return a.pitch < b.pitch;
        });
    return result;
}

inline bool sameHook (const std::vector<Note>& notes, int first, int second, int length)
{
    for (int i = 1; i < length; ++i)
    {
        const int firstInterval = notes[(size_t) (first + i)].pitch
                                - notes[(size_t) (first + i - 1)].pitch;
        const int secondInterval = notes[(size_t) (second + i)].pitch
                                 - notes[(size_t) (second + i - 1)].pitch;
        if (firstInterval != secondInterval)
            return false;

        const int firstGap = notes[(size_t) (first + i)].step
                           - notes[(size_t) (first + i - 1)].step;
        const int secondGap = notes[(size_t) (second + i)].step
                            - notes[(size_t) (second + i - 1)].step;
        if (std::abs (firstGap - secondGap) > 2)
            return false;
    }

    for (int i = 0; i < length; ++i)
        if (std::abs (notes[(size_t) (first + i)].length
                    - notes[(size_t) (second + i)].length) > 1)
            return false;
    return true;
}

struct HookMatch
{
    int length = 0;
    int firstIndex = -1;
    std::vector<int> starts;
};

inline HookMatch findHook (const std::vector<Note>& melody)
{
    // Prefer the shortest repeated motif (3..6 notes); within that length,
    // prefer the earliest template with the most non-overlapping occurrences.
    for (int length = 3; length <= 6; ++length)
    {
        HookMatch best;
        for (int first = 0; first + length <= (int) melody.size(); ++first)
        {
            std::vector<int> starts { first };
            int cursor = first + length;
            while (cursor + length <= (int) melody.size())
            {
                if (sameHook (melody, first, cursor, length))
                {
                    starts.push_back (cursor);
                    cursor += length;
                }
                else
                    ++cursor;
            }

            if (starts.size() >= 2
                && ((int) starts.size() > (int) best.starts.size()
                    || (starts.size() == best.starts.size()
                        && (best.firstIndex < 0 || first < best.firstIndex))))
            {
                best.length = length;
                best.firstIndex = first;
                best.starts = std::move (starts);
            }
        }
        if (best.length > 0)
            return best;
    }
    return {};
}

inline std::vector<int> motifTokens (const std::vector<Note>& melody, int start, int length)
{
    // Transposition-safe pitch intervals plus onset gaps and durations.
    std::vector<int> tokens;
    tokens.reserve ((size_t) length * 3);
    for (int i = 0; i < length; ++i)
    {
        const auto& note = melody[(size_t) (start + i)];
        const int interval = i == 0 ? 0
            : note.pitch - melody[(size_t) (start + i - 1)].pitch;
        const int gap = i == 0 ? 0
            : note.step - melody[(size_t) (start + i - 1)].step;
        tokens.push_back (interval);
        tokens.push_back (gap);
        tokens.push_back (note.length);
    }
    return tokens;
}

inline int editDistance (const std::vector<int>& a, const std::vector<int>& b)
{
    std::vector<int> previous (b.size() + 1), current (b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        previous[j] = (int) j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        current[0] = (int) i;
        for (size_t j = 1; j <= b.size(); ++j)
        {
            const int substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min ({ previous[j] + 1, current[j - 1] + 1, substitution });
        }
        previous.swap (current);
    }
    return previous[b.size()];
}

struct ClosureEvent
{
    int step = 0;
    double pitch = 0.0;
    double length = 1.0;
    std::vector<int> pitches;
};

inline std::vector<ClosureEvent> closureEvents (const std::vector<Note>& source, int channel)
{
    auto notes = sortedLane (source, channel);
    std::vector<ClosureEvent> events;
    if (channel != 1)
    {
        events.reserve (notes.size());
        for (const auto& note : notes)
            events.push_back ({ note.step, (double) note.pitch, (double) note.length, { note.pitch } });
        return events;
    }

    // For chords, each shared onset is one event: compare centres at the seam
    // and test the final voicing for tonic/dominant material.
    for (size_t i = 0; i < notes.size();)
    {
        const int step = notes[i].step;
        double pitchSum = 0.0, lengthSum = 0.0;
        std::vector<int> pitches;
        size_t j = i;
        while (j < notes.size() && notes[j].step == step)
        {
            pitchSum += notes[j].pitch;
            lengthSum += notes[j].length;
            pitches.push_back (notes[j].pitch);
            ++j;
        }
        const double count = (double) (j - i);
        events.push_back ({ step, pitchSum / count, lengthSum / count, std::move (pitches) });
        i = j;
    }
    return events;
}

struct Closure
{
    int leap = -1;
    float lengthDelta = -1.0f;
    int firstPitch = -1;
    int lastPitch = -1;
    bool tonicOrDominant = false;
    bool ok = false;
};

inline Closure analyzeClosure (const std::vector<Note>& notes, int channel, int rootPitchClass)
{
    Closure result;
    const auto events = closureEvents (notes, channel);
    if (events.empty())
        return result;

    double lengthSum = 0.0;
    int lengthCount = 0;
    for (const auto& note : notes)
        if (note.channel == channel && note.step >= 0 && note.length > 0)
        {
            lengthSum += note.length;
            ++lengthCount;
        }
    if (lengthCount == 0)
        return result;

    const auto& first = events.front();
    const auto& last = events.back();
    result.firstPitch = (int) std::lround (first.pitch);
    result.lastPitch = (int) std::lround (last.pitch);
    result.leap = (int) std::lround (std::abs (last.pitch - first.pitch));
    result.lengthDelta = (float) (last.length - lengthSum / (double) lengthCount);

    bool tonicOrDominant = false;
    for (const int pitch : last.pitches)
    {
        const int relative = (pitchClass (pitch) - pitchClass (rootPitchClass) + 12) % 12;
        if (relative == 0 || relative == 7)
        {
            tonicOrDominant = true;
            break;
        }
    }
    result.tonicOrDominant = tonicOrDominant;
    result.ok = result.leap <= 2 && result.tonicOrDominant;
    return result;
}

inline void classifyReturns (Metrics& result, int threshold)
{
    result.returnCandidates = (int) result.returnDistances.size();
    result.returnExactCount = result.returnVariedCount = result.returnRandomCount = 0;
    result.returnExact = result.returnVaried = result.returnRandom = result.returnScore = -1.0f;
    if (result.returnCandidates == 0)
        return;

    threshold = std::max (1, threshold);
    for (const int distance : result.returnDistances)
    {
        if (distance == 0)
            ++result.returnExactCount;
        else if (distance <= threshold)
            ++result.returnVariedCount;
        else
            ++result.returnRandomCount;
    }
    const float denom = (float) result.returnCandidates;
    result.returnExact = (float) result.returnExactCount / denom;
    result.returnVaried = (float) result.returnVariedCount / denom;
    result.returnRandom = (float) result.returnRandomCount / denom;
    result.returnScore = result.returnVaried;
}

inline Metrics analyze (const std::vector<Note>& notes,
                        int bars,
                        int rootPitchClass,
                        int returnThreshold = 1,
                        float contrastDensityThreshold = 1.0f,
                        float contrastRegisterThreshold = 2.0f)
{
    Metrics result;
    const int safeBars = std::max (1, bars);
    const int loopSteps = safeBars * 16;
    const auto melody = sortedLane (notes, 3);
    const auto hook = findHook (melody);

    if (hook.length > 0 && hook.firstIndex >= 0)
    {
        result.hookCount = (int) hook.starts.size();
        result.hookLengthNotes = hook.length;
        const int firstStep = melody[(size_t) hook.starts.front()].step;
        const int lastStep = melody[(size_t) hook.starts.back()].step;
        result.hookSpreadBars = (float) (lastStep - firstStep) / 16.0f;
        result.hookPresent = result.hookCount >= 2 && firstStep < loopSteps / 2;

        const auto templateTokens = motifTokens (melody, hook.firstIndex, hook.length);
        // Non-overlapping windows after the first motif occurrence are potential
        // returns; distances distinguish exact, varied and unrelated material.
        for (int start = hook.firstIndex + hook.length;
             start + hook.length <= (int) melody.size();
             start += hook.length)
            result.returnDistances.push_back (
                editDistance (templateTokens, motifTokens (melody, start, hook.length)));
    }

    std::vector<int> barCounts ((size_t) safeBars, 0);
    std::vector<double> barPitchSums ((size_t) safeBars, 0.0);
    std::vector<int> barPitchCounts ((size_t) safeBars, 0);
    for (const auto& note : melody)
    {
        if (note.step < 0 || note.step >= loopSteps) continue;
        const int bar = note.step / 16;
        ++barCounts[(size_t) bar];
        barPitchSums[(size_t) bar] += note.pitch;
        ++barPitchCounts[(size_t) bar];
    }
    if (! barCounts.empty())
    {
        const auto mm = std::minmax_element (barCounts.begin(), barCounts.end());
        result.contrastDensity = (float) (*mm.second - *mm.first);
    }

    bool haveMean = false;
    double minMean = 0.0, maxMean = 0.0;
    for (size_t bar = 0; bar < barPitchCounts.size(); ++bar)
    {
        if (barPitchCounts[bar] == 0) continue;
        const double mean = barPitchSums[bar] / (double) barPitchCounts[bar];
        if (! haveMean) { minMean = maxMean = mean; haveMean = true; }
        else { minMean = std::min (minMean, mean); maxMean = std::max (maxMean, mean); }
    }
    result.contrastRegister = haveMean ? (float) (maxMean - minMean) : 0.0f;
    result.contrastPresent = result.contrastDensity > contrastDensityThreshold
                          && result.contrastRegister > contrastRegisterThreshold;

    const auto melodyClosure = analyzeClosure (notes, 3, rootPitchClass);
    result.closureMelodyLeap = melodyClosure.leap;
    result.closureMelodyLengthDelta = melodyClosure.lengthDelta;
    result.closureMelodyFirstPitch = melodyClosure.firstPitch;
    result.closureMelodyLastPitch = melodyClosure.lastPitch;
    result.closureMelodyTonicOrDominant = melodyClosure.tonicOrDominant;
    result.closureMelodyOk = melodyClosure.ok;
    const auto chordClosure = analyzeClosure (notes, 1, rootPitchClass);
    result.closureChordLeap = chordClosure.leap;
    result.closureChordLengthDelta = chordClosure.lengthDelta;
    result.closureChordFirstPitch = chordClosure.firstPitch;
    result.closureChordLastPitch = chordClosure.lastPitch;
    result.closureChordTonicOrDominant = chordClosure.tonicOrDominant;
    result.closureChordOk = chordClosure.ok;

    classifyReturns (result, returnThreshold);
    return result;
}
} // namespace memorability
} // namespace midiforge
