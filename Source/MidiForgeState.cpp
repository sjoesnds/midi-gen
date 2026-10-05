#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "MidiForgeShared.h"

void MidiForgeAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    waitForGeneration();
    juce::MemoryOutputStream o(dest, false);
    o.writeInt (kStateMagic); o.writeInt (kStateVersion);
    o.writeInt(rootPc);o.writeInt(0); /* legacy style slot */ o.writeInt(scale);o.writeInt(progression);
    o.writeInt(rhythm);o.writeInt(bars);o.writeInt(seed);o.writeInt(octave);o.writeInt(sectionMode);
    o.writeFloat(chordDensity);o.writeFloat(bassDensity);o.writeFloat(melodyDensity);o.writeFloat(arpDensity);
    o.writeFloat(swing);o.writeFloat(humanize);o.writeFloat(complexity);
    o.writeFloat(melodyLength);o.writeFloat(pauseChance);o.writeFloat(leapChance);o.writeFloat(ghostChance);
    o.writeInt(arpRate);o.writeFloat(voicingWidth);o.writeBool(chordExtensions);o.writeBool(inversions);
    o.writeFloat(motifStrength);o.writeFloat(variationAmount);o.writeFloat(fillAmount);o.writeFloat(energy);
    o.writeBool(chordsEnabled);o.writeBool(bassEnabled);o.writeBool(melodyEnabled);o.writeBool(arpEnabled);o.writeBool(hookMode);
    o.writeInt(selectedVariation);
    o.writeInt(mood);o.writeInt(melodyType);o.writeInt(5); // legacy Era slot, fixed to 20s
    o.writeInt(soundTarget);
    o.writeInt(articulation);o.writeInt(autoNextOnDislike?1:0);
    o.writeInt(chordStyle);o.writeInt(drumsEnabled?1:0);
    o.writeInt(drumMuteMask);o.writeInt(drumPitchMode);
    o.writeBool(leadStyleSoundCloud);
    o.writeBool(lockChordsLayer);o.writeBool(lockBassLayer);o.writeBool(lockMelodyLayer);o.writeBool(lockArpLayer);
    o.writeBool(tasteEnabled); o.writeBool(humanizeEnabled);

    // State v3: persist the editable Piano Roll MIDI so a saved project restores
    // the exact edited variation instead of regenerating over user edits.
    std::vector<VisibleNote> savedNotes;
    {
        const juce::ScopedLock sl (activeNotesLock);
        savedNotes.reserve (activeNotes.size());
        for (const auto& n : activeNotes)
            savedNotes.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
    }
    o.writeInt ((int) savedNotes.size());
    for (const auto& n : savedNotes)
    {
        o.writeInt (n.step);
        o.writeInt (n.length);
        o.writeInt (n.note);
        o.writeInt (n.velocity);
        o.writeInt (n.channel);
    }
}

void MidiForgeAudioProcessor::setStateInformation(const void* data, int size)
{
    if (!data || size <= 0) return;
    waitForGeneration();
    juce::MemoryInputStream i (data, (size_t) size, false);
    if (i.getNumBytesRemaining() < 4) return;

    const int firstWord = i.readInt();
    int stateVersion = 0;
    if (firstWord == kStateMagic)
    {
        if (i.getNumBytesRemaining() < 4) return;
        stateVersion = i.readInt();
        if (stateVersion <= 0 || stateVersion > kStateVersion) return;
    }
    else
        i.setPosition (0);

    rootPc = 0; scale = Minor; progression = AutoProg;
    rhythm = Straight; bars = 4; seed = 1337; octave = 4; sectionMode = Loop;
    chordDensity = 0.9f; bassDensity = 0.8f; melodyDensity = 0.62f; arpDensity = 0.25f;
    swing = 0.0f; humanize = 0.15f; complexity = 0.55f;
    melodyLength = 0.35f; pauseChance = 0.10f; leapChance = 0.18f; ghostChance = 0.08f;
    arpRate = 4; voicingWidth = 0.45f; chordExtensions = true; inversions = true;
    motifStrength = 0.78f; variationAmount = 0.40f; fillAmount = 0.18f; energy = 0.65f;
    chordsEnabled = bassEnabled = melodyEnabled = true; arpEnabled = false; hookMode = true;
    mood = NeutralMood; melodyType = HookMelody; era = 5; soundTarget = 0;
    articulation = 0; autoNextOnDislike = true; chordStyle = 0; drumsEnabled = false;
    drumMuteMask = 0; drumPitchMode = 0; leadStyleSoundCloud = false;
    lockChordsLayer = lockBassLayer = lockMelodyLayer = lockArpLayer = false;
    tasteEnabled = true; humanizeEnabled = false;
    int savedSelection = 0;

    auto readIntRaw = [&] (int& out) -> bool { if (i.getNumBytesRemaining() < 4) return false; out = i.readInt(); return true; };
    auto readIntClamped = [&] (int& out, int lo, int hi) -> bool
    {
        int raw = 0; if (! readIntRaw (raw)) return false; out = juce::jlimit (lo, hi, raw); return true;
    };
    auto readFloatClamped = [&] (float& out, float lo, float hi) -> bool
    {
        if (i.getNumBytesRemaining() < 4) return false;
        const float raw = i.readFloat();
        if (std::isfinite (raw)) out = juce::jlimit (lo, hi, raw);
        return true;
    };
    auto readBoolSafe = [&] (bool& out) -> bool
    {
        if (i.getNumBytesRemaining() < 1) return false; out = i.readBool(); return true;
    };

    { int discardedSlot = 0; readIntClamped (rootPc,0,11); readIntClamped (discardedSlot,0,15); } readIntClamped (scale,0,11);
    readIntClamped (progression,0,6); readIntClamped (rhythm,0,3); readIntClamped (bars,1,16);
    readIntRaw(seed); readIntClamped(octave,2,6); readIntClamped(sectionMode,(int)Loop,(int)SongExtended);
    readFloatClamped(chordDensity,0.0f,1.0f); readFloatClamped(bassDensity,0.0f,1.0f);
    readFloatClamped(melodyDensity,0.0f,1.0f); readFloatClamped(arpDensity,0.0f,1.0f);
    readFloatClamped(swing,0.0f,0.75f); readFloatClamped(humanize,0.0f,1.0f);
    readFloatClamped(complexity,0.0f,1.0f); readFloatClamped(melodyLength,0.0f,1.0f);
    readFloatClamped(pauseChance,0.0f,1.0f); readFloatClamped(leapChance,0.0f,1.0f); readFloatClamped(ghostChance,0.0f,1.0f);
    readIntClamped(arpRate,1,8); readFloatClamped(voicingWidth,0.0f,1.0f);
    readBoolSafe(chordExtensions); readBoolSafe(inversions);
    readFloatClamped(motifStrength,0.0f,1.0f); readFloatClamped(variationAmount,0.0f,1.0f);
    readFloatClamped(fillAmount,0.0f,1.0f); readFloatClamped(energy,0.0f,1.0f);
    readBoolSafe(chordsEnabled); readBoolSafe(bassEnabled); readBoolSafe(melodyEnabled); readBoolSafe(arpEnabled); readBoolSafe(hookMode);
    readIntClamped(savedSelection,0,7);

    if (i.getNumBytesRemaining() >= 4) readIntClamped(mood,0,8);
    if (i.getNumBytesRemaining() >= 4) readIntClamped(melodyType,0,7);
    // Legacy project states stored Era here. Parse it to keep the byte layout
    // compatible, then intentionally discard it in favour of the fixed 20s context.
    if (i.getNumBytesRemaining() >= 4) { int legacyEra = 5; readIntClamped(legacyEra,0,5); }
    era = 5;
    if (i.getNumBytesRemaining() >= 4) readIntClamped(soundTarget,0,7);
    if (i.getNumBytesRemaining() >= 8)
    {
        readIntClamped(articulation,0,2);
        int v = autoNextOnDislike ? 1 : 0;
        if (readIntRaw(v)) autoNextOnDislike = v != 0;
    }
    if (i.getNumBytesRemaining() >= 8)
    {
        readIntClamped(chordStyle,0,2);
        int v = drumsEnabled ? 1 : 0;
        if (readIntRaw(v)) drumsEnabled = v != 0;
    }
    if (i.getNumBytesRemaining() >= 8)
    {
        readIntRaw(drumMuteMask); drumMuteMask &= 0xFF;
        readIntClamped(drumPitchMode,0,1);
    }
    if (i.getNumBytesRemaining() >= 1) readBoolSafe(leadStyleSoundCloud);
    if (i.getNumBytesRemaining() >= 4)
    {
        readBoolSafe(lockChordsLayer); readBoolSafe(lockBassLayer);
        readBoolSafe(lockMelodyLayer); readBoolSafe(lockArpLayer);
    }
    if (i.getNumBytesRemaining() >= 1) readBoolSafe(tasteEnabled);
    if (i.getNumBytesRemaining() >= 1) readBoolSafe(humanizeEnabled);

    std::vector<VisibleNote> savedNotes;
    bool hasSavedNotes = false;
    if (stateVersion >= 3 && i.getNumBytesRemaining() >= 4)
    {
        int count = 0;
        if (readIntRaw (count))
        {
            count = juce::jlimit (0, 65536, count);
            hasSavedNotes = true;
            savedNotes.reserve ((size_t) count);
            for (int n = 0; n < count; ++n)
            {
                if (i.getNumBytesRemaining() < 20)
                {
                    savedNotes.clear();
                    break;
                }
                int step = 0, length = 1, note = 60, velocity = 100, channel = 3;
                if (! readIntRaw (step) || ! readIntRaw (length) || ! readIntRaw (note)
                    || ! readIntRaw (velocity) || ! readIntRaw (channel))
                {
                    savedNotes.clear();
                    break;
                }
                savedNotes.push_back ({
                    juce::jmax (0, step),
                    juce::jmax (1, length),
                    juce::jlimit (0, 127, note),
                    juce::jlimit (1, 127, velocity),
                    juce::jlimit (1, 5, channel)
                });
            }
        }
    }

    realtimeSwing.store(swing); realtimeHumanize.store(humanize);
    realtimeHumanizeEnabled.store(humanizeEnabled); realtimeDrumMuteMask.store(drumMuteMask);
    regenerateBlocking(savedSelection);
    if (stateVersion >= 3 && hasSavedNotes)
        replaceVisibleNotes (savedNotes);
}
