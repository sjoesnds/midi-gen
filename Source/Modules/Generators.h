#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cstdint>

namespace generators
{

//==============================================================================
/** Sound profile for instrument-specific generation */
struct SoundProfile
{
    int laneShift = 0;          // Semitones added to melody register
    int laneCap = 92;           // Absolute upper MIDI limit
    float legato = 0.0f;        // Fraction of gap sustained (<0: use Melody Length slider)
    int minLen = 1, maxLen = 16;// Note length clamp in steps
    int velCenter = 80;         // Velocity compression centre
    float velSpread = 1.0f;     // 1 = unchanged, lower = flatter
    int minNotes = 4;           // Minimum notes per bar
    int minHits = 5;            // Minimum hits per pattern
    float densityMul = 1.0f;    // Scales density targets
    int maxLeap = 12;           // Max interval in semitones (0 = unlimited)
    int chordLen = 16;          // Chord hit length in steps
    bool chordTwoHits = false;  // Second chord hit on beat 3
    float slideChance = 0.0f;   // Chance of legato slide
    float vibChance = 0.0f;     // Chance of vibrato on long notes
    bool bassOff = false;       // Melody IS the bass voice (808)
    bool soloLine = false;      // Loop is ONE line (no layers)
};

/** Returns sound profile by ID */
inline SoundProfile getSoundProfile(int id)
{
    switch (id)
    {
        case 1:  // Pluck
            return {0, 92, 0.0f, 1, 2, 88, 0.45f, 5, 5, 1.10f, 12, 6, true, 0.0f, 0.0f, false, false};
        case 2:  // Synth Lead
            return {0, 92, 0.9f, 2, 16, 96, 0.40f, 4, 5, 0.95f, 9, 16, false, 0.3f, 0.5f, false, false};
        case 3:  // Bell / Mallet
            return {12, 96, 0.6f, 3, 6, 82, 0.60f, 3, 4, 0.72f, 12, 16, false, 0.0f, 0.0f, false, false};
        case 4:  // Pad / Strings
            return {-7, 84, 1.0f, 4, 16, 76, 0.30f, 2, 3, 0.50f, 5, 16, false, 0.0f, 0.0f, false, false};
        case 5:  // Brass
            return {-5, 88, 0.55f, 2, 6, 98, 0.70f, 4, 5, 0.90f, 7, 5, true, 0.0f, 0.0f, false, false};
        case 6:  // 808 / Sub Lead
            return {-30, 60, 0.85f, 2, 16, 100, 0.30f, 2, 3, 0.55f, 7, 16, false, 0.55f, 0.0f, true, true};
        case 7:  // Guitar
            return {-10, 80, 0.35f, 1, 6, 86, 0.70f, 4, 5, 1.00f, 9, 8, true, 0.2f, 0.0f, false, false};
        default: // Piano (neutral)
            return {0, 92, -1.0f, 1, 16, 80, 1.00f, 4, 5, 1.00f, 12, 16, false, 0.0f, 0.0f, false, false};
    }
}

//==============================================================================
/** Magic DNA parameters for coherent generation */
struct MagicDNA
{
    float melody = 0.50f;     // Melodic complexity
    float rhythm = 0.50f;     // Rhythmic activity
    float harmony = 0.50f;    // Harmonic richness
    float motif = 0.50f;      // Motif repetition strength
    float register_ = 0.50f;  // Register preference (low to high)
    float groove = 0.50f;     // Groove/swing amount
    float energy = 0.50f;     // Overall energy level
    float surprise = 0.35f;   // Unexpected elements
    
    uint32_t seed = 0xC0FFEEu;
    
    void randomize(juce::Random& rng)
    {
        melody = rng.nextFloat();
        rhythm = rng.nextFloat();
        harmony = rng.nextFloat();
        motif = rng.nextFloat();
        register_ = rng.nextFloat();
        groove = rng.nextFloat();
        energy = rng.nextFloat();
        surprise = rng.nextFloat() * 0.7f;
        seed = static_cast<uint32_t>(rng.nextInt());
    }
    
    void mutate(float amount, juce::Random& rng)
    {
        auto perturb = [&rng, amount](float& value) {
            value += rng.nextFloat() * amount * 2.0f - amount;
            value = juce::jlimit(0.0f, 1.0f, value);
        };
        
        perturb(melody);
        perturb(rhythm);
        perturb(harmony);
        perturb(motif);
        perturb(register_);
        perturb(groove);
        perturb(energy);
        perturb(surprise);
    }
};

//==============================================================================
/** Variation candidate with quality score */
struct VariationCandidate
{
    float quality = 0.0f;
    float density = 0.0f;
    float velocity = 0.0f;
    float leap = 0.0f;
    float rhythm = 0.0f;
    float repetition = 0.0f;
    float variety = 0.0f;
    float registerScore = 0.0f;
    float noteLength = 0.0f;
    
    bool operator<(const VariationCandidate& other) const
    {
        return quality < other.quality;
    }
};

//==============================================================================
/** Utility functions for music generation */

/** Hash function for deterministic randomness */
inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/** Combines multiple seeds into one */
inline uint32_t combineSeeds(uint32_t a, uint32_t b, uint32_t c = 0)
{
    return hash32(a ^ hash32(b ^ hash32(c)));
}

/** Generates a seeded random float in [0, 1) */
inline float seededRandom(uint32_t& seed)
{
    seed = hash32(seed);
    return static_cast<float>(seed) / static_cast<float>(UINT32_MAX);
}

/** Generates a seeded random int in [min, max] */
inline int seededRandomInt(uint32_t& seed, int min, int max)
{
    return min + (static_cast<int>(seededRandom(seed) * (max - min + 1)));
}

/** Applies humanization to timing */
inline float applyHumanization(float position, float amount, juce::Random& rng)
{
    if (amount <= 0.0f) return position;
    return position + (rng.nextFloat() * amount * 2.0f - amount);
}

/** Applies swing to a step position */
inline float applySwing(int stepInBar, float swingAmount, int stepsPerBar = 16)
{
    if (swingAmount <= 0.0f) return static_cast<float>(stepInBar);
    
    // Apply swing to off-beat positions (odd 8th notes)
    if ((stepInBar % 2) == 1)
    {
        return static_cast<float>(stepInBar) + swingAmount;
    }
    
    return static_cast<float>(stepInBar);
}

/** Calculates note density (notes per bar) */
inline float calculateDensity(const std::vector<midi::NoteEvent>& notes, int totalBars)
{
    if (totalBars <= 0 || notes.empty()) return 0.0f;
    return static_cast<float>(notes.size()) / static_cast<float>(totalBars);
}

/** Calculates average velocity */
inline float calculateAverageVelocity(const std::vector<midi::NoteEvent>& notes)
{
    if (notes.empty()) return 0.0f;
    
    int sum = 0;
    for (const auto& note : notes)
        sum += note.velocity;
    
    return static_cast<float>(sum) / static_cast<float>(notes.size());
}

/** Calculates average leap size in semitones */
inline float calculateAverageLeap(const std::vector<midi::NoteEvent>& notes)
{
    if (notes.size() < 2) return 0.0f;
    
    auto sorted = notes;
    std::sort(sorted.begin(), sorted.end(),
        [](const midi::NoteEvent& a, const midi::NoteEvent& b) { return a.step < b.step; });
    
    int totalLeap = 0;
    int count = 0;
    
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        if (sorted[i].channel == sorted[i-1].channel)
        {
            totalLeap += std::abs(sorted[i].note - sorted[i-1].note);
            ++count;
        }
    }
    
    return count > 0 ? static_cast<float>(totalLeap) / static_cast<float>(count) : 0.0f;
}

/** Calculates rhythmic regularity (0 = irregular, 1 = perfectly regular) */
inline float calculateRhythmicRegularity(const std::vector<midi::NoteEvent>& notes)
{
    if (notes.size() < 2) return 0.5f;
    
    auto sorted = notes;
    std::sort(sorted.begin(), sorted.end(),
        [](const midi::NoteEvent& a, const midi::NoteEvent& b) { return a.step < b.step; });
    
    std::vector<int> gaps;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        if (sorted[i].channel == sorted[i-1].channel)
            gaps.push_back(sorted[i].step - sorted[i-1].step);
    }
    
    if (gaps.empty()) return 0.5f;
    
    // Calculate variance of gaps
    float mean = 0.0f;
    for (int gap : gaps)
        mean += static_cast<float>(gap);
    mean /= static_cast<float>(gaps.size());
    
    float variance = 0.0f;
    for (int gap : gaps)
    {
        float diff = static_cast<float>(gap) - mean;
        variance += diff * diff;
    }
    variance /= static_cast<float>(gaps.size());
    
    // Convert to regularity score (lower variance = higher regularity)
    const float maxVariance = 16.0f; // Expected max variance
    return juce::jlimit(0.0f, 1.0f, 1.0f - (variance / maxVariance));
}

/** Calculates motif repetition strength */
inline float calculateMotifStrength(const std::vector<midi::NoteEvent>& notes, int patternSteps = 64)
{
    if (notes.size() < 4) return 0.0f;
    
    // Simple pattern matching: check for repeated pitch sequences
    std::unordered_map<uint32_t, int> patternCounts;
    
    for (size_t i = 0; i + 3 < notes.size(); ++i)
    {
        // Create hash of 4-note sequence
        uint32_t hash = 0;
        for (int j = 0; j < 4; ++j)
        {
            hash = hash32(hash ^ static_cast<uint32_t>(notes[i + j].note));
        }
        patternCounts[hash]++;
    }
    
    // Count how many patterns repeat
    int repeats = 0;
    for (const auto& [hash, count] : patternCounts)
    {
        if (count > 1)
            repeats += count - 1;
    }
    
    return juce::jlimit(0.0f, 1.0f, 
        static_cast<float>(repeats) / static_cast<float>(notes.size()));
}

} // namespace generators
