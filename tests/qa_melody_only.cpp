// MIDI Forge 0.105.1 release checks for the supported melody-only product.
#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
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
        bool onlyMelody = true;
        bool inLoop = true;
        bool onGrid = true;
        bool noCollisions = true;
        bool spacing = true;
        bool tonal = true;
        bool registerSafe = true;
        bool leapsSafe = true;
        int noteCount = 0;
    };

    MelodyChecks inspectMelody (const std::vector<Note>& notes,
                                int bars, int root, int scale)
    {
        MelodyChecks c;
        c.populated = ! notes.empty();
        const int loopSteps = juce::jmax (1, bars) * 16;
        std::vector<std::pair<int, int>> timeline;
        std::set<int> onsetSteps;

        for (const auto& n : notes)
        {
            if (n.channel != 3)
            {
                c.onlyMelody = false;
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
            c.noCollisions = c.noCollisions && onsetSteps.insert (n.step).second;
            c.tonal = c.tonal && inScale (n.note, root, scale);
            c.registerSafe = c.registerSafe && n.note >= 48 && n.note <= 90;
            timeline.push_back ({ n.step, n.note });
        }

        c.populated = c.populated && c.noteCount > 0;
        c.onlyMelody = c.onlyMelody && c.populated;
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

    bool parseMelodyMidi (const juce::File& file, int& noteOns)
    {
        noteOns = 0;
        juce::FileInputStream input (file);
        juce::MidiFile midi;
        if (! input.openedOk() || ! midi.readFrom (input))
            return false;

        bool tempo = false;
        bool timeSignature = false;
        bool onlyMelody = true;
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
                onlyMelody = onlyMelody && message.getChannel() == 3;
            }
        }
        return noteOns > 0 && onlyMelody && tempo && timeSignature;
    }
}

int main()
{
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_0_105_1_release_qa");
    settings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (settings);

    bool allMelodyOnly = true;
    bool allInLoop = true;
    bool allOnGrid = true;
    bool allNoCollisions = true;
    bool allSpacingSafe = true;
    bool allTonal = true;
    bool allRegisterSafe = true;
    bool allLeapsSafe = true;
    bool allConstraintsPreserved = true;
    bool allPerformancePreserved = true;
    bool allRetiredModesOff = true;
    int checkedSlots = 0;
    int checkedNotes = 0;
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

        p.magicRandomize();
        p.waitForGeneration();

        allConstraintsPreserved = allConstraintsPreserved
            && p.getRoot() == root && p.getScale() == scale && p.getVisibleBars() == 4;
        allPerformancePreserved = allPerformancePreserved
            && std::abs (p.getSwing() - 0.12f) < 0.001f
            && std::abs (p.getHumanize() - 0.23f) < 0.001f
            && p.isHumanizeEnabled();
        allRetiredModesOff = allRetiredModesOff
            && ! p.getLeadStyleSoundCloud()
            && ! p.isChordsEnabled() && ! p.isBassEnabled()
            && p.isMelodyEnabled() && ! p.isArpEnabled() && ! p.isDrumsEnabled();

        std::set<std::string> bankFingerprints;
        const int slots = p.getVariationCount();
        for (int v = 0; v < slots; ++v)
        {
            p.chooseVariation (v);
            const auto notes = p.getVisibleNotes();
            const auto c = inspectMelody (notes, p.getVisibleBars(), root, scale);
            ++checkedSlots;
            checkedNotes += c.noteCount;
            allMelodyOnly = allMelodyOnly && c.populated && c.onlyMelody;
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

    check ("MAGIC produces one populated melody lane",
          allMelodyOnly && checkedSlots == 96,
          juce::String (checkedSlots) + " variation slots checked");
    check ("MAGIC preserves creator Key / Scale / Bars",
          allConstraintsPreserved, "all 12 scales with four-bar loops");
    check ("Generated melody follows the eighth-note grid",
          allOnGrid && allNoCollisions && allSpacingSafe,
          "no off-grid, duplicate or one-sixteenth-spaced attacks");
    check ("Notes and note tails stay inside the loop",
          allInLoop, "valid step, length, note and velocity bounds");
    check ("Generated melody remains in key",
          allTonal && checkedNotes > 0, juce::String (checkedNotes) + " notes checked");
    check ("Generated melody stays inside the grounded register",
          allRegisterSafe && checkedNotes > 0, "MIDI pitches 48–90");
    check ("Generated melody avoids unsafe pitch leaps",
          allLeapsSafe && checkedNotes > 0, "adjacent pitches differ by at most 12 semitones");
    check ("MAGIC preserves explicit performance settings",
          allPerformancePreserved, "Swing and Humanize remain user-controlled");
    check ("Retired accompaniment and SoundCloud modes stay disabled",
          allRetiredModesOff, "MIDI Forge produces melody only");
    check ("MAGIC variation bank retains meaningful diversity",
          diverseBanks >= 10,
          juce::String (diverseBanks) + " of 12 banks have at least six distinct ideas");

    // Mutations must keep the same musical safety contract as newly authored MIDI.
    MidiForgeAudioProcessor transform;
    transform.setFeedbackLogFile (juce::File());
    transform.setRoot (7);
    transform.setScale (2);
    transform.setBars (4);
    transform.setSeed (105901);
    transform.magicRandomize();
    transform.waitForGeneration();
    transform.mutateSelected (0.45f);
    auto mutated = inspectMelody (transform.getVisibleNotes(), transform.getVisibleBars(),
                                  transform.getRoot(), transform.getScale());
    transform.evolveSelected();
    auto evolved = inspectMelody (transform.getVisibleNotes(), transform.getVisibleBars(),
                                  transform.getRoot(), transform.getScale());
    const bool transformsValid =
        mutated.populated && mutated.onlyMelody && mutated.inLoop && mutated.onGrid
        && mutated.noCollisions && mutated.spacing && mutated.tonal
        && mutated.registerSafe && mutated.leapsSafe
        && evolved.populated && evolved.onlyMelody && evolved.inLoop && evolved.onGrid
        && evolved.noCollisions && evolved.spacing && evolved.tonal
        && evolved.registerSafe && evolved.leapsSafe;
    check ("MUTATE / EVOLVE preserve the melody contract",
          transformsValid, "lane, key, range, loop bounds and grid remain valid");

    // Export and drag-temporary files must contain real, parseable, melody-only MIDI.
    const auto outFile = settings.getChildFile ("release_export.mid");
    int exportedNotes = 0;
    const bool exportWritten = transform.exportMidiFileTo (outFile);
    const bool exportParsed = exportWritten && parseMelodyMidi (outFile, exportedNotes);
    check ("MIDI export contains valid melody and tempo metadata",
          exportParsed, juce::String (exportedNotes) + " note-ons");

    juce::File dragFile = transform.writeTemporaryMidiFile();
    int draggedNotes = 0;
    const bool dragOk = dragFile.existsAsFile() && dragFile.getSize() > 0
        && parseMelodyMidi (dragFile, draggedNotes);
    check ("FL drag payload is a real melody-only MIDI file",
          dragOk, juce::String (draggedNotes) + " note-ons");
    if (dragFile.existsAsFile()) dragFile.deleteFile();

    // Old states are parsed for compatibility, but retired layer flags must not return.
    juce::MemoryBlock state;
    transform.getStateInformation (state);
    MidiForgeAudioProcessor restored;
    restored.setFeedbackLogFile (juce::File());
    restored.setStateInformation (state.getData(), (int) state.getSize());
    restored.waitForGeneration();
    bool restoredOnlyMelody = ! restored.isChordsEnabled() && ! restored.isBassEnabled()
        && restored.isMelodyEnabled() && ! restored.isArpEnabled() && ! restored.isDrumsEnabled()
        && ! restored.getLeadStyleSoundCloud();
    const auto restoredNotes = restored.getVisibleNotes();
    restoredOnlyMelody = restoredOnlyMelody && ! restoredNotes.empty()
        && std::all_of (restoredNotes.begin(), restoredNotes.end(),
            [] (const Note& n) { return n.channel == 3; });
    check ("State round-trip keeps the melody-only contract",
          restoredOnlyMelody, juce::String (restoredNotes.size()) + " notes restored");

    std::printf ("\n%s (%d failed check%s)\n",
                 failures == 0 ? "ALL MELODY-ONLY RELEASE CHECKS PASSED" : "MELODY-ONLY RELEASE CHECKS FAILED",
                 failures, failures == 1 ? "" : "s");
    outFile.deleteFile();
    settings.getChildFile ("taste.json").deleteFile();
    settings.getChildFile ("feedback.csv").deleteFile();
    return failures == 0 ? 0 : 1;
}
