#pragma once

// Corpus-derived aggregate targets and measurement helpers used only by headless QA.
// MIDI Forge's production/VST3 generation path does not include this header.

#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace midiforge
{
namespace corpus
{
enum Layer : size_t { Top = 0, Riff = 1, Bass = 2, LayerCount = 3 };
enum Metric : size_t { StepsLe2 = 0, LeapsGe5 = 1, WindowRepeat = 2, DurationBeats = 3, NotesPerBar = 4, MetricCount = 5 };

static constexpr const char* layerNames[LayerCount] = { "top", "riff", "bass" };
static constexpr const char* metricNames[MetricCount] = {
    "steps_le2", "leaps_ge5", "window_repeat", "dur_beats", "notes_per_bar"
};

struct Target
{
    double median = 0.0;
    bool hasIqr = false;
    double iqrLow = 0.0;
    double iqrHigh = 0.0;
};

struct LayerSample
{
    std::array<double, MetricCount> value {};
    std::array<bool, MetricCount> valid {};
    int noteCount = 0;
};

inline double median (std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort (values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return (values.size() % 2) != 0
        ? values[middle]
        : (values[middle - 1] + values[middle]) * 0.5;
}

inline int layerForPitch (int midiPitch)
{
    if (midiPitch < 48) return (int) Bass;
    if (midiPitch < 60) return (int) Riff;
    return (int) Top;
}

// VisibleNote positions and lengths use sixteenth-note steps: 4 steps = 1 beat,
// 16 steps = one 4/4 bar. A step is already the nearest available tick.
inline std::array<LayerSample, LayerCount> measure (
    const std::vector<MidiForgeAudioProcessor::VisibleNote>& notes, int bars)
{
    using Note = MidiForgeAudioProcessor::VisibleNote;
    std::array<LayerSample, LayerCount> out {};
    std::array<std::vector<const Note*>, LayerCount> notesByLayer;
    std::array<std::map<int, int>, LayerCount> lineByLayer;

    bars = std::max (1, bars);
    for (const auto& note : notes)
    {
        // Internal channel 5 is the plugin's drum lane. Also exclude standard
        // one-based MIDI channel 10 / zero-based channel 9 if present in test data.
        if (note.channel == 5 || note.channel == 9 || note.channel == 10)
            continue;
        if (note.note < 0 || note.note > 127 || note.step < 0
            || note.step >= bars * 16 || note.length < 1)
            continue;

        const size_t layer = (size_t) layerForPitch (note.note);
        notesByLayer[layer].push_back (&note);
        auto& line = lineByLayer[layer];
        const auto found = line.find (note.step);
        if (found == line.end())
            line.emplace (note.step, note.note);
        else if (note.note > found->second)
            found->second = note.note;
    }

    for (size_t layer = 0; layer < LayerCount; ++layer)
    {
        auto& result = out[layer];
        result.noteCount = (int) notesByLayer[layer].size();
        if (notesByLayer[layer].empty())
            continue;

        std::vector<double> durations;
        durations.reserve (notesByLayer[layer].size());
        std::vector<double> notesPerBar ((size_t) bars, 0.0);
        for (const auto* note : notesByLayer[layer])
        {
            durations.push_back ((double) note->length / 4.0);
            const int bar = note->step / 16;
            if (bar >= 0 && bar < bars)
                notesPerBar[(size_t) bar] += 1.0;
        }
        result.value[DurationBeats] = median (std::move (durations));
        result.valid[DurationBeats] = true;
        result.value[NotesPerBar] = median (std::move (notesPerBar));
        result.valid[NotesPerBar] = true;

        std::vector<int> pitches;
        pitches.reserve (lineByLayer[layer].size());
        for (const auto& entry : lineByLayer[layer])
            pitches.push_back (entry.second);

        std::vector<int> intervals;
        intervals.reserve (pitches.size() > 0 ? pitches.size() - 1 : 0);
        int smallSteps = 0;
        int largeLeaps = 0;
        for (size_t i = 1; i < pitches.size(); ++i)
        {
            const int delta = pitches[i] - pitches[i - 1];
            intervals.push_back (delta);
            if (std::abs (delta) <= 2) ++smallSteps;
            if (std::abs (delta) >= 5) ++largeLeaps;
        }
        if (! intervals.empty())
        {
            result.value[StepsLe2] = (double) smallSteps / (double) intervals.size();
            result.value[LeapsGe5] = (double) largeLeaps / (double) intervals.size();
            result.valid[StepsLe2] = result.valid[LeapsGe5] = true;
        }

        // Use overlapping windows of six successive intervals. Clip each interval
        // to [-12, 12], then compute sum(count(window) - 1) / number_of_windows.
        if (intervals.size() >= 6)
        {
            using Window = std::array<int, 6>;
            std::map<Window, int> counts;
            const size_t nWindows = intervals.size() - 5;
            for (size_t start = 0; start < nWindows; ++start)
            {
                Window window {};
                for (size_t offset = 0; offset < window.size(); ++offset)
                    window[offset] = std::max (-12, std::min (12, intervals[start + offset]));
                ++counts[window];
            }

            size_t repeats = 0;
            for (const auto& entry : counts)
                if (entry.second > 1)
                    repeats += (size_t) (entry.second - 1);
            result.value[WindowRepeat] = (double) repeats / (double) nWindows;
            result.valid[WindowRepeat] = true;
        }
    }
    return out;
}

inline bool loadTargets (const juce::File& file,
                         std::array<std::array<Target, MetricCount>, LayerCount>& targets)
{
    if (! file.existsAsFile())
        return false;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject())
        return false;

    const auto layers = parsed.getProperty (juce::Identifier ("layers"), juce::var());
    if (! layers.isObject())
        return false;

    for (size_t layer = 0; layer < LayerCount; ++layer)
    {
        const auto layerObject = layers.getProperty (
            juce::Identifier (layerNames[layer]), juce::var());
        if (! layerObject.isObject())
            return false;

        const auto iqrObject = layerObject.getProperty (juce::Identifier ("iqr"), juce::var());
        for (size_t metric = 0; metric < MetricCount; ++metric)
        {
            auto& target = targets[layer][metric];
            const auto value = layerObject.getProperty (
                juce::Identifier (metricNames[metric]), juce::var());
            if (value.isVoid())
                return false;
            target.median = value.toString().getDoubleValue();

            // The supplied corpus summary has no IQR for dur_beats. Keep that
            // comparison explicitly unavailable rather than inventing bounds.
            const auto bounds = iqrObject.getProperty (
                juce::Identifier (metricNames[metric]), juce::var());
            const auto* array = bounds.getArray();
            if (array != nullptr && array->size() >= 2)
            {
                target.iqrLow = array->getReference (0).toString().getDoubleValue();
                target.iqrHigh = array->getReference (1).toString().getDoubleValue();
                target.hasIqr = target.iqrHigh > target.iqrLow;
            }
        }
    }
    return true;
}

} // namespace corpus
} // namespace midiforge
