#pragma once

// Corpus-derived aggregate targets and measurement helpers used only by headless QA.
// MIDI Forge's production/VST3 generation path does not include this header.

#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <utility>
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


// Pipeline diagnostics are enabled only around an explicit headless QA MAGIC call.
// The VST3 target never includes this test helper.
struct PipelineTraceContext
{
    std::string outputPath;
    std::string treatment;
    int seed = 0;
    bool enabled = false;
};

inline PipelineTraceContext& pipelineTraceContext()
{
    static PipelineTraceContext context;
    return context;
}

inline void setPipelineTraceContext (const juce::File& outputFile,
                                     const char* treatment, int seed)
{
    auto& context = pipelineTraceContext();
    context.outputPath = outputFile.getFullPathName().toStdString();
    context.treatment = treatment != nullptr ? treatment : "unknown";
    context.seed = seed;
    context.enabled = ! context.outputPath.empty();
}

inline void clearPipelineTraceContext()
{
    auto& context = pipelineTraceContext();
    context.enabled = false;
    context.outputPath.clear();
    context.treatment.clear();
    context.seed = 0;
}

struct PipelineTraceSample
{
    std::array<LayerSample, LayerCount> layers {};
    double quality = 0.0;
};

inline double quantile (std::vector<double> values, double probability)
{
    if (values.empty())
        return 0.0;
    std::sort (values.begin(), values.end());
    const double p = std::max (0.0, std::min (1.0, probability));
    const double position = p * (double) (values.size() - 1);
    const size_t low = (size_t) position;
    const size_t high = std::min (values.size() - 1, low + 1);
    const double fraction = position - (double) low;
    return values[low] + (values[high] - values[low]) * fraction;
}

inline bool appendPipelineTraceSummary (const char* stage,
                                        const std::vector<PipelineTraceSample>& samples)
{
    const auto& context = pipelineTraceContext();
    if (! context.enabled || context.outputPath.empty() || samples.empty())
        return true;

    std::vector<double> qualities;
    qualities.reserve (samples.size());
    for (const auto& sample : samples)
        qualities.push_back (sample.quality);

    const double qualityP10 = quantile (qualities, 0.10);
    const double qualityP50 = quantile (qualities, 0.50);
    const double qualityP90 = quantile (qualities, 0.90);
    std::ofstream csv (context.outputPath, std::ios::out | std::ios::app);
    if (! csv.is_open())
        return false;

    for (size_t layer = 0; layer < LayerCount; ++layer)
    {
        std::vector<double> noteCounts;
        noteCounts.reserve (samples.size());
        size_t eligibleCandidates = 0;
        std::array<std::vector<double>, MetricCount> metricValues;

        for (const auto& sample : samples)
        {
            const auto& layerSample = sample.layers[layer];
            noteCounts.push_back ((double) layerSample.noteCount);
            if (layerSample.noteCount >= 50)
                ++eligibleCandidates;
            for (size_t metric = 0; metric < MetricCount; ++metric)
                if (layerSample.valid[metric])
                    metricValues[metric].push_back (layerSample.value[metric]);
        }

        const double noteP10 = quantile (noteCounts, 0.10);
        const double noteP50 = quantile (noteCounts, 0.50);
        const double noteP90 = quantile (noteCounts, 0.90);

        for (size_t metric = 0; metric < MetricCount; ++metric)
        {
            const auto& values = metricValues[metric];
            csv << context.seed << ',' << context.treatment << ','
                << (stage != nullptr ? stage : "unknown") << ',' << samples.size() << ','
                << layerNames[layer] << ','
                << std::fixed << std::setprecision (6)
                << noteP10 << ',' << noteP50 << ',' << noteP90 << ','
                << eligibleCandidates << ',' << metricNames[metric] << ','
                << values.size() << ',';
            if (values.empty())
                csv << "NA,NA,NA";
            else
                csv << quantile (values, 0.10) << ','
                    << quantile (values, 0.50) << ','
                    << quantile (values, 0.90);
            csv << ',' << qualityP10 << ',' << qualityP50 << ',' << qualityP90 << '\n';
        }
    }
    return csv.good();
}

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

        // Split the ordered interval line into non-overlapping windows of six.
        // Ignore an incomplete tail, clip each interval to [-12, 12], then compute
        // sum(count(window) - 1) / number_of_windows.
        const size_t nWindows = intervals.size() / 6;
        if (nWindows > 0)
        {
            using Window = std::array<int, 6>;
            std::map<Window, int> counts;
            for (size_t windowIndex = 0; windowIndex < nWindows; ++windowIndex)
            {
                const size_t start = windowIndex * 6;
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
