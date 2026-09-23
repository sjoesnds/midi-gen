#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cstdint>

namespace midi
{

//==============================================================================
/** MIDI note event with musical context */
struct NoteEvent
{
    int step = 0;           // Step position in pattern (0-based)
    int length = 0;         // Length in steps
    int note = 60;          // MIDI pitch
    int velocity = 80;      // Velocity (1-127)
    int channel = 0;        // MIDI channel (0-15)
    bool ghost = false;     // Ghost note flag
    
    bool operator==(const NoteEvent& other) const
    {
        return step == other.step && length == other.length &&
               note == other.note && velocity == other.velocity &&
               channel == other.channel && ghost == other.ghost;
    }
};

//==============================================================================
/** Articulation information for expressive playback */
struct ArticulationInfo
{
    bool slide = false;         // Portamento/glide to next note
    int slideToStep = -1;       // Target step for slide
    bool vibrato = false;       // Vibrato on long notes
    
    bool isEmpty() const { return !slide && !vibrato; }
};

//==============================================================================
/** Visible note for UI representation */
struct VisibleNote
{
    int step = 0;
    int length = 0;
    int note = 60;
    int velocity = 80;
    int channel = 0;
    
    bool operator==(const VisibleNote& other) const
    {
        return step == other.step && length == other.length &&
               note == other.note && velocity == other.velocity &&
               channel == other.channel;
    }
};

//==============================================================================
/** Pending note-off event for precise timing */
struct PendingNoteOff
{
    juce::int64 globalSample = 0;
    int channel = 0;
    int note = 0;
    
    bool operator<(const PendingNoteOff& other) const
    {
        return globalSample > other.globalSample; // Min-heap by sample time
    }
};

//==============================================================================
/** Utility functions for MIDI operations */

/** Converts MIDI note to frequency in Hz */
inline float midiNoteToFrequency(int noteNumber)
{
    return 440.0f * std::pow(2.0f, (noteNumber - 69) / 12.0f);
}

/** Quantizes a value to a grid */
inline int quantizeToGrid(int value, int gridSize)
{
    return gridSize > 0 ? ((value + gridSize / 2) / gridSize) * gridSize : value;
}

/** Clamps MIDI note to valid range */
inline int clampMidiNote(int note)
{
    return juce::jlimit(0, 127, note);
}

/** Clamps velocity to valid range */
inline int clampVelocity(int velocity)
{
    return juce::jlimit(1, 127, velocity);
}

/** Checks if two notes overlap in time */
inline bool notesOverlap(const NoteEvent& a, const NoteEvent& b)
{
    if (a.channel != b.channel) return false;
    const int aEnd = a.step + a.length;
    const int bEnd = b.step + b.length;
    return (a.step < bEnd) && (b.step < aEnd);
}

/** Merges overlapping notes on same channel */
inline void mergeOverlappingNotes(std::vector<NoteEvent>& notes)
{
    if (notes.empty()) return;
    
    // Group by channel
    std::unordered_map<int, std::vector<NoteEvent>> byChannel;
    for (const auto& note : notes)
        byChannel[note.channel].push_back(note);
    
    std::vector<NoteEvent> result;
    
    for (auto& [channel, channelNotes] : byChannel)
    {
        // Sort by step
        std::sort(channelNotes.begin(), channelNotes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.step < b.step; });
        
        // Merge overlaps
        std::vector<NoteEvent> merged;
        for (const auto& note : channelNotes)
        {
            if (merged.empty() || !notesOverlap(merged.back(), note))
            {
                merged.push_back(note);
            }
            else
            {
                // Extend previous note
                const int end = std::max(merged.back().step + merged.back().length,
                                        note.step + note.length);
                merged.back().length = end - merged.back().step;
            }
        }
        
        result.insert(result.end(), merged.begin(), merged.end());
    }
    
    notes = std::move(result);
}

/** Removes duplicate notes at same position/channel */
inline void removeDuplicateNotes(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(),
        [](const NoteEvent& a, const NoteEvent& b) {
            if (a.channel != b.channel) return a.channel < b.channel;
            if (a.step != b.step) return a.step < b.step;
            return a.note < b.note;
        });
    
    notes.erase(std::unique(notes.begin(), notes.end(),
        [](const NoteEvent& a, const NoteEvent& b) {
            return a.channel == b.channel && a.step == b.step && a.note == b.note;
        }), notes.end());
}

/** Quantizes all notes to a grid */
inline void quantizeNotes(std::vector<NoteEvent>& notes, int gridSteps)
{
    for (auto& note : notes)
    {
        note.step = quantizeToGrid(note.step, gridSteps);
        note.length = quantizeToGrid(note.length, gridSteps);
        if (note.length < gridSteps)
            note.length = gridSteps;
    }
}

} // namespace midi
