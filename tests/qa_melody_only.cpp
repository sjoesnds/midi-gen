// MIDI Forge 0.107.0 release checks for the melody + chords + bass creator.
#include "PluginProcessor.h"
#include "MidiForgeAblation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
        return c;
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

int main (int argc, char** argv)
{
    if (argc >= 2 && std::string (argv[1]) == "--ablation-report")
    {
        const int seedCount = argc >= 3 ? std::max (1, std::atoi (argv[2])) : 256;
        const auto output = argc >= 4 ? juce::File (argv[3])
            : juce::File::getCurrentWorkingDirectory().getChildFile ("ablation-report.csv");
        return runAblationReport (seedCount, output);
    }

    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_0_107_0_release_qa");
    settings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (settings);

    check ("Headless-only ablation switches are isolated", validateAblationSwitches(),
           "default scores unchanged; selected score and Composer Judge group can be excluded");

    bool allSupportedLayers = true;
    bool allInLoop = true;
    bool allOnGrid = true;
    bool allNoCollisions = true;
    bool allSpacingSafe = true;
    bool allTonal = true;
    bool allRegisterSafe = true;
    bool allLeapsSafe = true;
    bool allConstraintsPreserved = true;
    bool allPerformancePreserved = true;
    bool allLayerTogglesStayOn = true;
    int checkedSlots = 0;
    int checkedNotes = 0;
    int checkedMelodyNotes = 0;
    int checkedChordNotes = 0;
    int checkedBassNotes = 0;
    int severeLeapReversals = 0;
    int diverseBanks = 0;

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
        const int slots = p.getVariationCount();
        for (int v = 0; v < slots; ++v)
        {
            p.chooseVariation (v);
            const auto notes = p.getVisibleNotes();
            const auto c = inspectMelody (notes, p.getVisibleBars(), root, scale);
            ++checkedSlots;
            checkedNotes += c.noteCount;
            checkedMelodyNotes += c.melodyNotes;
            checkedChordNotes += c.chordNotes;
            checkedBassNotes += c.bassNotes;
            severeLeapReversals += c.severeLeapReversals;
            allSupportedLayers = allSupportedLayers && c.populated && c.supportedLayers
                && c.chordNotes > 0 && c.bassNotes > 0;
            allInLoop = allInLoop && c.inLoop;
            allOnGrid = allOnGrid && c.onGrid;
            allNoCollisions = allNoCollisions && c.noCollisions;
            allSpacingSafe = allSpacingSafe && c.spacing;
            allTonal = allTonal && c.tonal;
            allRegisterSafe = allRegisterSafe && c.registerSafe;
            allLeapsSafe = allLeapsSafe && c.leapsSafe;
            bankFingerprints.insert (fingerprint (notes));
        }
        if (slots == 8 && bankFingerprints.size() >= 6)
            ++diverseBanks;
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
        first.setSeed (fixedSeed);
        peer.setSeed (fixedSeed);
        first.magicRandomize();
        first.waitForGeneration();
        peer.magicRandomize();
        peer.waitForGeneration();

        const std::string firstBank = midiBankFingerprint (first);
        const std::string peerBank = midiBankFingerprint (peer);
        constexpr const char* expectedFirstBank = "32f2a094f35d8174";
        constexpr const char* expectedNextBankGolden = "f9b3b2e6969582dd";
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
    const bool keepExtensions = locked.getChordExtensions();
    const bool keepInversions = locked.getInversions();
    const bool keepChords = locked.isChordsEnabled();
    const bool keepBass = locked.isBassEnabled();
    locked.magicRandomize();
    locked.waitForGeneration();
    const bool lockContract = locked.getProgression() == keepProgression
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
           "all 21 individual lockable values remain unchanged");

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
