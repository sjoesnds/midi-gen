#include "PluginProcessor.h"
#include "MemorabilityProbe.h"
#include "MidiForgeAblation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using ProbeNote = midiforge::memorability::Note;

struct ReportRow
{
    int seed = 0;
    int variation = 0;
    int bars = 4;
    int root = 0;
    int scale = 1;
    float quality = -1.0f;
    int archetype = -1;
    int mood = -1;
    std::vector<ProbeNote> notes;
    midiforge::memorability::Metrics metrics;
};

double median (std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort (values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if ((values.size() & 1u) != 0u) return values[mid];
    return 0.5 * (values[mid - 1] + values[mid]);
}

struct Columns
{
    std::vector<std::string> fields;
};

std::vector<std::string> splitCsvLine (const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream stream (line);
    std::string field;
    while (std::getline (stream, field, ','))
        fields.push_back (field);
    if (! line.empty() && line.back() == ',')
        fields.emplace_back();
    return fields;
}

int columnIndex (const std::vector<std::string>& names, const std::string& name)
{
    const auto it = std::find (names.begin(), names.end(), name);
    return it == names.end() ? -1 : (int) std::distance (names.begin(), it);
}
}

bool validateMemorabilityProbe()
{
    using midiforge::memorability::Note;
    const std::vector<Note> repeated {
        { 0, 2, 60, 3 }, { 2, 2, 62, 3 }, { 4, 2, 64, 3 },
        { 8, 2, 60, 3 }, { 10, 2, 62, 3 }, { 12, 2, 64, 3 },
        { 16, 2, 60, 3 }, { 18, 2, 62, 3 }, { 20, 2, 64, 3 }
    };
    const auto hook = midiforge::memorability::analyze (repeated, 2, 0, 1, 0.0f, 0.0f);
    const bool knownHook = hook.hookPresent && hook.hookCount == 3
        && hook.hookLengthNotes == 3 && hook.returnCandidates >= 2;
    const bool exactReturns = hook.returnExact > 0.0f && hook.returnVaried == 0.0f;

    const std::vector<Note> noRepeats {
        { 0, 2, 60, 3 }, { 2, 2, 62, 3 }, { 4, 2, 64, 3 },
        { 6, 2, 67, 3 }, { 8, 2, 71, 3 }
    };
    const auto noHook = midiforge::memorability::analyze (noRepeats, 1, 0, 1);
    const bool noRepeatDetected = ! noHook.hookPresent;

    const std::vector<Note> sharpEnding {
        { 0, 2, 60, 3 }, { 4, 2, 62, 3 }, { 8, 2, 64, 3 },
        { 12, 2, 76, 3 }
    };
    const auto closure = midiforge::memorability::analyze (sharpEnding, 1, 0, 1);
    const bool badEndingDetected = ! closure.closureMelodyOk && closure.closureMelodyLeap > 2;

    const std::vector<Note> seamOnly {
        { 0, 2, 60, 3 }, { 4, 2, 62, 3 }, { 8, 2, 61, 3 }
    };
    const auto seam = midiforge::memorability::analyze (seamOnly, 1, 0, 1);
    const bool seamAndCadenceSplit = seam.closureMelodyLeap == 1
        && ! seam.closureMelodyTonicOrDominant && ! seam.closureMelodyOk;

    const std::vector<Note> octaveTonicEnding {
        { 0, 2, 60, 3 }, { 4, 2, 64, 3 }, { 8, 2, 72, 3 }
    };
    const auto octave = midiforge::memorability::analyze (octaveTonicEnding, 1, 0, 1);
    const bool tonalEndingSplit = octave.closureMelodyLeap == 12
        && octave.closureMelodyTonicOrDominant && ! octave.closureMelodyOk;

    return knownHook && exactReturns && noRepeatDetected && badEndingDetected
        && seamAndCadenceSplit && tonalEndingSplit;
}

int runMemorabilityReport (int seedCount, const juce::File& outputFile)
{
    seedCount = juce::jlimit (1, 4096, seedCount);
    if (! outputFile.getParentDirectory().createDirectory())
    {
        std::fprintf (stderr, "Could not create memorability output directory\n");
        return 2;
    }

    std::vector<ReportRow> rows;
    rows.reserve ((size_t) seedCount * 8);
    const auto settingsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_memorability_report");
    settingsRoot.createDirectory();

    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
    {
        const int seed = 108000 + seedIndex;
        const auto seedSettings = settingsRoot.getChildFile ("seed_" + juce::String (seed));
        seedSettings.deleteRecursively();
        if (! seedSettings.createDirectory()) return 2;
        MidiForgeAudioProcessor::setSettingsDirectoryOverride (seedSettings);
        {
            MidiForgeAudioProcessor processor;
            processor.setFeedbackLogFile (juce::File());
            processor.setTasteEnabled (true);
            processor.setSeed (seed);
            processor.waitForGeneration();
            // Match the public baseline's deterministic synthetic preference context.
            processor.trainTaste (0, 1.0f, 1.0f);
            processor.trainTaste (7, 0.0f, 1.0f);
            midiforge::qa::setActiveAblation ("");
            processor.magicRandomize();
            processor.waitForGeneration();

            const int slots = processor.getVariationCount();
            for (int slot = 0; slot < slots; ++slot)
            {
                processor.chooseVariation (slot);
                ReportRow row;
                row.seed = seed;
                row.variation = slot + 1;
                row.bars = juce::jmax (1, processor.getVisibleBars());
                row.root = processor.getRoot();
                row.scale = processor.getScale();
                processor.getVariationQASelectionMetadata (slot, row.quality, row.archetype, row.mood);
                for (const auto& n : processor.getVisibleNotes())
                    if (n.channel >= 1 && n.channel <= 3)
                        row.notes.push_back ({ n.step, n.length, n.note, n.channel });
                row.metrics = midiforge::memorability::analyze (
                    row.notes, row.bars, row.root, 1, -1.0f, -1.0f);
                rows.push_back (std::move (row));
            }
        }
        seedSettings.deleteRecursively();
    }
    midiforge::qa::setActiveAblation ("");
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (juce::File());
    if (rows.empty()) return 2;

    // The varied-return band uses the median positive edit distance across the
    // fixed-seed baseline; exact copies are a separate category and excluded.
    std::vector<double> positiveDistances, densitySpreads, registerSpreads;
    for (const auto& row : rows)
    {
        for (const int d : row.metrics.returnDistances)
            if (d > 0) positiveDistances.push_back ((double) d);
        densitySpreads.push_back (row.metrics.contrastDensity);
        registerSpreads.push_back (row.metrics.contrastRegister);
    }
    const int returnThreshold = std::max (1, (int) std::lround (median (positiveDistances)));
    const float densityThreshold = (float) median (densitySpreads);
    const float registerThreshold = (float) median (registerSpreads);
    for (auto& row : rows)
        row.metrics = midiforge::memorability::analyze (
            row.notes, row.bars, row.root, returnThreshold, densityThreshold, registerThreshold);

    std::ofstream csv (outputFile.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
    if (! csv.is_open())
    {
        std::fprintf (stderr, "Could not open memorability output CSV\n");
        return 2;
    }
    csv << "seed,variation,quality,archetype,mood,bars,root,scale,melody_note_count,"
           "hook_present,hook_count,hook_length_notes,hook_spread_bars,"
           "return_candidates,return_threshold,return_exact,return_varied,return_random,return_score,"
           "contrast_density,contrast_register,contrast_present,contrast_threshold_density,contrast_threshold_register,"
           "closure_melody_leap,closure_melody_length_delta,closure_melody_first_pitch,closure_melody_last_pitch,"
           "closure_melody_tonic_or_dominant,closure_melody_ok,"
           "closure_chord_leap,closure_chord_length_delta,closure_chord_first_pitch,closure_chord_last_pitch,"
           "closure_chord_tonic_or_dominant,closure_chord_ok\n";
    for (const auto& row : rows)
    {
        const auto& m = row.metrics;
        const int melodyCount = (int) std::count_if (row.notes.begin(), row.notes.end(),
            [] (const auto& n) { return n.channel == 3; });
        csv << row.seed << ',' << row.variation << ','
            << std::fixed << std::setprecision (4) << row.quality << ','
            << row.archetype << ',' << row.mood << ',' << row.bars << ','
            << row.root << ',' << row.scale << ',' << melodyCount << ','
            << (m.hookPresent ? 1 : 0) << ',' << m.hookCount << ',' << m.hookLengthNotes << ','
            << std::setprecision (3) << m.hookSpreadBars << ','
            << m.returnCandidates << ',' << returnThreshold << ','
            << std::setprecision (4) << m.returnExact << ',' << m.returnVaried << ','
            << m.returnRandom << ',' << m.returnScore << ','
            << m.contrastDensity << ',' << m.contrastRegister << ','
            << (m.contrastPresent ? 1 : 0) << ',' << densityThreshold << ',' << registerThreshold << ','
            << m.closureMelodyLeap << ',' << m.closureMelodyLengthDelta << ','
            << m.closureMelodyFirstPitch << ',' << m.closureMelodyLastPitch << ','
            << (m.closureMelodyTonicOrDominant ? 1 : 0) << ','
            << (m.closureMelodyOk ? 1 : 0) << ','
            << m.closureChordLeap << ',' << m.closureChordLengthDelta << ','
            << m.closureChordFirstPitch << ',' << m.closureChordLastPitch << ','
            << (m.closureChordTonicOrDominant ? 1 : 0) << ','
            << (m.closureChordOk ? 1 : 0) << '\n';
    }
    csv.flush();
    if (! csv.good()) return 2;
    std::printf ("MEMORABILITY_REPORT rows=%d return_threshold=%d contrast_density_threshold=%.3f contrast_register_threshold=%.3f\n",
                 (int) rows.size(), returnThreshold, densityThreshold, registerThreshold);
    return 0;
}

int runMemorabilityPack (const juce::File& csvFile, const juce::File& outputDirectory)
{
    juce::FileInputStream input (csvFile);
    if (! input.openedOk()) return 2;
    std::string line;
    if (input.isExhausted()) return 2;
    line = input.readNextLine().toStdString();
    const auto header = splitCsvLine (line);
    const int seedCol = columnIndex (header, "seed");
    const int variationCol = columnIndex (header, "variation");
    const int archetypeCol = columnIndex (header, "archetype");
    const int filenameCol = columnIndex (header, "midi_filename");
    if (seedCol < 0 || variationCol < 0 || archetypeCol < 0) return 2;
    if (! outputDirectory.createDirectory()) return 2;

    const auto promptFile = outputDirectory.getChildFile ("LISTENING_PROMPT.md");
    std::ofstream prompt (promptFile.getFullPathName().toStdString());
    prompt << "# MIDI Forge — free-form listening notes\n\n"
              "Open the 12 MIDI clips in FL Studio with the same instrument, preset, volume and effects. "
              "No scores or spreadsheet are needed. After listening, send free-form notes on three things:\n\n"
              "1. Which loop stayed in your memory, and what exactly: motif, rhythm or contour?\n"
              "2. Where does the melody feel random or stumble?\n"
              "3. Does the ending feel complete when it loops back to the start?\n\n"
              "File names identify the original seed, variation and archetype index; they do not imply a rating.\n";
    prompt.close();

    int exports = 0;
    const auto tempRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_memorability_pack");
    tempRoot.createDirectory();
    while (! input.isExhausted())
    {
        line = input.readNextLine().toStdString();
        if (line.empty()) continue;
        const auto fields = splitCsvLine (line);
        if (seedCol >= (int) fields.size() || variationCol >= (int) fields.size()
            || archetypeCol >= (int) fields.size()) return 2;
        const int seed = std::atoi (fields[(size_t) seedCol].c_str());
        const int variation = std::atoi (fields[(size_t) variationCol].c_str());
        const int expectedArchetype = std::atoi (fields[(size_t) archetypeCol].c_str());
        const juce::String filename = filenameCol >= 0 && filenameCol < (int) fields.size()
            ? juce::String (fields[(size_t) filenameCol])
            : juce::String (seed) + "_" + juce::String (variation) + "_" + juce::String (expectedArchetype) + ".mid";
        const auto runSettings = tempRoot.getChildFile ("seed_" + juce::String (seed) + "_" + juce::String (variation));
        runSettings.deleteRecursively();
        if (! runSettings.createDirectory()) return 2;
        MidiForgeAudioProcessor::setSettingsDirectoryOverride (runSettings);
        bool exported = false;
        {
            MidiForgeAudioProcessor processor;
            processor.setFeedbackLogFile (juce::File());
            processor.setTasteEnabled (true);
            processor.setSeed (seed);
            processor.waitForGeneration();
            processor.trainTaste (0, 1.0f, 1.0f);
            processor.trainTaste (7, 0.0f, 1.0f);
            midiforge::qa::setActiveAblation ("");
            processor.magicRandomize();
            processor.waitForGeneration();
            float quality = -1.0f;
            int archetype = -1, mood = -1;
            processor.getVariationQASelectionMetadata (variation - 1, quality, archetype, mood);
            if (archetype != expectedArchetype)
            {
                std::fprintf (stderr, "Replayed archetype mismatch seed=%d variation=%d expected=%d got=%d\n",
                              seed, variation, expectedArchetype, archetype);
                return 3;
            }
            processor.chooseVariation (variation - 1);
            exported = processor.exportMidiFileTo (outputDirectory.getChildFile (filename));
        }
        runSettings.deleteRecursively();
        if (! exported) return 2;
        ++exports;
    }
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (juce::File());
    std::printf ("MEMORABILITY_PACK exports=%d\n", exports);
    return exports == 12 ? 0 : 2;
}
