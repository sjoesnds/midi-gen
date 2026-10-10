// MIDI Forge 0.106.1 release checks for the melody + chords + bass creator.
#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
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

int main()
{
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_0_106_0_release_qa");
    settings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (settings);

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
    juce::MemoryBlock stateV4;
    legacySource.getStateInformation (stateV4);
    constexpr size_t legacyMaskOffset = 170;
    constexpr size_t v4MaskEnd = legacyMaskOffset + sizeof (int32_t);
    juce::MemoryBlock stateV3;
    bool builtLegacyState = stateV4.getSize() >= v4MaskEnd + sizeof (int32_t);
    if (builtLegacyState)
    {
        juce::MemoryOutputStream legacyStream (stateV3, false);
        const auto* bytes = static_cast<const uint8_t*> (stateV4.getData());
        legacyStream.write (bytes, 4);        // state magic
        legacyStream.writeInt (3);            // legacy state version
        legacyStream.write (bytes + 8, legacyMaskOffset - 8);
        legacyStream.write (bytes + v4MaskEnd, stateV4.getSize() - v4MaskEnd);
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
    transform.magicRandomize();
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
    check ("MUTATE / EVOLVE preserve the melody contract",
          transformsValid, "lane, key, range, loop bounds and grid remain valid");

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

    // State v4 must preserve the restored layers and all individual MAGIC locks.
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
