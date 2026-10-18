// MIDI Forge 0.108.0 release checks for the melody + chords + bass creator.
#include "PluginProcessor.h"
#include "MidiForgeAblation.h"
#include "MemorabilityProbe.h"
#include "corpus_layer_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check (const char* name, bool ok, const juce::String& detail)
    {
        std::printf ("[%s] %s — %s\n",
                     ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
        if (! ok) ++failures;
    }

    using Note = MidiForgeAudioProcessor::VisibleNote;

    bool inScale (int midiNote, int root, int scaleIndex)
    {
        static const std::array<std::array<int, 12>, 12> scales =
        {{
            {{0,2,4,5,7,9,11, -1,-1,-1,-1,-1}},
            {{0,2,3,5,7,8,10,-1,-1,-1,-1,-1}},
            {{0,2,3,5,7,9,10,-1,-1,-1,-1,-1}},
            {{0,1,3,5,7,8,10,-1,-1,-1,-1,-1}},
            {{0,2,3,5,7,8,11,-1,-1,-1,-1,-1}},
            {{0,2,3,5,7,9,11,-1,-1,-1,-1,-1}},
            {{0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1}},
            {{0,2,4,6,7,9,11,-1,-1,-1,-1,-1}},
            {{0,2,4,5,7,9,10,-1,-1,-1,-1,-1}},
            {{0,1,3,5,6,8,10,-1,-1,-1,-1,-1}},
            {{0,2,4,5,7,8,11,-1,-1,-1,-1,-1}},
            {{0,3,5,6,7,10,-1,-1,-1,-1,-1,-1}}
        }};

        if (scaleIndex < 0 || scaleIndex >= (int) scales.size())
            return false;

        const int relative = ((midiNote % 12) - root + 12) % 12;
        for (const int degree : scales[(size_t) scaleIndex])
        {
            if (degree < 0) break;
            if (degree == relative) return true;
        }
        return false;
    }

    struct MelodyChecks
    {
        bool populated = true;
        bool supportedLayers = true;
        bool inLoop = true;
        bool onGrid = true;
        bool noCollisions = true;
        bool spacing = true;
        bool tonal = true;
        bool registerSafe = true;
        bool leapsSafe = true;
        bool seamSafe = true;
        int noteCount = 0;
        int melodyNotes = 0;
        int chordNotes = 0;
        int bassNotes = 0;
        int severeLeapReversals = 0;
    };

    MelodyChecks inspectMelody (const std::vector<Note>& notes,
                                int bars, int root, int scale)
    {
        MelodyChecks c;
        c.populated = ! notes.empty();
        const int loopSteps = juce::jmax (1, bars) * 16;
        std::vector<std::pair<int, int>> timeline;
        std::set<int> melodyOnsets;

        for (const auto& n : notes)
        {
            if (n.channel < 1 || n.channel > 3)
            {
                c.supportedLayers = false;
                continue;
            }

            ++c.noteCount;
            c.inLoop = c.inLoop
                && n.step >= 0 && n.step < loopSteps
                && n.length >= 1 && n.length <= 16
                && n.step + n.length <= loopSteps
                && n.note >= 0 && n.note <= 127
                && n.velocity >= 1 && n.velocity <= 127;
            c.onGrid = c.onGrid && (n.step % 2 == 0);
            c.tonal = c.tonal && inScale (n.note, root, scale);

            if (n.channel == 1)
                ++c.chordNotes;
            else if (n.channel == 2)
                ++c.bassNotes;
            else
            {
                ++c.melodyNotes;
                c.noCollisions = c.noCollisions && melodyOnsets.insert (n.step).second;
                c.registerSafe = c.registerSafe && n.note >= 48 && n.note <= 90;
                timeline.push_back ({ n.step, n.note });
            }
        }

        c.populated = c.populated && c.melodyNotes > 0;
        c.supportedLayers = c.supportedLayers && c.populated;
        std::stable_sort (timeline.begin(), timeline.end(),
            [] (const auto& a, const auto& b)
            {
                if (a.first != b.first) return a.first < b.first;
                return a.second < b.second;
            });

        for (size_t i = 1; i < timeline.size(); ++i)
        {
            c.spacing = c.spacing && timeline[i].first - timeline[i - 1].first >= 2;
            c.leapsSafe = c.leapsSafe
                && std::abs (timeline[i].second - timeline[i - 1].second) <= 12;

            if (i >= 2)
            {
                const int previousDelta = timeline[i - 1].second - timeline[i - 2].second;
                const int currentDelta = timeline[i].second - timeline[i - 1].second;
                const bool reverses = (previousDelta > 0 && currentDelta < 0)
                                   || (previousDelta < 0 && currentDelta > 0);
                if (std::abs (previousDelta) >= 7
                    && std::abs (currentDelta) >= 7
                    && reverses)
                    ++c.severeLeapReversals;
            }
        }

        c.seamSafe = timeline.size() >= 2
            && std::abs (timeline.back().second - timeline.front().second) <= 9;
        return c;
    }

    struct HarmonyFitChecks
    {
        int structuralAnchors = 0;
        int chordToneAnchors = 0;
    };

    HarmonyFitChecks inspectCurrentChordFit (const std::vector<Note>& notes, int bars)
    {
        struct ChordMoment
        {
            int step = 0;
            std::array<bool, 12> pcs {};
        };

        HarmonyFitChecks result;
        std::vector<ChordMoment> moments;
        for (const auto& note : notes)
        {
            if (note.channel != 1 || note.step < 0)
                continue;

            auto it = std::find_if (moments.begin(), moments.end(),
                [&] (const ChordMoment& moment) { return moment.step == note.step; });
            if (it == moments.end())
            {
                moments.push_back ({ note.step, {} });
                it = moments.end() - 1;
            }
            it->pcs[(size_t) ((note.note % 12 + 12) % 12)] = true;
        }

        std::stable_sort (moments.begin(), moments.end(),
            [] (const ChordMoment& a, const ChordMoment& b)
            {
                return a.step < b.step;
            });

        std::vector<const Note*> melody;
        for (const auto& note : notes)
            if (note.channel == 3)
                melody.push_back (&note);
        std::stable_sort (melody.begin(), melody.end(),
            [] (const Note* a, const Note* b)
            {
                if (a->step != b->step) return a->step < b->step;
                return a->note < b->note;
            });

        for (size_t i = 0; i < melody.size(); ++i)
        {
            const auto& note = *melody[i];
            const int local = note.step % 16;
            const bool structural = (local % 4) == 0
                || note.length >= 4
                || i + 1 == melody.size();
            if (! structural)
                continue;

            const int bar = note.step / 16;
            int firstInBar = -1;
            int activeInBar = -1;
            for (size_t m = 0; m < moments.size(); ++m)
            {
                if (moments[m].step / 16 != bar)
                    continue;
                if (firstInBar < 0) firstInBar = (int) m;
                if (moments[m].step <= note.step) activeInBar = (int) m;
            }

            const int selected = activeInBar >= 0 ? activeInBar : firstInBar;
            if (selected < 0)
                continue;

            ++result.structuralAnchors;
            const int pc = ((note.note % 12) + 12) % 12;
            if (moments[(size_t) selected].pcs[(size_t) pc])
                ++result.chordToneAnchors;
        }

        return result;
    }

    std::string fingerprint (const std::vector<Note>& notes)
    {
        std::vector<Note> melody;
        for (const auto& n : notes)
            if (n.channel == 3) melody.push_back (n);

        std::stable_sort (melody.begin(), melody.end(),
            [] (const Note& a, const Note& b)
            {
                if (a.step != b.step) return a.step < b.step;
                return a.note < b.note;
            });

        std::string key;
        for (const auto& n : melody)
            key += std::to_string (n.step) + ":" + std::to_string (n.note)
                + ":" + std::to_string (n.length) + ";";
        return key;
    }

    // Normalize away absolute start time and pitch so transposed copies do not
    // count as distinct melodic ideas. Rhythm, contour intervals and note lengths
    // still have to differ for two candidates to pass as different contours.
    std::string contourFingerprint (const std::vector<Note>& notes)
    {
        std::vector<Note> melody;
        for (const auto& n : notes)
            if (n.channel == 3) melody.push_back (n);

        std::stable_sort (melody.begin(), melody.end(),
            [] (const Note& a, const Note& b)
            {
                if (a.step != b.step) return a.step < b.step;
                return a.note < b.note;
            });

        if (melody.empty())
            return {};

        const int firstStep = melody.front().step;
        const int firstPitch = melody.front().note;
        std::string key;
        for (const auto& n : melody)
            key += std::to_string (n.step - firstStep) + ":"
                + std::to_string (n.note - firstPitch) + ":"
                + std::to_string (n.length) + ";";
        return key;
    }

    // Detect a short contour returned from the opening bar in the fourth
    // bar of a four-bar phrase. Octave transposition is allowed, but the
    // consecutive pitch intervals must match exactly and the cadence note is
    // excluded from the returned window.
    bool hasMotifReturn (const std::vector<Note>& notes, int bars)
    {
        std::vector<Note> melody;
        for (const auto& note : notes)
            if (note.channel == 3)
                melody.push_back (note);

        std::stable_sort (melody.begin(), melody.end(),
            [] (const Note& a, const Note& b)
            {
                if (a.step != b.step) return a.step < b.step;
                return a.note < b.note;
            });

        for (int phraseStart = 0; phraseStart + 3 < bars; phraseStart += 4)
        {
            std::vector<Note> opening;
            std::vector<Note> reprise;
            for (const auto& note : melody)
            {
                const int bar = note.step / 16;
                if (bar == phraseStart)
                    opening.push_back (note);
                else if (bar == phraseStart + 3)
                    reprise.push_back (note);
            }

            if (opening.size() < 4 || reprise.size() < 5)
                continue;

            const size_t hookNotes = std::min<size_t> (
                7, std::min (opening.size(), reprise.size() - 1));
            const size_t sourceStart = opening.size() - hookNotes;
            const size_t targetStart = reprise.size() - hookNotes - 1;
            bool sameIntervals = true;
            for (size_t i = 1; i < hookNotes; ++i)
            {
                const int sourceDelta = opening[sourceStart + i].note
                    - opening[sourceStart + i - 1].note;
                const int targetDelta = reprise[targetStart + i].note
                    - reprise[targetStart + i - 1].note;
                if (sourceDelta != targetDelta)
                {
                    sameIntervals = false;
                    break;
                }
            }

            if (sameIntervals)
                return true;
        }
        return false;
    }

    std::string midiBankFingerprint (MidiForgeAudioProcessor& processor)
    {
        std::uint64_t hash = 14695981039346656037ull;
        auto addValue = [&] (std::uint32_t value)
        {
            for (int byte = 0; byte < 4; ++byte)
            {
                hash ^= (value >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ull;
            }
        };

        const int count = processor.getVariationCount();
        addValue (0x4D494449u); // "MIDI" domain separator
        addValue (static_cast<std::uint32_t> (count));
        for (int variation = 0; variation < count; ++variation)
        {
            processor.chooseVariation (variation);
            auto notes = processor.getVisibleNotes();
            std::stable_sort (notes.begin(), notes.end(),
                [] (const Note& a, const Note& b)
                {
                    if (a.channel != b.channel) return a.channel < b.channel;
                    if (a.step != b.step) return a.step < b.step;
                    if (a.note != b.note) return a.note < b.note;
                    if (a.length != b.length) return a.length < b.length;
                    return a.velocity < b.velocity;
                });

            addValue (static_cast<std::uint32_t> (variation));
            addValue (static_cast<std::uint32_t> (processor.getVisibleBars()));
            addValue (static_cast<std::uint32_t> (notes.size()));
            for (const auto& n : notes)
            {
                addValue (static_cast<std::uint32_t> (n.step));
                addValue (static_cast<std::uint32_t> (n.length));
                addValue (static_cast<std::uint32_t> (n.note));
                addValue (static_cast<std::uint32_t> (n.velocity));
                addValue (static_cast<std::uint32_t> (n.channel));
            }
        }

        std::ostringstream text;
        text << std::hex << std::setw (16) << std::setfill ('0') << hash;
        return text.str();
    }

    bool parseSupportedMidi (const juce::File& file, int& noteOns,
                             int& chordNoteOns, int& bassNoteOns)
    {
        noteOns = 0;
        chordNoteOns = 0;
        bassNoteOns = 0;
        juce::FileInputStream input (file);
        juce::MidiFile midi;
        if (! input.openedOk() || ! midi.readFrom (input))
            return false;

        bool tempo = false;
        bool timeSignature = false;
        bool supportedChannels = true;
        bool melodyPresent = false;
        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            if (midi.getTrack (t) == nullptr) continue;
            juce::MidiMessageSequence sequence (*midi.getTrack (t));
            for (int i = 0; i < sequence.getNumEvents(); ++i)
            {
                const auto& message = sequence.getEventPointer (i)->message;
                tempo = tempo || message.isTempoMetaEvent();
                timeSignature = timeSignature || message.isTimeSignatureMetaEvent();
                if (! message.isNoteOn()) continue;
                ++noteOns;
                const int channel = message.getChannel();
                supportedChannels = supportedChannels && channel >= 1 && channel <= 3;
                melodyPresent = melodyPresent || channel == 3;
                if (channel == 1) ++chordNoteOns;
                else if (channel == 2) ++bassNoteOns;
            }
        }
        return noteOns > 0 && supportedChannels && melodyPresent
            && chordNoteOns > 0 && bassNoteOns > 0 && tempo && timeSignature;
    }
}


    struct AblationTreatment
    {
        const char* name;
        const char* category;
        const char* target;
        bool disablesTaste = false;
    };

    std::vector<AblationTreatment> ablationTreatments()
    {
        return {
            { "Baseline", "baseline", "", false },
            { "rhythmGrammarScore", "score", "rhythmGrammarScore", false },
            { "melodyPleasantnessScore", "score", "melodyPleasantnessScore", false },
            { "melodyExpressionScore", "score", "melodyExpressionScore", false },
            { "harmonicIntelligenceScore", "score", "harmonicIntelligenceScore", false },
            { "phraseMemory4Score", "score", "phraseMemory4Score", false },
            { "composerGrammarScore", "score", "composerGrammarScore", false },
            { "melodicProsodyScore", "score", "melodicProsodyScore", false },
            { "creativeRangeScore", "score", "creativeRangeScore", false },
            { "contextualPhraseQualityScore", "score", "contextualPhraseQualityScore", false },
            { "motifMemoryScore", "score", "motifMemoryScore", false },
            { "grooveQualityScore", "score", "grooveQualityScore", false },
            { "loopForgeScore", "score", "loopForgeScore", false },
            { "motifSemanticsScore", "score", "motifSemanticsScore", false },
            { "phraseContrastScore", "score", "phraseContrastScore", false },
            { "loopClosureScore", "score", "loopClosureScore", false },
            { "closureJudgeScore", "score", "closureJudgeScore", false },
            { "localMelodyQualityScore", "score", "localMelodyQualityScore", false },
            { "localMelodyRhythmScore", "score", "localMelodyRhythmScore", false },
            { "composerJudgeScore", "score", "composerJudgeScore", false },
            { "judge:idea", "composer_judge_group", "judge:idea", false },
            { "judge:expression", "composer_judge_group", "judge:expression", false },
            { "judge:harmony", "composer_judge_group", "judge:harmony", false },
            { "judge:rhythm", "composer_judge_group", "judge:rhythm", false },
            { "judge:novelty", "composer_judge_group", "judge:novelty", false },
            { "judge:register_fit", "composer_judge_group", "judge:register_fit", false },
            { "judge:closure", "composer_judge_group", "judge:closure", false },
            { "judge:density_space", "composer_judge_group", "judge:density_space", false },
            { "judge:role_consistency", "composer_judge_group", "judge:role_consistency", false },
            { "TasteML", "taste_ml", "", true }
        };
    }

    int runAblationReport (int seedCount, const juce::File& outputFile)
    {
        seedCount = juce::jlimit (1, 4096, seedCount);
        if (! outputFile.getParentDirectory().createDirectory())
        {
            std::fprintf (stderr, "Could not create report directory: %s\n",
                          outputFile.getParentDirectory().getFullPathName().toRawUTF8());
            return 2;
        }
        std::ofstream csv (outputFile.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
        if (! csv.is_open())
        {
            std::fprintf (stderr, "Could not open ablation report: %s\n",
                          outputFile.getFullPathName().toRawUTF8());
            return 2;
        }
        csv << "treatment,category,seed,variation_count,total_notes,melody_notes,"
               "tonal_safe_fraction,grid_fraction,melody_grid_fraction,unique_melodies,"
               "complexity_simple,complexity_balanced,complexity_complex,"
               "mean_melody_attacks_per_bar,mean_distinct_melody_pitches,"
               "mean_pitch_range_semitones,elapsed_ms,bank_fingerprint\n";

        const auto treatments = ablationTreatments();
        const auto reportRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("midiforge_ablation_report");
        reportRoot.createDirectory();

        for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
        {
            // Every condition uses the same publicly documented fixed seed set.
            const int fixedSeed = 108000 + seedIndex;
            for (const auto& treatment : treatments)
            {
                midiforge::qa::setActiveAblation ("");
                const auto safeName = juce::String (treatment.name).replaceCharacter (':', '_');
                const auto runSettings = reportRoot.getChildFile (safeName + "_" + juce::String (fixedSeed));
                runSettings.deleteRecursively();
                if (! runSettings.createDirectory())
                {
                    std::fprintf (stderr, "Could not create settings directory for %s\n", treatment.name);
                    return 2;
                }
                MidiForgeAudioProcessor::setSettingsDirectoryOverride (runSettings);

                int totalNotes = 0, melodyNotes = 0, tonalSafeNotes = 0;
                int onGridNotes = 0, melodyOnGridNotes = 0;
                int simpleLoops = 0, balancedLoops = 0, complexLoops = 0;
                int variationCount = 0;
                double attacksPerBarSum = 0.0, distinctPitchesSum = 0.0, pitchRangeSum = 0.0;
                int measuredMelodyLoops = 0;
                std::set<std::string> ideas;
                std::string bankHash;
                const auto started = std::chrono::steady_clock::now();

                {
                    MidiForgeAudioProcessor processor;
                    processor.setFeedbackLogFile (juce::File());
                    processor.setTasteEnabled (true);
                    processor.setSeed (fixedSeed);
                    processor.waitForGeneration();

                    // Same synthetic taste context for each arm: initial variation 1
                    // gets a like, initial variation 8 a dislike.
                    processor.trainTaste (0, 1.0f, 1.0f);
                    processor.trainTaste (7, 0.0f, 1.0f);
                    midiforge::qa::setActiveAblation (treatment.target);
                    if (treatment.disablesTaste)
                        processor.setTasteEnabled (false);

                    processor.magicRandomize();
                    processor.waitForGeneration();
                    bankHash = midiBankFingerprint (processor);
                    variationCount = processor.getVariationCount();

                    for (int variation = 0; variation < variationCount; ++variation)
                    {
                        const int complexityClass = processor.getVariationMelodyComplexityClass (variation);
                        if (complexityClass == 0) ++simpleLoops;
                        else if (complexityClass == 1) ++balancedLoops;
                        else if (complexityClass == 2) ++complexLoops;

                        processor.chooseVariation (variation);
                        const auto notes = processor.getVisibleNotes();
                        const int loopBars = juce::jmax (1, processor.getVisibleBars());
                        int loopMelodyNotes = 0;
                        std::set<int> pitches;
                        int lowPitch = 128, highPitch = -1;
                        std::vector<Note> melody;
                        for (const auto& note : notes)
                        {
                            if (note.channel < 1 || note.channel > 3)
                                continue;
                            ++totalNotes;
                            if (inScale (note.note, processor.getRoot(), processor.getScale()))
                                ++tonalSafeNotes;
                            if ((note.step % 2) == 0)
                                ++onGridNotes;
                            if (note.channel != 3)
                                continue;

                            ++melodyNotes;
                            ++loopMelodyNotes;
                            melody.push_back (note);
                            if ((note.step % 2) == 0)
                                ++melodyOnGridNotes;
                            pitches.insert (note.note);
                            lowPitch = juce::jmin (lowPitch, note.note);
                            highPitch = juce::jmax (highPitch, note.note);
                        }
                        ideas.insert (fingerprint (melody));
                        if (loopMelodyNotes > 0)
                        {
                            ++measuredMelodyLoops;
                            attacksPerBarSum += (double) loopMelodyNotes / (double) loopBars;
                            distinctPitchesSum += (double) pitches.size();
                            pitchRangeSum += (double) juce::jmax (0, highPitch - lowPitch);
                        }
                    }
                }

                midiforge::qa::setActiveAblation ("");
                const auto ended = std::chrono::steady_clock::now();
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (ended - started).count();
                const double tonalFraction = totalNotes > 0 ? (double) tonalSafeNotes / (double) totalNotes : 0.0;
                const double gridFraction = totalNotes > 0 ? (double) onGridNotes / (double) totalNotes : 0.0;
                const double melodyGridFraction = melodyNotes > 0 ? (double) melodyOnGridNotes / (double) melodyNotes : 0.0;
                const double meanAttacks = measuredMelodyLoops > 0 ? attacksPerBarSum / measuredMelodyLoops : 0.0;
                const double meanPitches = measuredMelodyLoops > 0 ? distinctPitchesSum / measuredMelodyLoops : 0.0;
                const double meanRange = measuredMelodyLoops > 0 ? pitchRangeSum / measuredMelodyLoops : 0.0;

                csv << treatment.name << ',' << treatment.category << ',' << fixedSeed << ','
                    << variationCount << ',' << totalNotes << ',' << melodyNotes << ','
                    << std::fixed << std::setprecision (4)
                    << tonalFraction << ',' << gridFraction << ',' << melodyGridFraction << ','
                    << ideas.size() << ',' << simpleLoops << ',' << balancedLoops << ',' << complexLoops << ','
                    << meanAttacks << ',' << meanPitches << ',' << meanRange << ','
                    << elapsedMs << ',' << bankHash << '\n';
                csv.flush();
                runSettings.deleteRecursively();
            }
        }

        csv.close();
        std::printf ("Ablation report: %s (%d seeds x %d conditions; %d data rows)\n",
                     outputFile.getFullPathName().toRawUTF8(),
                     seedCount, (int) treatments.size(), seedCount * (int) treatments.size());
        return 0;
    }


    struct CorpusTreatmentStats
    {
        using MetricValues = std::array<
            std::array<std::vector<double>, midiforge::corpus::MetricCount>,
            midiforge::corpus::LayerCount>;
        MetricValues values;
        long long totalNotes = 0;
        long long tonalSafeNotes = 0;
        long long gridNotes = 0;
    };

    void printBaselinePipelineTraceSummary (const juce::File& pipelineFile)
    {
        std::ifstream input (pipelineFile.getFullPathName().toStdString());
        if (! input.is_open())
        {
            std::printf ("Could not read pipeline summary: %s\n",
                         pipelineFile.getFullPathName().toRawUTF8());
            return;
        }

        using MetricSamples = std::array<
            std::array<std::vector<double>, midiforge::corpus::MetricCount>,
            midiforge::corpus::LayerCount>;
        using LayerTriples = std::array<
            std::array<std::vector<double>, 3>, midiforge::corpus::LayerCount>;
        std::array<MetricSamples, 2> metricMedians {};
        std::array<MetricSamples, 2> eligibleMetricMedians {};
        std::array<std::array<std::vector<double>, midiforge::corpus::LayerCount>, 2> noteMedians {};
        std::array<std::array<std::vector<double>, midiforge::corpus::LayerCount>, 2> eligibility {};
        std::array<std::array<std::vector<double>, 3>, 2> quality {};
        std::array<LayerTriples, 2> eligibleQuality {};
        std::map<std::string, double> allCandidateCounts;
        std::map<std::string, double> eligibleCandidateCounts;
        std::string line;
        std::getline (input, line); // header

        while (std::getline (input, line))
        {
            std::stringstream row (line);
            std::vector<std::string> fields;
            std::string field;
            while (std::getline (row, field, ','))
                fields.push_back (field);
            if (fields.size() < 18 || fields[1] != "Baseline")
                continue;

            int stage = -1;
            if (fields[2] == "candidate_pool_before_final_selection") stage = 0;
            else if (fields[2] == "selected_bank_before_output_cleanup") stage = 1;
            if (stage < 0)
                continue;

            size_t layer = midiforge::corpus::LayerCount;
            for (size_t i = 0; i < midiforge::corpus::LayerCount; ++i)
                if (fields[4] == midiforge::corpus::layerNames[i]) layer = i;
            size_t metric = midiforge::corpus::MetricCount;
            for (size_t i = 0; i < midiforge::corpus::MetricCount; ++i)
                if (fields[9] == midiforge::corpus::metricNames[i]) metric = i;
            if (layer >= midiforge::corpus::LayerCount
                || metric >= midiforge::corpus::MetricCount)
                continue;

            const std::string countKey = fields[0] + "|" + fields[2] + "|" + fields[4];
            if (fields[17] == "all")
            {
                if (metric == midiforge::corpus::StepsLe2)
                {
                    if (fields[6] != "NA")
                        noteMedians[(size_t) stage][layer].push_back (std::stod (fields[6]));
                    const double count = std::stod (fields[3]);
                    allCandidateCounts[countKey] = count;
                    if (count > 0.0)
                        eligibility[(size_t) stage][layer].push_back (std::stod (fields[8]) / count);
                    if (layer == (size_t) midiforge::corpus::Top)
                    {
                        for (size_t qi = 0; qi < 3; ++qi)
                            if (fields[14 + qi] != "NA")
                                quality[(size_t) stage][qi].push_back (std::stod (fields[14 + qi]));
                    }
                }

                if (fields[10] != "0" && fields[12] != "NA")
                    metricMedians[(size_t) stage][layer][metric].push_back (std::stod (fields[12]));
            }
            else if (fields[17] == "layer50_eligible")
            {
                if (metric == midiforge::corpus::StepsLe2)
                {
                    const double count = std::stod (fields[3]);
                    eligibleCandidateCounts[countKey] = count;
                    for (size_t qi = 0; qi < 3; ++qi)
                        if (fields[14 + qi] != "NA")
                            eligibleQuality[(size_t) stage][layer][qi].push_back (std::stod (fields[14 + qi]));
                }
                if (fields[10] != "0" && fields[12] != "NA")
                    eligibleMetricMedians[(size_t) stage][layer][metric].push_back (std::stod (fields[12]));
            }
        }

        for (const auto& entry : eligibleCandidateCounts)
        {
            const auto found = allCandidateCounts.find (entry.first);
            if (found != allCandidateCounts.end() && found->second > 0.0)
            {
                const size_t split = entry.first.rfind ('|');
                const size_t stagePosition = entry.first.find ('|');
                const std::string stageName = entry.first.substr (stagePosition + 1,
                                                                  split - stagePosition - 1);
                const int stage = stageName == "candidate_pool_before_final_selection" ? 0 : 1;
                const std::string layerName = entry.first.substr (split + 1);
                for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
                    if (layerName == midiforge::corpus::layerNames[layer])
                        eligibility[(size_t) stage][layer].push_back (entry.second / found->second);
            }
        }

        const char* stageNames[2] = { "candidate_pool", "selected_bank" };
        const char* qualityNames[3] = { "P10", "P50", "P90" };
        std::printf ("CORPUS_PIPELINE_BASELINE metric medians across fixed seeds: pool vs selected bank\n");
        std::printf ("stage,layer,metric,seed_samples,median_of_per_seed_medians\n");
        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
            for (size_t metric = 0; metric < midiforge::corpus::MetricCount; ++metric)
                for (int stage = 0; stage < 2; ++stage)
                {
                    const auto& values = metricMedians[(size_t) stage][layer][metric];
                    std::printf ("%s,%s,%s,%zu,",
                        stageNames[stage], midiforge::corpus::layerNames[layer],
                        midiforge::corpus::metricNames[metric], values.size());
                    if (values.empty()) std::printf ("NA\n");
                    else std::printf ("%.6f\n", midiforge::corpus::median (values));
                }

        std::printf ("CORPUS_PIPELINE_BASELINE coverage: note-count median and >=50-note candidate share\n");
        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
            for (int stage = 0; stage < 2; ++stage)
            {
                const auto& counts = noteMedians[(size_t) stage][layer];
                const auto& eligible = eligibility[(size_t) stage][layer];
                std::printf ("%s,%s,note_count_median,",
                    stageNames[stage], midiforge::corpus::layerNames[layer]);
                if (counts.empty()) std::printf ("NA,");
                else std::printf ("%.3f,", midiforge::corpus::median (counts));
                if (eligible.empty()) std::printf ("NA\n");
                else std::printf ("%.6f\n", midiforge::corpus::median (eligible));
            }

        std::printf ("CORPUS_PIPELINE_BASELINE metrics for candidates with >=50 notes in the same pitch layer\n");
        std::printf ("stage,layer,metric,eligible_seed_samples,median_of_per_seed_eligible_medians\n");
        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
            for (size_t metric = 0; metric < midiforge::corpus::MetricCount; ++metric)
                for (int stage = 0; stage < 2; ++stage)
                {
                    const auto& values = eligibleMetricMedians[(size_t) stage][layer][metric];
                    std::printf ("%s,%s,%s,%zu,",
                        stageNames[stage], midiforge::corpus::layerNames[layer],
                        midiforge::corpus::metricNames[metric], values.size());
                    if (values.empty()) std::printf ("NA\n");
                    else std::printf ("%.6f\n", midiforge::corpus::median (values));
                }

        std::printf ("CORPUS_PIPELINE_BASELINE quality distribution; global quality P10 is the lower-tail guardrail\n");
        for (size_t qi = 0; qi < 3; ++qi)
            for (int stage = 0; stage < 2; ++stage)
            {
                const auto& values = quality[(size_t) stage][qi];
                std::printf ("%s,%s,seed_samples,",
                    stageNames[stage], qualityNames[qi]);
                if (values.empty()) std::printf ("0,NA\n");
                else std::printf ("%zu,%.6f\n", values.size(), midiforge::corpus::median (values));
            }

        std::printf ("CORPUS_PIPELINE_BASELINE quality percentiles within each >=50-note layer subset\n");
        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
            for (size_t qi = 0; qi < 3; ++qi)
                for (int stage = 0; stage < 2; ++stage)
                {
                    const auto& values = eligibleQuality[(size_t) stage][layer][qi];
                    std::printf ("%s,%s,%s,seed_samples,",
                        stageNames[stage], midiforge::corpus::layerNames[layer], qualityNames[qi]);
                    if (values.empty()) std::printf ("0,NA\n");
                    else std::printf ("%zu,%.6f\n", values.size(), midiforge::corpus::median (values));
                }
    }

    int runCorpusLayerAblationReport (int seedCount, const juce::File& outputFile)
    {
        seedCount = juce::jlimit (1, 256, seedCount);

        const auto targetFile = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("data").getChildFile ("corpus_targets.json");
        std::array<std::array<midiforge::corpus::Target, midiforge::corpus::MetricCount>,
                   midiforge::corpus::LayerCount> targets {};
        if (! midiforge::corpus::loadTargets (targetFile, targets))
        {
            std::fprintf (stderr, "Could not load corpus targets from %s\n",
                          targetFile.getFullPathName().toRawUTF8());
            return 2;
        }

        if (! outputFile.getParentDirectory().createDirectory())
        {
            std::fprintf (stderr, "Could not create corpus report directory: %s\n",
                          outputFile.getParentDirectory().getFullPathName().toRawUTF8());
            return 2;
        }

        const auto pipelineFile = outputFile.getSiblingFile (
            outputFile.getFileNameWithoutExtension() + "-pipeline.csv");
        {
            std::ofstream traceCsv (pipelineFile.getFullPathName().toStdString(),
                                   std::ios::out | std::ios::trunc);
            if (! traceCsv.is_open())
            {
                std::fprintf (stderr, "Could not create pipeline trace: %s\n",
                              pipelineFile.getFullPathName().toRawUTF8());
                return 2;
            }
            traceCsv << "seed,treatment,stage,candidate_count,layer,layer_note_count_p10,"
                        "layer_note_count_p50,layer_note_count_p90,candidates_ge_50_notes,"
                        "metric,metric_samples,metric_p10,metric_p50,metric_p90,"
                        "quality_p10,quality_p50,quality_p90,population\n";
        }

        const auto candidateFile = outputFile.getSiblingFile (
            outputFile.getFileNameWithoutExtension() + "-candidates.csv");
        {
            std::ofstream candidateCsv (candidateFile.getFullPathName().toStdString(),
                                        std::ios::out | std::ios::trunc);
            if (! candidateCsv.is_open())
            {
                std::fprintf (stderr, "Could not create corpus candidate audit: %s\n",
                              candidateFile.getFullPathName().toRawUTF8());
                return 2;
            }
            candidateCsv << "seed,treatment,stage,candidate_index,candidate_identity,quality,"
                            "top_note_count,top_steps_le2,top_leaps_ge5,top_window_repeat,"
                            "top_dur_beats,top_notes_per_bar,"
                            "top_window_repeat6_sliding_clip,top_window_repeat4_nonoverlap_clip,"
                            "top_window_repeat4_sliding_clip,top_window_repeat6_nonoverlap_raw,"
                            "riff_note_count,riff_steps_le2,riff_leaps_ge5,riff_window_repeat,"
                            "riff_dur_beats,riff_notes_per_bar,"
                            "riff_window_repeat6_sliding_clip,riff_window_repeat4_nonoverlap_clip,"
                            "riff_window_repeat4_sliding_clip,riff_window_repeat6_nonoverlap_raw,"
                            "bass_note_count,bass_steps_le2,bass_leaps_ge5,bass_window_repeat,"
                            "bass_dur_beats,bass_notes_per_bar,"
                            "bass_window_repeat6_sliding_clip,bass_window_repeat4_nonoverlap_clip,"
                            "bass_window_repeat4_sliding_clip,bass_window_repeat6_nonoverlap_raw\n";
        }

        const auto treatments = ablationTreatments();
        std::vector<CorpusTreatmentStats> allStats (treatments.size());
        const auto reportRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("midiforge_corpus_layer_ablation");
        reportRoot.createDirectory();

        for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
        {
            const int fixedSeed = 108000 + seedIndex;
            for (size_t treatmentIndex = 0; treatmentIndex < treatments.size(); ++treatmentIndex)
            {
                const auto& treatment = treatments[treatmentIndex];
                midiforge::qa::setActiveAblation ("");
                const auto safeName = juce::String (treatment.name).replaceCharacter (':', '_');
                const auto runSettings = reportRoot.getChildFile (
                    safeName + "_" + juce::String (fixedSeed));
                runSettings.deleteRecursively();
                if (! runSettings.createDirectory())
                {
                    std::fprintf (stderr, "Could not create corpus settings directory for %s\n",
                                  treatment.name);
                    midiforge::qa::setActiveAblation ("");
                    return 2;
                }
                MidiForgeAudioProcessor::setSettingsDirectoryOverride (runSettings);

                {
                    MidiForgeAudioProcessor processor;
                    processor.setFeedbackLogFile (juce::File());
                    processor.setTasteEnabled (true);
                    processor.setSeed (fixedSeed);
                    processor.waitForGeneration();

                    // Match the existing ablation report's taste context exactly.
                    processor.trainTaste (0, 1.0f, 1.0f);
                    processor.trainTaste (7, 0.0f, 1.0f);
                    midiforge::qa::setActiveAblation (treatment.target);
                    if (treatment.disablesTaste)
                        processor.setTasteEnabled (false);

                    midiforge::corpus::setPipelineTraceContext (
                        pipelineFile, treatment.name, fixedSeed);
                    processor.magicRandomize();
                    processor.waitForGeneration();
                    midiforge::corpus::clearPipelineTraceContext();

                    const int variationCount = processor.getVariationCount();
                    for (int variation = 0; variation < variationCount; ++variation)
                    {
                        processor.chooseVariation (variation);
                        const auto notes = processor.getVisibleNotes();
                        const auto measured = midiforge::corpus::measure (
                            notes, processor.getVisibleBars());
                        auto& stats = allStats[treatmentIndex];

                        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
                            for (size_t metric = 0; metric < midiforge::corpus::MetricCount; ++metric)
                                if (measured[layer].valid[metric])
                                    stats.values[layer][metric].push_back (measured[layer].value[metric]);

                        for (const auto& note : notes)
                        {
                            // Match the corpus non-drum filter for plugin-visible MIDI.
                            if (note.channel == 5 || note.channel == 9 || note.channel == 10)
                                continue;
                            ++stats.totalNotes;
                            if (inScale (note.note, processor.getRoot(), processor.getScale()))
                                ++stats.tonalSafeNotes;
                            if ((note.step % 2) == 0)
                                ++stats.gridNotes;
                        }
                    }
                }

                midiforge::qa::setActiveAblation ("");
                runSettings.deleteRecursively();
            }
        }

        // Calculate baseline medians once; every other row reports the difference
        // from the baseline so the existing score/judge effects are directly visible.
        std::array<std::array<double, midiforge::corpus::MetricCount>,
                   midiforge::corpus::LayerCount> baselineMedians {};
        std::array<std::array<bool, midiforge::corpus::MetricCount>,
                   midiforge::corpus::LayerCount> baselineValid {};
        for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
            for (size_t metric = 0; metric < midiforge::corpus::MetricCount; ++metric)
            {
                const auto& samples = allStats[0].values[layer][metric];
                if (! samples.empty())
                {
                    baselineMedians[layer][metric] = midiforge::corpus::median (samples);
                    baselineValid[layer][metric] = true;
                }
            }

        std::ofstream csv (outputFile.getFullPathName().toStdString(),
                           std::ios::out | std::ios::trunc);
        if (! csv.is_open())
        {
            std::fprintf (stderr, "Could not open corpus report: %s\n",
                          outputFile.getFullPathName().toRawUTF8());
            midiforge::qa::setActiveAblation ("");
            return 2;
        }

        csv << "treatment,category,seeds,variation_samples,layer,metric,bank_median,"
               "corpus_median,iqr_low,iqr_high,deviation_iqr_units,outside_iqr,"
               "delta_vs_baseline,tonal_safe_fraction,grid_fraction,notes_checked\n";
        std::printf ("CORPUS_LAYER_ABLATION seeds=%d variations_per_seed=8 "
                     "unit=per-candidate-loop; source_min_notes_per_layer=50\n", seedCount);
        std::printf ("treatment,layer,metric,n,bank_median,corpus_median,"
                     "iqr_low,iqr_high,deviation_iqr_units,outside_iqr,delta_vs_baseline,"
                     "tonal_safe_fraction,grid_fraction\n");

        for (size_t treatmentIndex = 0; treatmentIndex < treatments.size(); ++treatmentIndex)
        {
            const auto& treatment = treatments[treatmentIndex];
            const auto& stats = allStats[treatmentIndex];
            const double tonalFraction = stats.totalNotes > 0
                ? (double) stats.tonalSafeNotes / (double) stats.totalNotes : 0.0;
            const double gridFraction = stats.totalNotes > 0
                ? (double) stats.gridNotes / (double) stats.totalNotes : 0.0;

            for (size_t layer = 0; layer < midiforge::corpus::LayerCount; ++layer)
                for (size_t metric = 0; metric < midiforge::corpus::MetricCount; ++metric)
                {
                    const auto& samples = stats.values[layer][metric];
                    const auto& target = targets[layer][metric];
                    const bool hasSamples = ! samples.empty();
                    const double bankMedian = hasSamples
                        ? midiforge::corpus::median (samples) : 0.0;
                    const bool hasBaseline = baselineValid[layer][metric];
                    const double delta = hasSamples && hasBaseline
                        ? bankMedian - baselineMedians[layer][metric] : 0.0;
                    const bool outsideIqr = hasSamples && target.hasIqr
                        && (bankMedian < target.iqrLow || bankMedian > target.iqrHigh);
                    const double deviation = hasSamples && target.hasIqr
                        ? (bankMedian - target.median) / (target.iqrHigh - target.iqrLow) : 0.0;

                    csv << treatment.name << ',' << treatment.category << ',' << seedCount << ','
                        << samples.size() << ',' << midiforge::corpus::layerNames[layer] << ','
                        << midiforge::corpus::metricNames[metric] << ',';
                    if (hasSamples) csv << std::fixed << std::setprecision (6) << bankMedian;
                    else csv << "NA";
                    csv << ',' << std::fixed << std::setprecision (6) << target.median << ',';
                    if (target.hasIqr)
                        csv << target.iqrLow << ',' << target.iqrHigh << ','
                            << (hasSamples ? std::to_string (deviation) : "NA") << ','
                            << (hasSamples ? (outsideIqr ? "true" : "false") : "NA");
                    else
                        csv << "NA,NA,NA,NA";
                    csv << ',' << (hasSamples && hasBaseline ? std::to_string (delta) : "NA")
                        << ',' << std::fixed << std::setprecision (6) << tonalFraction
                        << ',' << gridFraction << ',' << stats.totalNotes << '\n';

                    std::printf ("%s,%s,%s,%zu,", treatment.name,
                        midiforge::corpus::layerNames[layer],
                        midiforge::corpus::metricNames[metric], samples.size());
                    if (hasSamples) std::printf ("%.6f", bankMedian);
                    else std::printf ("NA");
                    std::printf (",%.6f,", target.median);
                    if (target.hasIqr)
                        std::printf ("%.6f,%.6f,", target.iqrLow, target.iqrHigh);
                    else
                        std::printf ("NA,NA,");
                    if (hasSamples && target.hasIqr)
                        std::printf ("%.6f,%s,", deviation, outsideIqr ? "true" : "false");
                    else
                        std::printf ("NA,NA,");
                    if (hasSamples && hasBaseline) std::printf ("%.6f", delta);
                    else std::printf ("NA");
                    std::printf (",%.6f,%.6f\n", tonalFraction, gridFraction);
                }
        }

        csv.close();
        reportRoot.deleteRecursively();
        midiforge::corpus::clearPipelineTraceContext();
        if (! pipelineFile.existsAsFile() || pipelineFile.getSize() < 100)
        {
            std::fprintf (stderr, "Corpus pipeline trace was not written: %s\n",
                          pipelineFile.getFullPathName().toRawUTF8());
            return 2;
        }
        std::printf ("Corpus candidate pipeline trace: %s (pool vs selected bank, quality percentiles, layer eligibility >=50 notes)\n",
                     pipelineFile.getFullPathName().toRawUTF8());
        printBaselinePipelineTraceSummary (pipelineFile);
        std::printf ("Corpus layer ablation report: %s (%d seeds x %d treatments; %d summary rows)\n",
                     outputFile.getFullPathName().toRawUTF8(), seedCount,
                     (int) treatments.size(),
                     (int) (treatments.size() * midiforge::corpus::LayerCount
                            * midiforge::corpus::MetricCount));
        return 0;
    }

    bool exportCalibrationClip (int fixedSeed,
                                const AblationTreatment& treatment,
                                const juce::File& outputFile,
                                const juce::File& settingsRoot)
    {
        if (outputFile.existsAsFile())
            outputFile.deleteFile();

        midiforge::qa::setActiveAblation ("");
        const auto safeName = juce::String (treatment.name).replaceCharacter (':', '_');
        const auto runSettings = settingsRoot.getChildFile (
            safeName + "_" + juce::String (fixedSeed));
        runSettings.deleteRecursively();
        if (! runSettings.createDirectory())
            return false;

        MidiForgeAudioProcessor::setSettingsDirectoryOverride (runSettings);
        bool exported = false;
        {
            MidiForgeAudioProcessor processor;
            processor.setFeedbackLogFile (juce::File());
            processor.setTasteEnabled (true);
            processor.setSeed (fixedSeed);
            processor.waitForGeneration();

            // Give every condition the same deterministic synthetic taste context.
            processor.trainTaste (0, 1.0f, 1.0f);
            processor.trainTaste (7, 0.0f, 1.0f);
            midiforge::qa::setActiveAblation (treatment.target);
            if (treatment.disablesTaste)
                processor.setTasteEnabled (false);

            processor.magicRandomize();
            processor.waitForGeneration();
            if (processor.getVariationCount() == 8)
            {
                // Calibration is specifically about the first-ranked MAGIC pick.
                processor.chooseVariation (0);
                exported = processor.exportMidi (outputFile);
            }
        }

        midiforge::qa::setActiveAblation ("");
        runSettings.deleteRecursively();
        return exported && outputFile.existsAsFile() && outputFile.getSize() > 0;
    }

    bool midiFilesAreIdentical (const juce::File& firstFile,
                                const juce::File& secondFile,
                                bool& readable)
    {
        juce::MemoryBlock firstData, secondData;
        readable = firstFile.loadFileAsData (firstData)
            && secondFile.loadFileAsData (secondData);
        if (! readable)
            return false;

        return firstData.getSize() == secondData.getSize()
            && std::memcmp (firstData.getData(), secondData.getData(), firstData.getSize()) == 0;
    }

    int runCalibrationPack (int seedCount, const juce::File& outputDirectory)
    {
        seedCount = juce::jlimit (1, 32, seedCount);
        if (! outputDirectory.exists() && ! outputDirectory.createDirectory())
        {
            std::fprintf (stderr, "Could not create calibration directory: %s\n",
                          outputDirectory.getFullPathName().toRawUTF8());
            return 2;
        }

        const auto midiDirectory = outputDirectory.getChildFile ("midi");
        if (midiDirectory.exists() && ! midiDirectory.deleteRecursively())
        {
            std::fprintf (stderr, "Could not clear old calibration MIDI files\n");
            return 2;
        }
        if (! midiDirectory.createDirectory())
        {
            std::fprintf (stderr, "Could not create calibration MIDI folder\n");
            return 2;
        }

        std::ofstream ratings (outputDirectory.getChildFile ("ratings-template.csv")
                                   .getFullPathName().toStdString(),
                               std::ios::out | std::ios::trunc);
        std::ofstream key (outputDirectory.getChildFile ("answer-key.csv")
                               .getFullPathName().toStdString(),
                           std::ios::out | std::ios::trunc);
        std::ofstream audit (outputDirectory.getChildFile ("comparison-audit.csv")
                                 .getFullPathName().toStdString(),
                             std::ios::out | std::ios::trunc);
        if (! ratings.is_open() || ! key.is_open() || ! audit.is_open())
        {
            std::fprintf (stderr, "Could not create calibration CSV files\n");
            return 2;
        }
        ratings << "pair_id,clip_a,clip_b,preference_A_B_or_TIE,quality_a_1_to_5,"
                   "quality_b_1_to_5,confidence_1_to_3,notes\n";
        key << "pair_id,seed,treatment,variation_slot_1_based,clip_a,clip_a_condition,"
               "clip_b,clip_b_condition\n";
        audit << "seed,treatment,result,pair_id,details\n";

        // All nine scores changed the selected MIDI bank in both seeds of the first pilot.
        // This batch checks the first ranked pick only, never a later matching slot.
        const std::array<const char*, 9> selectedNames {{
            "melodyPleasantnessScore",
            "melodyExpressionScore",
            "harmonicIntelligenceScore",
            "melodicProsodyScore",
            "creativeRangeScore",
            "motifMemoryScore",
            "grooveQualityScore",
            "localMelodyQualityScore",
            "phraseContrastScore"
        }};
        const auto allTreatments = ablationTreatments();
        const auto baselineIt = std::find_if (allTreatments.begin(), allTreatments.end(),
            [] (const AblationTreatment& treatment)
            {
                return std::string (treatment.name) == "Baseline";
            });
        if (baselineIt == allTreatments.end())
        {
            std::fprintf (stderr, "Calibration baseline condition is missing\n");
            return 2;
        }
        const auto baseline = *baselineIt;
        std::vector<AblationTreatment> selectedTreatments;
        for (const auto* selectedName : selectedNames)
        {
            const auto found = std::find_if (allTreatments.begin(), allTreatments.end(),
                [selectedName] (const AblationTreatment& treatment)
                {
                    return std::string (treatment.name) == selectedName;
                });
            if (found == allTreatments.end())
            {
                std::fprintf (stderr, "Calibration treatment is missing: %s\n", selectedName);
                return 2;
            }
            selectedTreatments.push_back (*found);
        }

        const auto tempRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("midiforge_calibration_pack");
        tempRoot.deleteRecursively();
        if (! tempRoot.createDirectory())
        {
            std::fprintf (stderr, "Could not create calibration temporary directory\n");
            return 2;
        }
        const auto settingsRoot = tempRoot.getChildFile ("settings");
        if (! settingsRoot.createDirectory())
        {
            tempRoot.deleteRecursively();
            return 2;
        }

        int pairIndex = 0;
        int unchangedCount = 0;
        int attemptedCount = 0;
        bool success = true;
        for (int seedIndex = 0; seedIndex < seedCount && success; ++seedIndex)
        {
            const int fixedSeed = 108000 + seedIndex;
            const auto baselineMidi = tempRoot.getChildFile (
                "baseline_" + juce::String (fixedSeed) + ".mid");
            success = exportCalibrationClip (fixedSeed, baseline, baselineMidi, settingsRoot);
            if (! success)
            {
                std::fprintf (stderr, "Failed to export baseline first pick for seed %d\n", fixedSeed);
                break;
            }

            for (int treatmentIndex = 0;
                 treatmentIndex < (int) selectedTreatments.size() && success;
                 ++treatmentIndex)
            {
                const auto& treatment = selectedTreatments[(size_t) treatmentIndex];
                const auto candidateMidi = tempRoot.getChildFile (
                    "candidate_" + juce::String (fixedSeed) + "_"
                    + juce::String (treatment.name) + ".mid");
                success = exportCalibrationClip (fixedSeed, treatment, candidateMidi, settingsRoot);
                if (! success)
                {
                    std::fprintf (stderr, "Failed to export first pick for %s, seed %d\n",
                                  treatment.name, fixedSeed);
                    break;
                }

                ++attemptedCount;
                bool readable = false;
                const bool identical = midiFilesAreIdentical (baselineMidi, candidateMidi, readable);
                if (! readable)
                {
                    std::fprintf (stderr, "Could not read MIDI files for %s, seed %d\n",
                                  treatment.name, fixedSeed);
                    success = false;
                    break;
                }
                if (identical)
                {
                    ++unchangedCount;
                    audit << fixedSeed << ',' << treatment.name
                          << ",NO_CHANGE_FIRST_PICK,,First-ranked MIDI is identical\n";
                    candidateMidi.deleteFile();
                    continue;
                }

                const int nextPair = pairIndex + 1;
                std::ostringstream idStream;
                idStream << "P" << std::setw (3) << std::setfill ('0') << nextPair;
                const auto pairId = idStream.str();
                const auto clipAName = "clip_" + pairId + "_A.mid";
                const auto clipBName = "clip_" + pairId + "_B.mid";
                const auto clipA = midiDirectory.getChildFile (clipAName);
                const auto clipB = midiDirectory.getChildFile (clipBName);

                // Blind order alternates; condition names are written only to the separate key.
                const bool baselineIsA = ((seedIndex + treatmentIndex) % 2) == 0;
                const auto& sourceA = baselineIsA ? baselineMidi : candidateMidi;
                const auto& sourceB = baselineIsA ? candidateMidi : baselineMidi;
                if (! sourceA.copyFileTo (clipA) || ! sourceB.copyFileTo (clipB))
                {
                    std::fprintf (stderr, "Failed to copy blind pair %s\n", pairId.c_str());
                    success = false;
                    break;
                }
                bool copiedFilesReadable = false;
                const bool copiedFilesIdentical =
                    midiFilesAreIdentical (clipA, clipB, copiedFilesReadable);
                if (! copiedFilesReadable || copiedFilesIdentical)
                {
                    std::fprintf (stderr, "Invalid or identical MIDI data in pair %s\n", pairId.c_str());
                    clipA.deleteFile();
                    clipB.deleteFile();
                    success = false;
                    break;
                }

                pairIndex = nextPair;
                ratings << pairId << ",midi/" << clipAName << ",midi/" << clipBName
                        << ",,,,,\n";
                key << pairId << ',' << fixedSeed << ',' << treatment.name << ','
                    << "1,midi/" << clipAName << ','
                    << (baselineIsA ? "Baseline" : treatment.name) << ','
                    << "midi/" << clipBName << ','
                    << (baselineIsA ? treatment.name : "Baseline") << '\n';
                audit << fixedSeed << ',' << treatment.name
                      << ",PAIR_CREATED," << pairId << ",First-ranked MIDI differs\n";
                candidateMidi.deleteFile();
            }
            baselineMidi.deleteFile();
        }

        ratings.close();
        key.close();
        audit.close();
        tempRoot.deleteRecursively();
        if (! success)
            return 2;

        const juce::String instructions =
            juce::String ("MIDI Forge - first-pick blind listening pilot\n\n")
            + juce::String (pairIndex) + " valid A/B pairs from "
            + juce::String (attemptedCount)
            + " fixed-seed comparisons. Only the first-ranked MAGIC result is tested.\n\n"
            + "Open ratings-template.csv and keep answer-key.csv closed until ratings are complete.\n"
            + "In FL Studio, import A and B from each pair into the same project. Use the same instrument, "
              "preset, mixer chain and volume. Choose A, B or TIE, rate both from 1 (weak) to 5 (strong), "
              "and set confidence from 1 to 3. Judge phrasing, rhythm, memorability, naturalness and "
              "whether you would keep the melody. Do not rate which one is louder.\n"
            + juce::String (unchangedCount)
            + " comparisons did not change the first-ranked MIDI and were excluded from the rating template; "
              "see comparison-audit.csv.\n\n"
              "This is a small pilot, not enough on its own to justify changing production score weights. "
              "Every pair was checked for distinct MIDI data. Production generation and score weights are unchanged.\n";
        if (! outputDirectory.getChildFile ("README.txt").replaceWithText (instructions))
        {
            std::fprintf (stderr, "Could not write calibration instructions\n");
            return 2;
        }

        std::printf ("Calibration pack: %s (%d seeds, %d attempted comparisons, %d blind pairs, "
                     "%d unchanged first picks, %d MIDI clips)\n",
                     outputDirectory.getFullPathName().toRawUTF8(), seedCount, attemptedCount,
                     pairIndex, unchangedCount, pairIndex * 2);
        return pairIndex > 0 ? 0 : 3;
    }

    bool validateAblationSwitches()
    {
        const float untouched = midiforge::qa::scoreOrZero ("test-only-score", 0.75f);
        midiforge::qa::setActiveAblation ("test-only-score");
        const float disabled = midiforge::qa::scoreOrZero ("test-only-score", 0.75f);
        midiforge::qa::setActiveAblation ("");
        const midiforge::ComposerJudge::Metrics metrics {
            0.82f, 0.64f, 0.77f, 0.71f, 0.75f, 0.48f, 0.69f,
            0.78f, 0.73f, 0.81f, 0.66f, 0.74f, 0.62f
        };
        const float judgeBaseline = midiforge::ComposerJudge::score (metrics);
        midiforge::qa::setActiveAblation ("judge:idea");
        const float judgeAblated = midiforge::ComposerJudge::score (metrics);
        midiforge::qa::setActiveAblation ("");
        return std::abs (untouched - 0.75f) < 0.00001f
            && std::abs (disabled) < 0.00001f
            && std::abs (judgeBaseline - judgeAblated) > 0.00001f;
    }

bool validateCorpusMetricDefinitions()
{
    std::vector<Note> notes;
    const std::array<int, 18> deltas {{
        1, 2, 3, 4, 5, 6,
        1, 2, 3, 4, 5, 6,
        1, 2, 3, 4, 5, 7
    }};
    int step = 0;
    int pitch = 60;
    notes.push_back ({ step, 2, pitch, 90, 3 });
    for (const int delta : deltas)
    {
        step += 2;
        pitch += delta;
        notes.push_back ({ step, 2, pitch, 90, 3 });
    }

    // Pitch bands are authoritative, not the plugin's logical layer/channel.
    notes.push_back ({ 6, 2, 60, 90, 2 }); // top layer, but below the onset's line note
    notes.push_back ({ 0, 2, 47, 90, 3 }); // bass
    notes.push_back ({ 2, 2, 48, 90, 2 }); // riff lower boundary
    notes.push_back ({ 4, 2, 59, 90, 2 }); // riff upper boundary

    // All drum channel representations must be excluded from the layer line.
    notes.push_back ({ 8, 2, 127, 90, 5 });
    notes.push_back ({ 10, 2, 120, 90, 9 });
    notes.push_back ({ 12, 2, 118, 90, 10 });

    const auto measured = midiforge::corpus::measure (notes, 3);
    const std::vector<Note> oneWindowNotes (notes.begin(), notes.begin() + 7);
    const auto oneWindowMeasured = midiforge::corpus::measure (oneWindowNotes, 3);
    const auto& oneWindowTop = oneWindowMeasured[midiforge::corpus::Top];
    const auto& top = measured[midiforge::corpus::Top];
    const auto& riff = measured[midiforge::corpus::Riff];
    const auto& bass = measured[midiforge::corpus::Bass];
    const double third = 1.0 / 3.0;

    return top.noteCount == 20 && riff.noteCount == 2 && bass.noteCount == 1
        && top.valid[midiforge::corpus::StepsLe2]
        && top.valid[midiforge::corpus::LeapsGe5]
        && top.valid[midiforge::corpus::WindowRepeat]
        && top.valid[midiforge::corpus::DurationBeats]
        && top.valid[midiforge::corpus::NotesPerBar]
        && std::abs (top.value[midiforge::corpus::StepsLe2] - third) < 0.000001
        && std::abs (top.value[midiforge::corpus::LeapsGe5] - third) < 0.000001
        && std::abs (top.value[midiforge::corpus::WindowRepeat] - (6.0 / 13.0)) < 0.000001
        && top.repeatProbeValid[midiforge::corpus::Repeat6SlidingClipped]
        && top.repeatProbeValid[midiforge::corpus::Repeat4NonOverlapClipped]
        && top.repeatProbeValid[midiforge::corpus::Repeat4SlidingClipped]
        && top.repeatProbeValid[midiforge::corpus::Repeat6NonOverlapRaw]
        && std::abs (top.repeatProbeValue[midiforge::corpus::Repeat6SlidingClipped] - (6.0 / 13.0)) < 0.000001
        && std::abs (top.repeatProbeValue[midiforge::corpus::Repeat4NonOverlapClipped] - 0.25) < 0.000001
        && top.repeatProbeValue[midiforge::corpus::Repeat4SlidingClipped] > 0.0
        && std::abs (top.repeatProbeValue[midiforge::corpus::Repeat6NonOverlapRaw] - third) < 0.000001
        && oneWindowTop.valid[midiforge::corpus::WindowRepeat]
        && std::abs (oneWindowTop.value[midiforge::corpus::WindowRepeat]) < 0.000001
        && ! oneWindowTop.repeatProbeValid[midiforge::corpus::Repeat6SlidingClipped]
        && ! oneWindowTop.repeatProbeValid[midiforge::corpus::Repeat4NonOverlapClipped]
        && oneWindowTop.repeatProbeValid[midiforge::corpus::Repeat4SlidingClipped]
        && ! oneWindowTop.repeatProbeValid[midiforge::corpus::Repeat6NonOverlapRaw]
        && std::abs (top.value[midiforge::corpus::DurationBeats] - 0.5) < 0.000001
        && std::abs (top.value[midiforge::corpus::NotesPerBar] - 8.0) < 0.000001;
}

int runMemorabilityReport (int seedCount, const juce::File& outputFile);
int runMemorabilityPack (const juce::File& csvFile, const juce::File& outputDirectory);
bool validateMemorabilityProbe();

int main (int argc, char** argv)
{
    if (argc >= 2 && std::string (argv[1]) == "--memorability-report")
    {
        const int seedCount = argc >= 3 ? std::max (1, std::atoi (argv[2])) : 256;
        const auto output = argc >= 4 ? juce::File (argv[3])
            : juce::File::getCurrentWorkingDirectory().getChildFile ("baseline/memorability-baseline-256.csv");
        return runMemorabilityReport (seedCount, output);
    }
    if (argc >= 2 && std::string (argv[1]) == "--memorability-pack")
    {
        if (argc < 4) return 2;
        return runMemorabilityPack (juce::File (argv[2]), juce::File (argv[3]));
    }
    if (argc >= 2 && std::string (argv[1]) == "--calibration-pack")
    {
        const int seedCount = argc >= 3 ? std::max (1, std::atoi (argv[2])) : 8;
        const auto output = argc >= 4 ? juce::File (argv[3])
            : juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-pack");
        return runCalibrationPack (seedCount, output);
    }

    if (argc >= 2 && std::string (argv[1]) == "--ablation-report")
    {
        const int seedCount = argc >= 3 ? std::max (1, std::atoi (argv[2])) : 256;
        const auto output = argc >= 4 ? juce::File (argv[3])
            : juce::File::getCurrentWorkingDirectory().getChildFile ("ablation-report.csv");
        return runAblationReport (seedCount, output);
    }

    if (argc >= 2 && std::string (argv[1]) == "--corpus-layer-ablation")
    {
        const int seedCount = argc >= 3 ? std::max (1, std::atoi (argv[2])) : 64;
        const auto output = argc >= 4 ? juce::File (argv[3])
            : juce::File::getCurrentWorkingDirectory().getChildFile ("corpus-layer-ablation.csv");
        return runCorpusLayerAblationReport (seedCount, output);
    }

    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_0_109_0_release_qa");
    settings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (settings);

    check ("Memorability probe unit checks", validateMemorabilityProbe(),
           "known repeated hook, no-repeat melody, exact returns and unsafe loop ending");
    check ("Headless-only ablation switches are isolated", validateAblationSwitches(),
           "default scores unchanged; selected score and Composer Judge group can be excluded");
    check ("Corpus layer metrics follow the supplied definitions",
           validateCorpusMetricDefinitions(),
           "pitch bands, highest onset note, non-overlapping six-interval windows, duration and bars");

    bool allSupportedLayers = true;
    bool allInLoop = true;
    bool allOnGrid = true;
    bool allNoCollisions = true;
    bool allSpacingSafe = true;
    bool allTonal = true;
    bool allRegisterSafe = true;
    bool allLeapsSafe = true;
    bool allLoopSeamsSafe = true;
    bool allConstraintsPreserved = true;
    bool allPerformancePreserved = true;
    bool allLayerTogglesStayOn = true;
    int checkedSlots = 0;
    int checkedNotes = 0;
    int checkedMelodyNotes = 0;
    int checkedChordNotes = 0;
    int checkedBassNotes = 0;
    int severeLeapReversals = 0;
    int checkedHarmonyAnchors = 0;
    int matchedHarmonyAnchors = 0;
    int diverseBanks = 0;
    int diverseContourBanks = 0;
    int motifReturnVariations = 0;

    // Exercise all declared scales with eight selected candidates per scale.
    for (int scale = 0; scale < 12; ++scale)
    {
        MidiForgeAudioProcessor p;
        p.setFeedbackLogFile (juce::File());
        const int root = (scale * 5 + 7) % 12;
        p.setRoot (root);
        p.setScale (scale);
        p.setBars (4);
        p.setSeed (105100 + scale * 97);
        p.setSwing (0.12f);
        p.setHumanize (0.23f);
        p.setHumanizeEnabled (true);
        // Keep the restored layers on while stress-testing every scale and MAGIC slot.
        p.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ChordsEnabled, true);
        p.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::BassEnabled, true);
        p.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::RootKey, true);
        p.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ScaleMode, true);

        p.magicRandomize();
        p.waitForGeneration();

        allConstraintsPreserved = allConstraintsPreserved
            && p.getRoot() == root && p.getScale() == scale && p.getVisibleBars() == 4;
        allPerformancePreserved = allPerformancePreserved
            && std::abs (p.getSwing() - 0.12f) < 0.001f
            && std::abs (p.getHumanize() - 0.23f) < 0.001f
            && p.isHumanizeEnabled();
        allLayerTogglesStayOn = allLayerTogglesStayOn
            && p.isChordsEnabled() && p.isBassEnabled()
            && p.isMelodyEnabled() && ! p.isArpEnabled() && ! p.isDrumsEnabled()
            && ! p.getLeadStyleSoundCloud();

        std::set<std::string> bankFingerprints;
        std::set<std::string> bankContourFingerprints;
        const int slots = p.getVariationCount();
        for (int v = 0; v < slots; ++v)
        {
            p.chooseVariation (v);
            const auto notes = p.getVisibleNotes();
            if (hasMotifReturn (notes, p.getVisibleBars()))
                ++motifReturnVariations;
            const auto c = inspectMelody (notes, p.getVisibleBars(), root, scale);
            ++checkedSlots;
            checkedNotes += c.noteCount;
            checkedMelodyNotes += c.melodyNotes;
            checkedChordNotes += c.chordNotes;
            checkedBassNotes += c.bassNotes;
            severeLeapReversals += c.severeLeapReversals;
            const auto harmony = inspectCurrentChordFit (notes, p.getVisibleBars());
            checkedHarmonyAnchors += harmony.structuralAnchors;
            matchedHarmonyAnchors += harmony.chordToneAnchors;
            allSupportedLayers = allSupportedLayers && c.populated && c.supportedLayers
                && c.chordNotes > 0 && c.bassNotes > 0;
            allInLoop = allInLoop && c.inLoop;
            allOnGrid = allOnGrid && c.onGrid;
            allNoCollisions = allNoCollisions && c.noCollisions;
            allSpacingSafe = allSpacingSafe && c.spacing;
            allTonal = allTonal && c.tonal;
            allRegisterSafe = allRegisterSafe && c.registerSafe;
            allLeapsSafe = allLeapsSafe && c.leapsSafe;
            allLoopSeamsSafe = allLoopSeamsSafe && c.seamSafe;
            bankFingerprints.insert (fingerprint (notes));
            bankContourFingerprints.insert (contourFingerprint (notes));
        }
        if (slots == 8 && bankFingerprints.size() >= 6)
            ++diverseBanks;
        if (slots == 8 && bankContourFingerprints.size() >= 5)
            ++diverseContourBanks;
    }

    check ("MAGIC produces melody, chords and bass",
          allSupportedLayers && checkedSlots == 96,
          juce::String (checkedSlots) + " variations; "
              + juce::String (checkedMelodyNotes) + " melody, "
              + juce::String (checkedChordNotes) + " chord and "
              + juce::String (checkedBassNotes) + " bass notes");
    check ("MAGIC preserves creator Key / Scale / Bars",
          allConstraintsPreserved, "all 12 scales with four-bar loops");
    check ("Generated melody follows the eighth-note grid",
          allOnGrid && allNoCollisions && allSpacingSafe,
          "no off-grid, duplicate or one-sixteenth-spaced attacks");
    check ("Notes and note tails stay inside the loop",
          allInLoop, "valid step, length, note and velocity bounds");
    check ("All generated layers remain in key",
          allTonal && checkedNotes > 0, juce::String (checkedNotes) + " notes checked");
    check ("Melody stays inside the grounded register",
          allRegisterSafe && checkedMelodyNotes > 0, "MIDI pitches 48–90");
    check ("Melody avoids unsafe pitch leaps",
          allLeapsSafe && checkedMelodyNotes > 0, "adjacent pitches differ by at most 12 semitones");
    check ("Loop seam stays within a ninth",
          allLoopSeamsSafe && checkedMelodyNotes > 0,
          "first and last melody notes differ by at most 9 semitones");
    check ("Structural melody notes follow the active chord",
          checkedHarmonyAnchors > 0
              && (double) matchedHarmonyAnchors / (double) checkedHarmonyAnchors >= 0.72,
          juce::String (matchedHarmonyAnchors) + "/" + juce::String (checkedHarmonyAnchors)
              + " strong/long melody notes are chord tones in the active chord");
    check ("MAGIC limits abrupt large-leap reversals",
          severeLeapReversals <= 14,
          juce::String (severeLeapReversals)
              + " opposing leap pairs across 96 generated variations");
    check ("MAGIC preserves explicit performance settings",
          allPerformancePreserved, "Swing and Humanize remain user-controlled");
    check ("MAGIC respects locked CHORDS / BASS switches",
          allLayerTogglesStayOn, "both switches stay on through all 12 MAGIC runs");
    check ("MAGIC variation bank retains meaningful diversity",
          diverseBanks >= 10,
          juce::String (diverseBanks) + " of 12 banks have at least six distinct ideas");
    check ("MAGIC varies melodic contour, not just absolute pitch",
          diverseContourBanks >= 10,
          juce::String (diverseContourBanks)
              + " of 12 banks have at least five transposition-normalized contours");
    check ("MAGIC returns a short melodic motif",
          motifReturnVariations > 0,
          juce::String (motifReturnVariations)
              + " of 96 selected variations contain a matching opening-to-ending contour");

    // Mood should create a repeatable difference in melodic intent across seeds/archetypes,
    // rather than being washed out by hidden Character variation.
    {
        double aggressiveSpace = 0.0, aggressiveDensity = 0.0, aggressiveLeap = 0.0;
        double dreamySpace = 0.0, dreamyDensity = 0.0, dreamyLeap = 0.0;
        double nostalgicMotif = 0.0, energeticMotif = 0.0;
        int samples = 0;
        for (int identity = 0; identity < 32; ++identity)
        {
            for (int archetype = 0; archetype < 8; ++archetype)
            {
                const auto seed = (uint32_t) (0x61C88647u * (uint32_t) (identity + 1)
                                               ^ (uint32_t) archetype * 0x9E3779B9u);
                const auto aggressive = midiforge::MelodyIntent::makePlan (
                    4, 0, 4, 0.65f, 0.55f, seed, archetype);
                const auto dreamy = midiforge::MelodyIntent::makePlan (
                    4, 0, 5, 0.65f, 0.55f, seed, archetype);
                const auto nostalgic = midiforge::MelodyIntent::makePlan (
                    4, 0, 6, 0.65f, 0.55f, seed, archetype);
                const auto energetic = midiforge::MelodyIntent::makePlan (
                    4, 0, 8, 0.65f, 0.55f, seed, archetype);

                aggressiveSpace += aggressive.dnaSpace;
                aggressiveDensity += aggressive.dnaDensity;
                aggressiveLeap += aggressive.dnaLeap;
                dreamySpace += dreamy.dnaSpace;
                dreamyDensity += dreamy.dnaDensity;
                dreamyLeap += dreamy.dnaLeap;
                nostalgicMotif += nostalgic.dnaMotif;
                energeticMotif += energetic.dnaMotif;
                ++samples;
            }
        }
        const double aggressiveSpaceMean = aggressiveSpace / samples;
        const double aggressiveDensityMean = aggressiveDensity / samples;
        const double aggressiveLeapMean = aggressiveLeap / samples;
        const double dreamySpaceMean = dreamySpace / samples;
        const double dreamyDensityMean = dreamyDensity / samples;
        const double dreamyLeapMean = dreamyLeap / samples;
        const double nostalgicMotifMean = nostalgicMotif / samples;
        const double energeticMotifMean = energeticMotif / samples;
        const bool moodContrast =
            dreamySpaceMean - aggressiveSpaceMean >= 0.36
            && aggressiveDensityMean - dreamyDensityMean >= 0.32
            && aggressiveLeapMean - dreamyLeapMean >= 0.45
            && nostalgicMotifMean - energeticMotifMean >= 0.08;
        check ("Mood intent reaches phrase spacing, density, leaps and motif",
               moodContrast,
               juce::String ("Dreamy−Aggressive space ")
                   + juce::String (dreamySpaceMean - aggressiveSpaceMean, 2)
                   + ", density "
                   + juce::String (aggressiveDensityMean - dreamyDensityMean, 2)
                   + ", leap "
                   + juce::String (aggressiveLeapMean - dreamyLeapMean, 2)
                   + ", Nostalgic−Energetic motif "
                   + juce::String (nostalgicMotifMean - energeticMotifMean, 2));
    }

    // MAGIC reproducibility contract: identical project state and press history
    // must produce equivalent MIDI events in all eight generated variation slots.
    // First CI run publishes candidate fingerprints; fixed golden values are added
    // only after those checks have been observed from the actual Windows/Linux toolchain.
    {
        constexpr int fixedSeed = 108107;
        MidiForgeAudioProcessor first;
        MidiForgeAudioProcessor peer;
        first.setFeedbackLogFile (juce::File());
        peer.setFeedbackLogFile (juce::File());
        first.setTasteEnabled (false);
        peer.setTasteEnabled (false);
        // Keep this frozen MIDI golden independent of MAGIC's new key/mode rolls.
        first.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::RootKey, true);
        first.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ScaleMode, true);
        peer.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::RootKey, true);
        peer.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ScaleMode, true);
        first.setSeed (fixedSeed);
        peer.setSeed (fixedSeed);
        first.magicRandomize();
        first.waitForGeneration();
        peer.magicRandomize();
        peer.waitForGeneration();

        const std::string firstBank = midiBankFingerprint (first);
        const std::string peerBank = midiBankFingerprint (peer);
        constexpr const char* expectedFirstBank = "c7187c0d4555c731";
        constexpr const char* expectedNextBankGolden = "9c024c6a843c9c37";
        check ("MAGIC repeats the same complete MIDI bank for identical initial state",
               firstBank == peerBank,
               juce::String ("seed=") + juce::String (fixedSeed) + " fingerprint=" + juce::String (firstBank.c_str()));
        check ("MAGIC first press matches frozen MIDI golden",
               firstBank == expectedFirstBank,
               juce::String ("expected=") + juce::String (expectedFirstBank)
                   + " actual=" + juce::String (firstBank.c_str()));
        std::printf ("MAGIC_GOLDEN_FIRST seed=%d fingerprint=%s\n",
                     fixedSeed, firstBank.c_str());

        juce::MemoryBlock checkpoint;
        first.getStateInformation (checkpoint);
        first.magicRandomize();
        first.waitForGeneration();
        const std::string expectedNextBank = midiBankFingerprint (first);
        check ("MAGIC second press matches frozen MIDI golden",
               expectedNextBank == expectedNextBankGolden,
               juce::String ("expected=") + juce::String (expectedNextBankGolden)
                   + " actual=" + juce::String (expectedNextBank.c_str()));
        std::printf ("MAGIC_GOLDEN_NEXT seed=%d fingerprint=%s\n",
                     fixedSeed, expectedNextBank.c_str());

        MidiForgeAudioProcessor resumed;
        resumed.setFeedbackLogFile (juce::File());
        resumed.setTasteEnabled (false);
        resumed.setStateInformation (checkpoint.getData(), (int) checkpoint.getSize());
        resumed.waitForGeneration();
        resumed.magicRandomize();
        resumed.waitForGeneration();
        const std::string resumedNextBank = midiBankFingerprint (resumed);
        check ("MAGIC sequence resumes after state save/load",
               expectedNextBank == resumedNextBank,
               juce::String ("expected=") + juce::String (expectedNextBank.c_str())
                   + " resumed=" + juce::String (resumedNextBank.c_str()));
    }

    // Repeated unlocked MAGIC presses must sample multiple root keys and both familiar modes.
    {
        MidiForgeAudioProcessor keyModeRolls;
        keyModeRolls.setFeedbackLogFile (juce::File());
        keyModeRolls.setTasteEnabled (false);
        keyModeRolls.setSeed (106901);
        std::set<int> rootsSeen, modesSeen;
        bool validKeyMode = true;
        for (int pass = 0; pass < 12; ++pass)
        {
            keyModeRolls.magicRandomize();
            keyModeRolls.waitForGeneration();
            const int generatedRoot = keyModeRolls.getRoot();
            const int generatedMode = keyModeRolls.getScale();
            rootsSeen.insert (generatedRoot);
            modesSeen.insert (generatedMode);
            validKeyMode = validKeyMode && generatedRoot >= 0 && generatedRoot <= 11
                && (generatedMode == MidiForgeAudioProcessor::Major
                    || generatedMode == MidiForgeAudioProcessor::Minor);
        }
        check ("MAGIC randomizes root keys and major/minor modes",
               validKeyMode && rootsSeen.size() >= 4
                   && modesSeen.count (MidiForgeAudioProcessor::Major) == 1
                   && modesSeen.count (MidiForgeAudioProcessor::Minor) == 1,
               juce::String (rootsSeen.size()) + " distinct roots; "
                   + juce::String (modesSeen.size()) + " modes observed");
    }

    // Every exposed randomizable value must survive MAGIC when its individual lock is on.
    MidiForgeAudioProcessor locked;
    locked.setFeedbackLogFile (juce::File());
    locked.setMagicParameterLockMask ((1u << MidiForgeAudioProcessor::kMagicLockCount) - 1u);
    const int keepProgression = locked.getProgression();
    const int keepRhythm = locked.getRhythm();
    const int keepOctave = locked.getOctave();
    const int keepMood = locked.getMood();
    const int keepMelodyType = locked.getMelodyType();
    const float keepChordDensity = locked.getChordDensity();
    const float keepBassDensity = locked.getBassDensity();
    const float keepMelodyDensity = locked.getMelodyDensity();
    const float keepComplexity = locked.getComplexity();
    const float keepMotif = locked.getMotifStrength();
    const float keepVariation = locked.getVariationAmount();
    const float keepFill = locked.getFillAmount();
    const float keepEnergy = locked.getEnergy();
    const float keepLength = locked.getMelodyLength();
    const float keepPause = locked.getPauseChance();
    const float keepLeap = locked.getLeapChance();
    const float keepGhost = locked.getGhostChance();
    const int keepRoot = locked.getRoot();
    const int keepScale = locked.getScale();
    const bool keepExtensions = locked.getChordExtensions();
    const bool keepInversions = locked.getInversions();
    const bool keepChords = locked.isChordsEnabled();
    const bool keepBass = locked.isBassEnabled();
    locked.magicRandomize();
    locked.waitForGeneration();
    const bool lockContract = locked.getRoot() == keepRoot && locked.getScale() == keepScale
        && locked.getProgression() == keepProgression
        && locked.getRhythm() == keepRhythm && locked.getOctave() == keepOctave
        && locked.getMood() == keepMood && locked.getMelodyType() == keepMelodyType
        && std::abs (locked.getChordDensity() - keepChordDensity) < 0.0001f
        && std::abs (locked.getBassDensity() - keepBassDensity) < 0.0001f
        && std::abs (locked.getMelodyDensity() - keepMelodyDensity) < 0.0001f
        && std::abs (locked.getComplexity() - keepComplexity) < 0.0001f
        && std::abs (locked.getMotifStrength() - keepMotif) < 0.0001f
        && std::abs (locked.getVariationAmount() - keepVariation) < 0.0001f
        && std::abs (locked.getFillAmount() - keepFill) < 0.0001f
        && std::abs (locked.getEnergy() - keepEnergy) < 0.0001f
        && std::abs (locked.getMelodyLength() - keepLength) < 0.0001f
        && std::abs (locked.getPauseChance() - keepPause) < 0.0001f
        && std::abs (locked.getLeapChance() - keepLeap) < 0.0001f
        && std::abs (locked.getGhostChance() - keepGhost) < 0.0001f
        && locked.getChordExtensions() == keepExtensions
        && locked.getInversions() == keepInversions
        && locked.isChordsEnabled() == keepChords
        && locked.isBassEnabled() == keepBass;
    check ("Every parameter lock survives MAGIC", lockContract,
           "all 23 individual lockable values remain unchanged");

    // Locks must preserve OFF as well as ON. Keep the other MAGIC parameters free.
    MidiForgeAudioProcessor lockedOffLayers;
    lockedOffLayers.setFeedbackLogFile (juce::File());
    lockedOffLayers.setChordsEnabled (false);
    lockedOffLayers.setBassEnabled (false);
    lockedOffLayers.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ChordsEnabled, true);
    lockedOffLayers.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::BassEnabled, true);
    for (int pass = 0; pass < 8; ++pass)
    {
        lockedOffLayers.magicRandomize();
        lockedOffLayers.waitForGeneration();
    }
    check ("Locked-OFF CHORDS/BASS switches remain OFF",
           ! lockedOffLayers.isChordsEnabled() && ! lockedOffLayers.isBassEnabled(),
           "both individual locks preserve OFF through eight MAGIC presses");

    // Recreate the serialized layout used by 0.105.x (state v3): v4 added a
    // four-byte MAGIC-lock mask immediately before the saved-note count.
    // The legacy state has both accompaniment flags off and one edited melody note.
    MidiForgeAudioProcessor legacySource;
    legacySource.setFeedbackLogFile (juce::File());
    legacySource.setChordsEnabled (false);
    legacySource.setBassEnabled (false);
    legacySource.replaceVisibleNotes (std::vector<Note> { { 0, 4, 64, 100, 3 } });
    juce::MemoryBlock stateCurrent;
    legacySource.getStateInformation (stateCurrent);
    constexpr size_t legacyMaskOffset = 170;
    constexpr size_t lockMaskEnd = legacyMaskOffset + sizeof (std::int32_t);
    juce::MemoryBlock stateV3;
    bool builtLegacyState = stateCurrent.getSize() >= lockMaskEnd + sizeof (int32_t);
    if (builtLegacyState)
    {
        juce::MemoryOutputStream legacyStream (stateV3, false);
        const auto* bytes = static_cast<const std::uint8_t*> (stateCurrent.getData());
        legacyStream.write (bytes, 4);        // state magic
        legacyStream.writeInt (3);            // legacy state version
        legacyStream.write (bytes + 8, legacyMaskOffset - 8);
        legacyStream.write (bytes + lockMaskEnd, stateCurrent.getSize() - lockMaskEnd);
    }

    MidiForgeAudioProcessor legacyLoaded;
    legacyLoaded.setFeedbackLogFile (juce::File());
    if (builtLegacyState)
        legacyLoaded.setStateInformation (stateV3.getData(), (int) stateV3.getSize());
    legacyLoaded.waitForGeneration();
    const auto migratedNotes = legacyLoaded.getVisibleNotes();
    const bool keptLegacyMelody = std::any_of (migratedNotes.begin(), migratedNotes.end(),
        [] (const Note& n) { return n.channel == 3 && n.step == 0 && n.note == 64; });
    const bool restoredChordLayer = std::any_of (migratedNotes.begin(), migratedNotes.end(),
        [] (const Note& n) { return n.channel == 1; });
    const bool restoredBassLayer = std::any_of (migratedNotes.begin(), migratedNotes.end(),
        [] (const Note& n) { return n.channel == 2; });
    check ("0.105.x state migration restores accompaniment without losing melody",
           builtLegacyState && legacyLoaded.isChordsEnabled() && legacyLoaded.isBassEnabled()
               && keptLegacyMelody && restoredChordLayer && restoredBassLayer,
           juce::String (migratedNotes.size())
               + " notes; old melody retained and chord/bass material restored");

    // Mutations must keep the same musical safety contract as newly authored MIDI.
    MidiForgeAudioProcessor transform;
    transform.setFeedbackLogFile (juce::File());
    transform.setRoot (7);
    transform.setScale (2);
    transform.setBars (4);
    transform.setSeed (105901);
    transform.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::ChordsEnabled, true);
    transform.setMagicParameterLocked (MidiForgeAudioProcessor::MagicLock::BassEnabled, true);
    transform.setMagicParameterLockMask ((1u << MidiForgeAudioProcessor::kMagicLockCount) - 1u);
    // This is a transform invariant test, not a MAGIC test. Regenerate from the
    // explicit seed above so a failure is reproducible instead of time-seeded.
    transform.regenerate();
    transform.waitForGeneration();
    transform.mutateSelected (0.45f);
    auto mutated = inspectMelody (transform.getVisibleNotes(), transform.getVisibleBars(),
                                  transform.getRoot(), transform.getScale());
    transform.evolveSelected();
    auto evolved = inspectMelody (transform.getVisibleNotes(), transform.getVisibleBars(),
                                  transform.getRoot(), transform.getScale());
    const bool transformsValid =
        mutated.populated && mutated.supportedLayers && mutated.inLoop && mutated.onGrid
        && mutated.noCollisions && mutated.spacing && mutated.tonal
        && mutated.registerSafe && mutated.leapsSafe
        && evolved.populated && evolved.supportedLayers && evolved.inLoop && evolved.onGrid
        && evolved.noCollisions && evolved.spacing && evolved.tonal
        && evolved.registerSafe && evolved.leapsSafe;
    auto describeTransformChecks = [] (const MelodyChecks& c)
    {
        return juce::String ("pop=") + (c.populated ? "1" : "0")
            + " lanes=" + (c.supportedLayers ? "1" : "0")
            + " loop=" + (c.inLoop ? "1" : "0")
            + " grid=" + (c.onGrid ? "1" : "0")
            + " collisions=" + (c.noCollisions ? "1" : "0")
            + " spacing=" + (c.spacing ? "1" : "0")
            + " tonal=" + (c.tonal ? "1" : "0")
            + " register=" + (c.registerSafe ? "1" : "0")
            + " leaps=" + (c.leapsSafe ? "1" : "0");
    };
    const auto transformDetail = juce::String ("seed=105901; mutate{")
        + describeTransformChecks (mutated)
        + "} evolve{" + describeTransformChecks (evolved) + "}";
    check ("MUTATE / EVOLVE preserve the melody contract",
          transformsValid, transformDetail);

    // Export and drag-temporary files must contain real MIDI with only the supported layers.
    const auto outFile = settings.getChildFile ("release_export.mid");
    int exportedNotes = 0, exportedChords = 0, exportedBass = 0;
    const bool exportWritten = transform.exportMidiFileTo (outFile);
    const bool exportParsed = exportWritten
        && parseSupportedMidi (outFile, exportedNotes, exportedChords, exportedBass);
    check ("MIDI export contains valid melody + chord/bass tracks and tempo metadata",
          exportParsed, juce::String (exportedNotes) + " notes ("
              + juce::String (exportedChords) + " chord, "
              + juce::String (exportedBass) + " bass)");

    juce::File dragFile = transform.writeTemporaryMidiFile();
    int draggedNotes = 0, draggedChords = 0, draggedBass = 0;
    const bool dragOk = dragFile.existsAsFile() && dragFile.getSize() > 0
        && parseSupportedMidi (dragFile, draggedNotes, draggedChords, draggedBass);
    check ("FL drag payload preserves melody + supported layers",
          dragOk, juce::String (draggedNotes) + " notes ("
              + juce::String (draggedChords) + " chord, "
              + juce::String (draggedBass) + " bass)");
    if (dragFile.existsAsFile()) dragFile.deleteFile();

    // State v5 must preserve the restored layers, MAGIC locks and sequence state.
    juce::MemoryBlock state;
    transform.getStateInformation (state);
    MidiForgeAudioProcessor restored;
    restored.setFeedbackLogFile (juce::File());
    restored.setStateInformation (state.getData(), (int) state.getSize());
    restored.waitForGeneration();
    bool restoredLayerState = restored.isChordsEnabled() && restored.isBassEnabled()
        && restored.isMelodyEnabled() && ! restored.isArpEnabled() && ! restored.isDrumsEnabled()
        && ! restored.getLeadStyleSoundCloud()
        && restored.getMagicParameterLockMask() == transform.getMagicParameterLockMask();
    const auto restoredNotes = restored.getVisibleNotes();
    restoredLayerState = restoredLayerState && ! restoredNotes.empty()
        && std::all_of (restoredNotes.begin(), restoredNotes.end(),
            [] (const Note& n) { return n.channel >= 1 && n.channel <= 3; });
    check ("State round-trip keeps layers and MAGIC locks",
          restoredLayerState, juce::String (restoredNotes.size()) + " supported notes restored");

    std::printf ("\n%s (%d failed check%s)\n",
                 failures == 0 ? "ALL MELODY + BASS/CHORD RELEASE CHECKS PASSED" : "MELODY + BASS/CHORD RELEASE CHECKS FAILED",
                 failures, failures == 1 ? "" : "s");
    outFile.deleteFile();
    settings.getChildFile ("taste.json").deleteFile();
    settings.getChildFile ("feedback.csv").deleteFile();
    return failures == 0 ? 0 : 1;
}
