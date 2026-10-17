#pragma once

// Corpus-derived aggregate targets and measurement helpers used only by headless QA.
// MIDI Forge's production/VST3 generation path does not include this header.

#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

enum RepeatProbe : size_t
{
    Repeat6SlidingClipped = 0,
    Repeat4NonOverlapClipped = 1,
    Repeat4SlidingClipped = 2,
    Repeat6NonOverlapRaw = 3,
    RepeatProbeCount = 4
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
    std::array<double, RepeatProbeCount> repeatProbeValue {};
    std::array<bool, RepeatProbeCount> repeatProbeValid {};
    int noteCount = 0;
};


// Pipeline diagnostics are enabled only around an explicit headless QA MAGIC call.
// The VST3 target never includes this test helper.
struct PipelineTraceContext
{
    std::string outputPath;
    std::string candidateOutputPath;
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
    juce::File pipelineFile (context.outputPath);
    auto candidateStem = pipelineFile.getFileNameWithoutExtension();
    if (candidateStem.endsWith ("-pipeline"))
        candidateStem = candidateStem.dropLastCharacters (9);
    context.candidateOutputPath = pipelineFile.getSiblingFile (
        candidateStem + "-candidates.csv").getFullPathName().toStdString();
    context.enabled = ! context.outputPath.empty();
}

inline void clearPipelineTraceContext()
{
    auto& context = pipelineTraceContext();
    context.enabled = false;
    context.outputPath.clear();
    context.candidateOutputPath.clear();
    context.treatment.clear();
    context.seed = 0;
}

struct PipelineTraceSample
{
    std::array<LayerSample, LayerCount> layers {};
    int candidateIndex = -1;
    std::uint32_t candidateIdentity = 0;
    double quality = 0.0;
};

// Diagnostic sensitivity helper. It deliberately requires at least two complete
// windows; one window cannot demonstrate a repetition. This is not assumed to be
// the original corpus metric definition.
inline std::pair<double, bool> repeatedWindowRate (const std::vector<int>& intervals,
                                                   size_t windowLength,
                                                   size_t stride,
                                                   bool clipIntervals)
{
    if (windowLength == 0 || stride == 0 || intervals.size() < windowLength)
        return { 0.0, false };

    const size_t windowCount = 1 + (intervals.size() - windowLength) / stride;
    if (windowCount < 2)
        return { 0.0, false };

    std::map<std::vector<int>, size_t> counts;
    for (size_t start = 0; start + windowLength <= intervals.size(); start += stride)
    {
        std::vector<int> window;
        window.reserve (windowLength);
        for (size_t offset = 0; offset < windowLength; ++offset)
        {
            int interval = intervals[start + offset];
            if (clipIntervals)
                interval = std::max (-12, std::min (12, interval));
            window.push_back (interval);
        }
        ++counts[window];
    }

    size_t repeats = 0;
    for (const auto& entry : counts)
        if (entry.second > 1)
            repeats += entry.second - 1;
    return { (double) repeats / (double) windowCount, true };
}

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

    std::ofstream csv (context.outputPath, std::ios::out | std::ios::app);
    if (! csv.is_open())
        return false;

    auto writeLayerPopulation = [&] (const char* population,
                                     size_t layer,
                                     const std::vector<const PipelineTraceSample*>& populationSamples,
                                     size_t eligibleCount)
    {
        std::vector<double> noteCounts;
        std::vector<double> qualities;
        std::array<std::vector<double>, MetricCount> metricValues;
        noteCounts.reserve (populationSamples.size());
        qualities.reserve (populationSamples.size());

        for (const auto* sample : populationSamples)
        {
            const auto& layerSample = sample->layers[layer];
            noteCounts.push_back ((double) layerSample.noteCount);
            qualities.push_back (sample->quality);
            for (size_t metric = 0; metric < MetricCount; ++metric)
                if (layerSample.valid[metric])
                    metricValues[metric].push_back (layerSample.value[metric]);
        }

        for (size_t metric = 0; metric < MetricCount; ++metric)
        {
            const auto& values = metricValues[metric];
            csv << context.seed << ',' << context.treatment << ','
                << (stage != nullptr ? stage : "unknown") << ','
                << populationSamples.size() << ',' << layerNames[layer] << ',';
            if (noteCounts.empty())
                csv << "NA,NA,NA,";
            else
                csv << std::fixed << std::setprecision (6)
                    << quantile (noteCounts, 0.10) << ','
                    << quantile (noteCounts, 0.50) << ','
                    << quantile (noteCounts, 0.90) << ',';
            csv << eligibleCount << ',' << metricNames[metric] << ','
                << values.size() << ',';
            if (values.empty())
                csv << "NA,NA,NA";
            else
                csv << std::fixed << std::setprecision (6)
                    << quantile (values, 0.10) << ','
                    << quantile (values, 0.50) << ','
                    << quantile (values, 0.90);
            if (qualities.empty())
                csv << ",NA,NA,NA";
            else
                csv << ',' << std::fixed << std::setprecision (6)
                    << quantile (qualities, 0.10) << ','
                    << quantile (qualities, 0.50) << ','
                    << quantile (qualities, 0.90);
            csv << ',' << population << '\n';
        }
    };

    std::vector<const PipelineTraceSample*> allSamples;
    allSamples.reserve (samples.size());
    for (const auto& sample : samples)
        allSamples.push_back (&sample);

    for (size_t layer = 0; layer < LayerCount; ++layer)
    {
        size_t eligibleCount = 0;
        std::vector<const PipelineTraceSample*> eligibleSamples;
        eligibleSamples.reserve (samples.size());
        for (const auto& sample : samples)
        {
            if (sample.layers[layer].noteCount >= 50)
            {
                ++eligibleCount;
                eligibleSamples.push_back (&sample);
            }
        }

        // "all" preserves the existing whole-pool summary.
        writeLayerPopulation ("all", layer, allSamples, eligibleCount);
        // Corpus-comparable subset: only candidates with at least 50 notes in this
        // exact pitch layer. Empty subsets produce NA metrics rather than fake zeros.
        writeLayerPopulation ("layer50_eligible", layer, eligibleSamples, eligibleCount);
    }
    return csv.good();
}


inline bool appendPipelineCandidateAudit (const char* stage,
                                          const std::vector<PipelineTraceSample>& samples)
{
    const auto& context = pipelineTraceContext();
    if (! context.enabled || context.treatment != "Baseline"
        || context.candidateOutputPath.empty() || samples.empty())
        return true;

    std::ofstream csv (context.candidateOutputPath, std::ios::out | std::ios::app);
    if (! csv.is_open())
        return false;

    for (const auto& sample : samples)
    {
        csv << context.seed << ',' << context.treatment << ','
            << (stage != nullptr ? stage : "unknown") << ','
            << sample.candidateIndex << ',' << sample.candidateIdentity << ','
            << std::fixed << std::setprecision (6) << sample.quality;

        for (size_t layer = 0; layer < LayerCount; ++layer)
        {
            const auto& layerSample = sample.layers[layer];
            csv << ',' << layerSample.noteCount;
            for (size_t metric = 0; metric < MetricCount; ++metric)
            {
                csv << ',';
                if (layerSample.valid[metric])
                    csv << std::fixed << std::setprecision (6) << layerSample.value[metric];
                else
                    csv << "NA";
            }
            for (size_t probe = 0; probe < RepeatProbeCount; ++probe)
            {
                csv << ',';
                if (layerSample.repeatProbeValid[probe])
                    csv << std::fixed << std::setprecision (6) << layerSample.repeatProbeValue[probe];
                else
                    csv << "NA";
            }
        }
        csv << '\n';
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
        // One complete window has no comparison partner; report NA rather than
        // treating "repeat not observable" as an observed repeat rate of zero.
        if (nWindows >= 2)
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

        // Sensitivity-only alternatives expose how the repeat statistic changes
        // with window length, overlap, and extreme-interval clipping. These values
        // must not be compared with corpus targets until the original calculator
        // definition is recovered.
        const auto repeat6Sliding = repeatedWindowRate (intervals, 6, 1, true);
        result.repeatProbeValue[Repeat6SlidingClipped] = repeat6Sliding.first;
        result.repeatProbeValid[Repeat6SlidingClipped] = repeat6Sliding.second;

        const auto repeat4NonOverlap = repeatedWindowRate (intervals, 4, 4, true);
        result.repeatProbeValue[Repeat4NonOverlapClipped] = repeat4NonOverlap.first;
        result.repeatProbeValid[Repeat4NonOverlapClipped] = repeat4NonOverlap.second;

        const auto repeat4Sliding = repeatedWindowRate (intervals, 4, 1, true);
        result.repeatProbeValue[Repeat4SlidingClipped] = repeat4Sliding.first;
        result.repeatProbeValid[Repeat4SlidingClipped] = repeat4Sliding.second;

        const auto repeat6Raw = repeatedWindowRate (intervals, 6, 6, false);
        result.repeatProbeValue[Repeat6NonOverlapRaw] = repeat6Raw.first;
        result.repeatProbeValid[Repeat6NonOverlapRaw] = repeat6Raw.second;
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
