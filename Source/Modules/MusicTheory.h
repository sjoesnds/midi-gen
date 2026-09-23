#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>

namespace music
{

//==============================================================================
/** Scale definitions and utilities */
enum class ScaleType
{
    Major,
    Minor,
    Dorian,
    Phrygian,
    HarmonicMinor,
    MelodicMinor,
    Pentatonic
};

/** Returns semitones for a given scale type relative to root */
inline std::vector<int> getScaleSemitones(ScaleType scale)
{
    switch (scale)
    {
        case ScaleType::Major:          return {0, 2, 4, 5, 7, 9, 11};
        case ScaleType::Minor:          return {0, 2, 3, 5, 7, 8, 10};
        case ScaleType::Dorian:         return {0, 2, 3, 5, 7, 9, 10};
        case ScaleType::Phrygian:       return {0, 1, 3, 5, 7, 8, 10};
        case ScaleType::HarmonicMinor:  return {0, 2, 3, 5, 7, 8, 11};
        case ScaleType::MelodicMinor:   return {0, 2, 3, 5, 7, 9, 11};
        case ScaleType::Pentatonic:     return {0, 3, 5, 7, 10};
        default:                        return {0, 2, 4, 5, 7, 9, 11};
    }
}

/** Snaps a MIDI note to the nearest note in the scale */
inline int snapToScale(int midiNote, int rootPitchClass, ScaleType scale)
{
    const auto semitones = getScaleSemitones(scale);
    if (semitones.empty()) return midiNote;
    
    const int pc = midiNote % 12;
    const int octave = midiNote / 12;
    
    // Find closest scale degree
    int closest = semitones[0];
    int minDist = 12;
    
    for (int st : semitones)
    {
        int dist = std::abs(st - pc);
        if (dist < minDist)
        {
            minDist = dist;
            closest = st;
        }
    }
    
    return (octave * 12) + rootPitchClass + closest;
}

//==============================================================================
/** Chord progression definitions */
enum class ProgressionType
{
    AutoProg,
    Pop,
    Dark,
    Emotional,
    Cinematic,
    JazzLike,
    Looping
};

/** Returns chord degrees for a progression type (in roman numeral degrees) */
inline std::vector<int> getProgressionDegrees(ProgressionType prog)
{
    switch (prog)
    {
        case ProgressionType::Pop:        return {1, 5, 6, 4};      // I-V-vi-IV
        case ProgressionType::Dark:       return {1, 6, 7, 4};      // i-VI-VII-iv
        case ProgressionType::Emotional:  return {6, 4, 1, 5};      // vi-IV-I-V
        case ProgressionType::Cinematic:  return {1, 7, 3, 6};      // i-VII-III-VI
        case ProgressionType::JazzLike:   return {2, 5, 1, 6};      // ii-V-I-vi
        case ProgressionType::Looping:    return {1, 4, 1, 5};      // I-IV-I-V
        default:                          return {1, 5, 6, 4};      // Auto = Pop
    }
}

/** Converts a scale degree to a MIDI pitch */
inline int degreeToPitch(int degree, int rootPitchClass, ScaleType scale, int baseOctave)
{
    const auto semitones = getScaleSemitones(scale);
    if (semitones.empty() || degree < 1) return 0;
    
    // Degree is 1-based (1=root, 2=second, etc.)
    const int idx = (degree - 1) % static_cast<int>(semitones.size());
    const int octOffset = (degree - 1) / static_cast<int>(semitones.size());
    
    return ((baseOctave + octOffset) * 12) + rootPitchClass + semitones[idx];
}

//==============================================================================
/** Rhythm patterns */
enum class RhythmPattern
{
    Straight,
    Syncopated,
    Broken,
    Euclidean
};

/** Determines if a step should have a hit based on rhythm pattern */
inline bool shouldRhythmHit(int stepInBar, RhythmPattern rhythm, int stepsPerBar = 16)
{
    switch (rhythm)
    {
        case RhythmPattern::Straight:
            // Hits on 0, 4, 8, 12 (quarter notes)
            return (stepInBar % 4) == 0;
            
        case RhythmPattern::Syncopated:
            // Off-beat emphasis
            return (stepInBar % 4) == 2 || (stepInBar % 8) == 0;
            
        case RhythmPattern::Broken:
            // Irregular pattern
            return (stepInBar == 0) || (stepInBar == 3) || 
                   (stepInBar == 6) || (stepInBar == 10) ||
                   (stepInBar == 14);
                   
        case RhythmPattern::Euclidean:
            // Euclidean rhythm E(5,16) - 5 hits in 16 steps
            {
                const int hits = 5;
                return ((stepInBar * hits) % stepsPerBar) < hits;
            }
            
        default:
            return (stepInBar % 4) == 0;
    }
}

//==============================================================================
/** Genre-specific defaults */
enum class Genre
{
    Universal, Trap, House, Techno, BoomBap, Ambient, 
    Cinematic, RnB, Pop, Drill, DnB, Jersey, Afro, Hyperpop, Experimental, Lofi
};

struct GenreDefaults
{
    float tempo = 120.0f;
    float swing = 0.0f;
    float complexity = 0.5f;
    float energy = 0.5f;
    int typicalBars = 4;
};

inline GenreDefaults getGenreDefaults(Genre genre)
{
    switch (genre)
    {
        case Genre::Trap:       return {140.0f, 0.0f, 0.6f, 0.7f, 4};
        case Genre::House:      return {128.0f, 0.1f, 0.5f, 0.8f, 4};
        case Genre::Techno:     return {135.0f, 0.0f, 0.4f, 0.9f, 4};
        case Genre::BoomBap:    return {90.0f, 0.3f, 0.7f, 0.6f, 4};
        case Genre::Ambient:    return {100.0f, 0.0f, 0.3f, 0.3f, 8};
        case Genre::Cinematic:  return {110.0f, 0.0f, 0.5f, 0.7f, 8};
        case Genre::RnB:        return {95.0f, 0.2f, 0.6f, 0.5f, 4};
        case Genre::Pop:        return {120.0f, 0.0f, 0.5f, 0.7f, 4};
        case Genre::Drill:      return {140.0f, 0.0f, 0.7f, 0.8f, 4};
        case Genre::DnB:        return {174.0f, 0.0f, 0.6f, 0.9f, 4};
        case Genre::Jersey:     return {150.0f, 0.0f, 0.5f, 0.8f, 4};
        case Genre::Afro:       return {105.0f, 0.1f, 0.5f, 0.6f, 4};
        case Genre::Hyperpop:   return {160.0f, 0.0f, 0.8f, 0.9f, 4};
        case Genre::Experimental:return {110.0f, 0.1f, 0.9f, 0.5f, 8};
        case Genre::Lofi:       return {85.0f, 0.3f, 0.4f, 0.4f, 4};
        default:                return {120.0f, 0.0f, 0.5f, 0.5f, 4};
    }
}

} // namespace music
