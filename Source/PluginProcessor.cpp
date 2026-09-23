#include "PluginProcessor.h"
#ifndef MIDIFORGE_HEADLESS
#include "PluginEditor.h"
#endif
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

// Modular includes
#include "Modules/MusicTheory.h"
#include "Modules/MidiTypes.h"
#include "Modules/Generators.h"
#include "Modules/PresetManager.h"

namespace
{
    // Use module hash function instead of local implementation
    // This ensures consistent hashing across the codebase
}

namespace
{
    // 0.39 Sound Profiles: the melody is written for a *type of sound*, not only
    // for a piano.  A piano tolerates sparse, staccato, velocity-driven notes;
    // a lead needs legato, a pluck needs short consistent hits, a pad needs long
    // stepwise notes, a bell needs high register and ringing notes.
    // Use module SoundProfile instead of local definition
    using SoundProfile = generators::SoundProfile;
    static SoundProfile soundProfileFor (int id)
    {
        return generators::getSoundProfile(id);
    }
}
MidiForgeAudioProcessor::MidiForgeAudioProcessor()
: AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    preferencesFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("MidiForge").getChildFile ("taste.json");
    loadPreferences();
    regenerate();
}
bool MidiForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void MidiForgeAudioProcessor::prepareToPlay(double sr, int)
{
sampleRate = sr; lastGlobalStep.store (-1);
samplePosition = 0;
pendingOffs.clear();
}
void MidiForgeAudioProcessor::setRoot(int v){rootPc=juce::jlimit(0,11,v);regenerate();}
void MidiForgeAudioProcessor::setGenre(int v){genre=juce::jlimit(0,15,v);regenerate();}
void MidiForgeAudioProcessor::setScale(int v){scale=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setMood(int v){mood=juce::jlimit(0,8,v);regenerate();}
void MidiForgeAudioProcessor::setMelodyType(int v){melodyType=juce::jlimit(0,7,v);regenerate();}
void MidiForgeAudioProcessor::setSoundTarget(int v){soundTarget=juce::jlimit(0,7,v);regenerate();}
void MidiForgeAudioProcessor::setArticulation(int v){articulation=juce::jlimit(0,2,v);}
void MidiForgeAudioProcessor::setChordStyle(int v){chordStyle=juce::jlimit(0,2,v);regenerate();}
void MidiForgeAudioProcessor::setDrumsEnabled(bool on){drumsEnabled=on;regenerate();}
const char* MidiForgeAudioProcessor::drumRowName(int row)
{
    static const char* names[kDrumRows]={"Kick","Snare","Clap","Hat","Open Hat","Toms","Crash","Shaker"};
    return names[juce::jlimit(0,kDrumRows-1,row)];
}
int MidiForgeAudioProcessor::drumRowForNote(int gm)
{
    switch(gm)
    {
        case 36: return 0;
        case 37: case 38: return 1;
        case 39: return 2;
        case 42: return 3;
        case 46: return 4;
        case 43: case 45: case 47: case 50: return 5;
        case 49: return 6;
        case 70: return 7;
        default: return -1;
    }
}
int MidiForgeAudioProcessor::drumRowNote(int row)
{
    static const int pitches[kDrumRows]={36,38,39,42,46,45,49,70};
    return pitches[juce::jlimit(0,kDrumRows-1,row)];
}
int MidiForgeAudioProcessor::drumOutPitch(int row,int gm) const
{
    if(drumPitchMode==1) return gm;                       // General MIDI kit
    return 60 + (row==5 ? gm-45 : 0);                     // one sample per channel: C5 (toms keep their relative pitches)
}
bool MidiForgeAudioProcessor::toggleDrumHit(int step,int row)
{
    row=juce::jlimit(0,kDrumRows-1,row);
    const auto notes=getVisibleNotes();
    for(int i=0;i<(int)notes.size();++i)
        if(notes[(size_t)i].channel==5 && notes[(size_t)i].step==step && drumRowForNote(notes[(size_t)i].note)==row)
        { deleteVisibleNote(i); return false; }
    static const int velocities[kDrumRows]={110,105,100,75,78,90,96,55};
    addVisibleNote(step,drumRowNote(row),row==4 ? 2 : 1,velocities[row],5);
    return true;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForDrumRow(int row) const
{
    auto file=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("MidiForge_"+juce::String(row<0 ? "Drums" : drumRowName(row))+"_"
                      +juce::String(juce::Random::getSystemRandom().nextInt())+".mid");
    auto midiFile=buildMidiFile(5,row);
    if(auto stream=file.createOutputStream()) midiFile.writeTo(*stream);
    return file;
}
void MidiForgeAudioProcessor::dislikeAndAdvance()
{
    const int vi = selectedVariation;
    dislikeVariation(vi);
    if (! autoNextOnDislike) return;
    int count = 0;
    { juce::ScopedLock sl(variationsLock); count = (int) variations.size(); }
    if (vi + 1 < count) chooseVariation(vi + 1);
    else magicRandomize();
}
void MidiForgeAudioProcessor::setEra(int v){era=juce::jlimit(0,5,v);regenerate();}
void MidiForgeAudioProcessor::setProgression(int v){progression=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setRhythm(int v){rhythm=juce::jlimit(0,3,v);regenerate();}
void MidiForgeAudioProcessor::setBars(int v){bars=juce::jlimit(1,16,v);regenerate();}
void MidiForgeAudioProcessor::setSeed(int v){seed=v;regenerate();}
void MidiForgeAudioProcessor::setOctave(int v){octave=juce::jlimit(2,6,v);regenerate();}
void MidiForgeAudioProcessor::setSectionMode(int v){ sectionMode = Loop; regenerate(); }
void MidiForgeAudioProcessor::setChordDensity(float v){chordDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setBassDensity(float v){bassDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setMelodyDensity(float v){melodyDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setArpDensity(float v){arpDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setSwing(float v){swing=juce::jlimit(0.f,.75f,v);}
void MidiForgeAudioProcessor::setHumanize(float v){humanize=juce::jlimit(0.f,1.f,v);}
void MidiForgeAudioProcessor::setComplexity(float v){complexity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setMelodyLength(float v){melodyLength=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setPauseChance(float v){pauseChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setLeapChance(float v){leapChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setGhostChance(float v){ghostChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setArpRate(int v){arpRate=juce::jlimit(1,8,v);regenerate();}
void MidiForgeAudioProcessor::setVoicingWidth(float v){voicingWidth=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setChordExtensions(bool v){chordExtensions=v;regenerate();}
void MidiForgeAudioProcessor::setInversions(bool v){inversions=v;regenerate();}
void MidiForgeAudioProcessor::setMotifStrength(float v){motifStrength=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setVariationAmount(float v){variationAmount=juce::jlimit(0.f,1.f,v);regenerateVariations();}
void MidiForgeAudioProcessor::setFillAmount(float v){fillAmount=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setEnergy(float v){energy=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setChordsEnabled(bool v){chordsEnabled=v;}
void MidiForgeAudioProcessor::setBassEnabled(bool v){bassEnabled=v;}
void MidiForgeAudioProcessor::setMelodyEnabled(bool v){melodyEnabled=v;}
void MidiForgeAudioProcessor::setArpEnabled(bool v){arpEnabled=v;}
void MidiForgeAudioProcessor::setHookMode(bool v){hookMode=v;regenerate();}

// Refactored: use music::getScaleSemitones instead of local implementation
std::vector<int> MidiForgeAudioProcessor::scaleSemitones() const
{
    return music::getScaleSemitones(static_cast<music::ScaleType>(scale));
}

// Refactored: use music::getProgressionDegrees with scale validation
std::vector<int> MidiForgeAudioProcessor::progressionDegrees() const
{
    // 0.39.1: stacking thirds on Harmonic/Melodic Minor, Dorian and Phrygian gives
    // augmented and diminished triads on several degrees (up to a third of all bars
    // in Melodic Minor).  Those chords sound "wrong" in a loop, so such a degree is
    // replaced by the closest degree that forms a plain major/minor triad.
    auto degrees = progressionDegreesRaw();
    const auto sc = scaleSemitones();
    const int n = (int) sc.size();
    if (n < 7) return degrees;
    auto pitchAt = [&](int k) { const int idx = ((k % n) + n) % n; return sc[(size_t) idx] + 12 * ((k - idx) / n); };
    auto plainTriad = [&](int d) { const int a = pitchAt(d + 2) - pitchAt(d), b = pitchAt(d + 4) - pitchAt(d);
                                   return (a == 3 || a == 4) && b == 7; };
    for (auto& d : degrees)
    {
        if (plainTriad(d)) continue;
        for (int alt : { 2, -2, 4, -4 })
        {
            const int c = (((d + alt) % n) + n) % n;
            if (plainTriad(c)) { d = c; break; }
        }
    }
    return degrees;
}

std::vector<int> MidiForgeAudioProcessor::progressionDegreesRaw() const
{
    // Use module progression types
    switch(static_cast<music::ProgressionType>(progression))
    {
        case music::ProgressionType::Pop:       return {0, 4, 5, 3};
        case music::ProgressionType::Dark:      return {0, 5, 2, 6};
        case music::ProgressionType::Emotional: return {5, 3, 0, 4};
        case music::ProgressionType::Cinematic: return {0, 3, 4, 5};
        case music::ProgressionType::JazzLike:  return {1, 4, 0, 3};
        case music::ProgressionType::Looping:   return {0, 5, 3, 4};
        default: break;
    }
    
    // Auto-progression based on genre
    switch(genre){
    case Trap:return{0,5,2,6};
    case House:return{0,4,5,3};
    case Techno:return{0,5,3,4};
    case BoomBap:return{0,5,3,4};
    case Ambient:return{0,3,5,4};
    case Cinematic:return{0,3,4,5};
    case RnB:return{1,4,0,5};
    case GenrePop:return{0,4,5,3};
    case Drill:return{0,5,3,6};
case DnB:return{0,5,3,4};
case Jersey:return{0,5,3,4};
case Afro:return{0,3,4,5};
case Hyperpop:return{0,4,5,3};
case Experimental:return{0,2,5,3};
case Lofi:return{0,5,3,4};
default:return{0,5,3,4};
}
}
// 0.38 Register lanes ---------------------------------------------------
// Every layer lives in its own register, so the parts no longer pile up in the
// same octaves.  The Octave control moves chords and melody together by at most
// one octave; the bass lane is fixed (never below E1 = MIDI 28, which is still
// audible on ordinary speakers/headphones).
// Two notes of one layer on the same step and pitch would retrigger in the DAW; keep the first.
template <typename T>
static void removeDuplicateNotes (std::vector<T>& v)
{
    std::unordered_set<long long> seen;
    std::vector<T> out;
    out.reserve (v.size());
    for (const auto& n : v)
    {
        const long long key = ((long long) n.channel << 40) | ((long long) n.step << 8) | (long long) n.note;
        if (seen.insert (key).second) out.push_back (n);
    }
    v.swap (out);
}
// The melody is one line: no two melody notes on the same step and no melody note
// running into the next one.  (Candidate mutations used to create accidental dyads.)
template <typename T>
static void cleanMelodyLine (std::vector<T>& v)
{
    std::vector<size_t> idx;
    for (size_t i = 0; i < v.size(); ++i) if (v[i].channel == 3) idx.push_back (i);
    std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return v[a].step < v[b].step; });
    std::vector<bool> drop (v.size(), false);
    long long prev = -1;
    for (size_t i : idx)
    {
        if (prev >= 0 && v[(size_t) prev].step == v[i].step) { drop[i] = true; continue; }
        if (prev >= 0)
        {
            auto& a = v[(size_t) prev];
            a.length = std::max (1, std::min (a.length, v[i].step - a.step));
        }
        prev = (long long) i;
    }
    std::vector<T> out;
    out.reserve (v.size());
    for (size_t i = 0; i < v.size(); ++i) if (! drop[i]) out.push_back (v[i]);
    v.swap (out);
}
static int foldIntoLane (int note, int lo, int hi)
{
    if (hi - lo < 11) return juce::jlimit (lo, hi, note);
    while (note < lo) note += 12;
    while (note > hi) note -= 12;
    return note;
}
void MidiForgeAudioProcessor::registerLane (int part, int& lo, int& hi) const
{
    const int shift = juce::jlimit (-12, 12, (octave - 4) * 6);   // Octave 3/4/5/6 = -6/0/+6/+12
    switch (part)
    {
        case 0:  lo = 48 + shift; hi = juce::jmin (72 + shift, 84); break;   // chords
        case 1:  lo = 28;         hi = 52;                          break;   // bass
        default:                                                              // melody
        {
            const auto prof = soundProfileFor (soundTarget);
            if (prof.soloLine) { lo = 28; hi = 50; break; }   // 808: E1..D3
            lo = 62 + shift + prof.laneShift;
            hi = juce::jmin (86 + shift + prof.laneShift, prof.laneCap);
            lo = juce::jmin (lo, hi - 14);
            break;
        }
    }
}
int MidiForgeAudioProcessor::degreeToPitch(int degree,int baseOctave) const
{
const auto s=scaleSemitones(); int count=(int)s.size();
if(!count)return 60;
int oct=degree/count, idx=degree%count;
if(idx<0){idx+=count;--oct;}
return 12*(baseOctave+oct)+rootPc+s[(size_t)idx];
}
int MidiForgeAudioProcessor::snapToScale(int midi) const
{
    // Refactored: use music::snapToScale from module
    return music::snapToScale(midi, rootPc, static_cast<music::ScaleType>(scale));
}

bool MidiForgeAudioProcessor::rhythmHit(int x) const
{
    // Refactored: use music::shouldRhythmHit from module
    return music::shouldRhythmHit(x, static_cast<music::RhythmPattern>(rhythm), 16);
}
// --- Learning -----------------------------------------------------------
void MidiForgeAudioProcessor::sampleVariationFeatures(int vi,float& d,float& e,float& c) const
{
const juce::ScopedLock sl(variationsLock);
if(vi<0||vi>=(int)variations.size()){d=e=c=0.5f;return;}
const auto& notes=variations[(size_t)vi].notes;
const int barsN=juce::jmax(1,variations[(size_t)vi].bars);
int mel=0,velSum=0,velN=0,prevNote=-1,intSum=0,intN=0;
for(const auto& n:notes){
velSum+=n.velocity; ++velN;
if(n.channel==3){
++mel;
if(prevNote>=0){ intSum+=std::abs(n.note-prevNote); ++intN; }
prevNote=n.note;
}
}
d=juce::jlimit(0.f,1.f,(float)mel/(float)(barsN*8));
e=juce::jlimit(0.f,1.f,velN>0?(float)velSum/(float)velN/127.f:0.5f);
c=juce::jlimit(0.f,1.f,intN>0?(float)intSum/(float)intN/12.f:0.5f);
}
void MidiForgeAudioProcessor::sampleVariationTaste(int varIndex, std::array<float,8>& features) const
{
    features.fill(0.5f);
    juce::ScopedLock sl(variationsLock);
    if (varIndex < 0 || varIndex >= (int)variations.size()) return;

    const auto& notes = variations[(size_t)varIndex].notes;
    std::vector<const NoteEvent*> melody;
    for (const auto& n : notes)
        if (n.channel == 3) melody.push_back(&n);
    if (melody.empty()) return;

    const int totalSteps = juce::jmax(1, variations[(size_t)varIndex].bars * 16);

    float velocity = 0.5f;
    for (auto* n : melody) velocity += (float)n->velocity / 127.0f;
    velocity /= (float)melody.size() + 1.0f;

    int leaps = 0, repeatedIntervals = 0, intervalCount = 0;
    std::vector<int> uniquePitches;
    int minPitch = 127, maxPitch = 0;
    for (size_t i = 0; i < melody.size(); ++i)
    {
        minPitch = juce::jmin(minPitch, melody[i]->note);
        maxPitch = juce::jmax(maxPitch, melody[i]->note);
        uniquePitches.push_back(melody[i]->note % 12);
        if (i > 0)
        {
            const int d = melody[i]->note - melody[i - 1]->note;
            if (std::abs(d) >= 5) ++leaps;
            if (i > 1)
            {
                const int prev = melody[i - 1]->note - melody[i - 2]->note;
                if (std::abs(d) == std::abs(prev)) ++repeatedIntervals;
            }
            ++intervalCount;
        }
    }
    std::sort(uniquePitches.begin(), uniquePitches.end());
    uniquePitches.erase(std::unique(uniquePitches.begin(), uniquePitches.end()), uniquePitches.end());

    int offbeats = 0;
    float lengthNorm = 0.0f;
    for (auto* n : melody)
    {
        if ((n->step % 4) != 0) ++offbeats;
        lengthNorm += (float)n->length / 16.0f;
    }
    lengthNorm /= (float)melody.size();

    features[0] = juce::jlimit(0.0f, 1.0f, (float)melody.size() / (float)juce::jmax(1, variations[(size_t)varIndex].bars * 6));
    features[1] = juce::jlimit(0.0f, 1.0f, velocity);
    features[2] = intervalCount > 0 ? juce::jlimit(0.0f, 1.0f, (float)leaps / (float)intervalCount) : 0.5f;
    features[3] = juce::jlimit(0.0f, 1.0f, (float)offbeats / (float)melody.size());
    features[4] = intervalCount > 1 ? juce::jlimit(0.0f, 1.0f, (float)repeatedIntervals / (float)(intervalCount - 1)) : 0.5f;
    features[5] = juce::jlimit(0.0f, 1.0f, (float)uniquePitches.size() / 6.0f);
    features[6] = juce::jlimit(0.0f, 1.0f, (float)(maxPitch - minPitch) / 24.0f);
    features[7] = juce::jlimit(0.0f, 1.0f, lengthNorm);

    juce::ignoreUnused(totalSteps);
}

void MidiForgeAudioProcessor::applyTasteToCandidate(float& quality, float density, float velocity, float leap,
                                                     float rhythm, float repetition, float variety,
                                                     float registerScore, float noteLength) const
{
    if (likedFeatureN > 0)
    {
        const float confidence = juce::jlimit(0.0f, 1.0f, (float)likedFeatureN / 8.0f);
        const float f[8] = { density, velocity, leap, rhythm, repetition, variety, registerScore, noteLength };
        float similarity = 0.0f;
        for (int i = 0; i < 8; ++i)
            similarity += 1.0f - juce::jlimit(0.0f, 1.0f, std::abs(f[i] - likedFeatures[(size_t)i]));
        similarity /= 8.0f;
        quality += 0.16f * confidence * similarity;
    }

    if (dislikedFeatureN > 0)
    {
        const float confidence = juce::jlimit(0.0f, 1.0f, (float)dislikedFeatureN / 8.0f);
        const float f[8] = { density, velocity, leap, rhythm, repetition, variety, registerScore, noteLength };
        float similarity = 0.0f;
        for (int i = 0; i < 8; ++i)
            similarity += 1.0f - juce::jlimit(0.0f, 1.0f, std::abs(f[i] - dislikedFeatures[(size_t)i]));
        similarity /= 8.0f;
        quality -= 0.13f * confidence * similarity;
    }
}

void MidiForgeAudioProcessor::likeVariation(int vi)
{
    if (vi < 0 || vi > 7) return;
    likeCounts[(size_t)vi]++;
    float d, e, c; sampleVariationFeatures(vi, d, e, c);
    std::array<float,8> f; sampleVariationTaste(vi, f);
    likedD = (likedD * likedN + d) / (likedN + 1);
    likedE = (likedE * likedN + e) / (likedN + 1);
    likedC = (likedC * likedN + c) / (likedN + 1);
    ++likedN;
    for (int i = 0; i < 8; ++i)
        likedFeatures[(size_t)i] = (likedFeatures[(size_t)i] * (float)(likedFeatureN) + f[(size_t)i])
                                   / (float)(likedFeatureN + 1);
    ++likedFeatureN;
    trainTaste(vi, 1.0f, 1.0f);
    savePreferences();
    applyLearnedWeights();
}

void MidiForgeAudioProcessor::dislikeVariation(int vi)
{
    if (vi < 0 || vi > 7) return;
    dislikeCounts[(size_t)vi]++;
    float d, e, c; sampleVariationFeatures(vi, d, e, c);
    std::array<float,8> f; sampleVariationTaste(vi, f);
    disD = (disD * disN + d) / (disN + 1);
    disE = (disE * disN + e) / (disN + 1);
    disC = (disC * disN + c) / (disN + 1);
    ++disN;
    for (int i = 0; i < 8; ++i)
        dislikedFeatures[(size_t)i] = (dislikedFeatures[(size_t)i] * (float)(dislikedFeatureN) + f[(size_t)i])
                                      / (float)(dislikedFeatureN + 1);
    ++dislikedFeatureN;
    trainTaste(vi, 0.0f, 1.0f);
    savePreferences();
    applyLearnedWeights();
}

void MidiForgeAudioProcessor::trainTaste(int vi, float likeTarget, float weight)
{
    std::vector<NoteEvent> notes;
    int barsN = 1;
    {
        juce::ScopedLock sl(variationsLock);
        if (vi < 0 || vi >= (int) variations.size()) return;
        notes = variations[(size_t) vi].notes;
        barsN = variations[(size_t) vi].bars;
    }
    const auto x = taste::extractFeatures(notes, barsN);
    taste::Vec z;
    for (size_t i = 0; i < (size_t) taste::kDim; ++i)
        z[i] = juce::jlimit(-3.0f, 3.0f, (x[i] - tasteMean[i]) / tasteStd[i]);
    tasteModel.update(z, soundTarget, genre, likeTarget, weight);
}

void MidiForgeAudioProcessor::resetTaste()
{
    tasteModel.reset();
    likedD = likedE = likedC = 0; likedN = 0;
    disD = disE = disC = 0; disN = 0;
    likeCounts.fill(0); dislikeCounts.fill(0);
    likedFeatures.fill(0.0f); dislikedFeatures.fill(0.0f);
    likedFeatureN = dislikedFeatureN = 0;
    savePreferences();
}

juce::String MidiForgeAudioProcessor::getTasteSummary() const
{
    if (tasteModel.samples() < 2.0f) return "learning: rate a few loops";
    int likeIdx, avoidIdx;
    tasteModel.topPreferences(likeIdx, avoidIdx);
    juce::String t;
    if (likeIdx >= 0) t << "likes " << taste::featureName(likeIdx);
    if (avoidIdx >= 0) t << (t.isEmpty() ? "" : " | ") << "avoids " << taste::featureName(avoidIdx);
    return t.isEmpty() ? juce::String("no clear taste yet") : t;
}

int MidiForgeAudioProcessor::getLikeCount(int vi) const { return (vi >= 0 && vi < 8) ? likeCounts[(size_t)vi] : 0; }
int MidiForgeAudioProcessor::getDislikeCount(int vi) const { return (vi >= 0 && vi < 8) ? dislikeCounts[(size_t)vi] : 0; }
int MidiForgeAudioProcessor::getVariationScore(int vi) const { return getLikeCount(vi) - getDislikeCount(vi); }

void MidiForgeAudioProcessor::applyLearnedWeights()
{
    float dirD = 0, dirE = 0, dirC = 0; int w = 0;
    if (likedN > 0 && disN > 0) { dirD = likedD - disD; dirE = likedE - disE; dirC = likedC - disC; w = juce::jmin(likedN, disN); }
    else if (likedN > 0) { dirD = likedD - melodyDensity; dirE = likedE - energy; dirC = likedC - complexity; w = likedN; }
    else if (disN > 0) { dirD = melodyDensity - disD; dirE = energy - disE; dirC = complexity - disC; w = disN; }
    if (w <= 0) return;
    const float g = 0.15f * juce::jlimit(0.f, 1.f, (float)w / 6.f);
    melodyDensity = juce::jlimit(0.f, 1.f, melodyDensity + dirD * g);
    energy = juce::jlimit(0.f, 1.f, energy + dirE * g);
    complexity = juce::jlimit(0.f, 1.f, complexity + dirC * g);
}

void MidiForgeAudioProcessor::savePreferences()
{
    juce::DynamicObject* o = new juce::DynamicObject();
    o->setProperty("likedN", likedN); o->setProperty("likedD", (double)likedD);
    o->setProperty("likedE", (double)likedE); o->setProperty("likedC", (double)likedC);
    o->setProperty("disN", disN); o->setProperty("disD", (double)disD);
    o->setProperty("disE", (double)disE); o->setProperty("disC", (double)disC);
    o->setProperty("likedFeatureN", likedFeatureN);
    o->setProperty("dislikedFeatureN", dislikedFeatureN);
    juce::Array<juce::var> lk, dk, lf, df;
    for (int i = 0; i < 8; ++i)
    {
        lk.add(likeCounts[(size_t)i]); dk.add(dislikeCounts[(size_t)i]);
        lf.add((double)likedFeatures[(size_t)i]); df.add((double)dislikedFeatures[(size_t)i]);
    }
    o->setProperty("likes", lk); o->setProperty("dislikes", dk);
    o->setProperty("tasteML", tasteModel.toVar());
    o->setProperty("likedFeatures", lf); o->setProperty("dislikedFeatures", df);
    preferencesFile.getParentDirectory().createDirectory();
    preferencesFile.replaceWithText(juce::JSON::toString(juce::var(o)));
}

void MidiForgeAudioProcessor::loadPreferences()
{
    if (!preferencesFile.existsAsFile()) return;
    juce::var v = juce::JSON::parse(preferencesFile.loadFileAsString());
    if (auto* o = v.getDynamicObject())
    {
        likedN = (int)o->getProperty("likedN"); disN = (int)o->getProperty("disN");
        likedD = (float)o->getProperty("likedD"); likedE = (float)o->getProperty("likedE"); likedC = (float)o->getProperty("likedC");
        disD = (float)o->getProperty("disD"); disE = (float)o->getProperty("disE"); disC = (float)o->getProperty("disC");
        tasteModel.fromVar(o->getProperty("tasteML"));
        likedFeatureN = (int)o->getProperty("likedFeatureN");
        dislikedFeatureN = (int)o->getProperty("dislikedFeatureN");
        if (auto* la = o->getProperty("likes").getArray())
            for (int i = 0; i < 8 && i < la->size(); ++i) likeCounts[(size_t)i] = (int)(*la)[i];
        if (auto* da = o->getProperty("dislikes").getArray())
            for (int i = 0; i < 8 && i < da->size(); ++i) dislikeCounts[(size_t)i] = (int)(*da)[i];
        if (auto* la = o->getProperty("likedFeatures").getArray())
            for (int i = 0; i < 8 && i < la->size(); ++i) likedFeatures[(size_t)i] = (float)(*la)[i];
        if (auto* da = o->getProperty("dislikedFeatures").getArray())
            for (int i = 0; i < 8 && i < da->size(); ++i) dislikedFeatures[(size_t)i] = (float)(*da)[i];
    }
}

// --- Preset Management ---------------------------------------------------
void MidiForgeAudioProcessor::loadPreset(const presets::PresetData& preset)
{
    setRoot(preset.root);
    setGenre(preset.genre);
    setScale(preset.scale);
    progression = preset.progression;
    rhythm = preset.rhythm;
    setMood(preset.mood);
    setMelodyType(preset.melodyType);
    setSoundTarget(preset.soundTarget);
    
    setChordDensity(preset.chordDensity);
    setBassDensity(preset.bassDensity);
    setMelodyDensity(preset.melodyDensity);
    setArpDensity(preset.arpDensity);
    
    setSwing(preset.swing);
    setHumanize(preset.humanize);
    complexity = preset.complexity;
    
    melodyLength = preset.melodyLength;
    pauseChance = preset.pauseChance;
    leapChance = preset.leapChance;
    ghostChance = preset.ghostChance;
    
    voicingWidth = preset.voicingWidth;
    motifStrength = preset.motifStrength;
    variationAmount = preset.variationAmount;
    fillAmount = preset.fillAmount;
    energy = preset.energy;
    
    arpRate = preset.arpRate;
    chordExtensions = preset.chordExtensions;
    inversions = preset.inversions;
    chordsEnabled = preset.chordsEnabled;
    bassEnabled = preset.bassEnabled;
    melodyEnabled = preset.melodyEnabled;
    arpEnabled = preset.arpEnabled;
    hookMode = preset.hookMode;
    drumsEnabled = preset.drumsEnabled;
    articulation = preset.articulation;
    
    // DNA parameters
    setDnaMelody(preset.dnaMelody);
    setDnaRhythm(preset.dnaRhythm);
    setDnaHarmony(preset.dnaHarmony);
    setDnaMotif(preset.dnaMotif);
    setDnaRegister(preset.dnaRegister);
    setDnaGroove(preset.dnaGroove);
    setDnaEnergy(preset.dnaEnergy);
    setDnaSurprise(preset.dnaSurprise);
    
    // Layer locks
    setLockChords(preset.lockChords);
    setLockBass(preset.lockBass);
    setLockMelody(preset.lockMelody);
    setLockArp(preset.lockArp);
    
    regenerate();
}

void MidiForgeAudioProcessor::saveCurrentSettingsAsPreset(const juce::String& name)
{
    presets::PresetData preset;
    preset.name = name;
    
    preset.root = rootPc;
    preset.genre = genre;
    preset.scale = scale;
    preset.progression = progression;
    preset.rhythm = rhythm;
    preset.mood = mood;
    preset.melodyType = melodyType;
    preset.soundTarget = soundTarget;
    
    preset.chordDensity = chordDensity;
    preset.bassDensity = bassDensity;
    preset.melodyDensity = melodyDensity;
    preset.arpDensity = arpDensity;
    
    preset.swing = swing;
    preset.humanize = humanize;
    preset.complexity = complexity;
    
    preset.melodyLength = melodyLength;
    preset.pauseChance = pauseChance;
    preset.leapChance = leapChance;
    preset.ghostChance = ghostChance;
    
    preset.voicingWidth = voicingWidth;
    preset.motifStrength = motifStrength;
    preset.variationAmount = variationAmount;
    preset.fillAmount = fillAmount;
    preset.energy = energy;
    
    preset.arpRate = arpRate;
    preset.chordExtensions = chordExtensions;
    preset.inversions = inversions;
    preset.chordsEnabled = chordsEnabled;
    preset.bassEnabled = bassEnabled;
    preset.melodyEnabled = melodyEnabled;
    preset.arpEnabled = arpEnabled;
    preset.hookMode = hookMode;
    preset.drumsEnabled = drumsEnabled;
    preset.articulation = articulation;
    
    preset.dnaMelody = dnaMelody;
    preset.dnaRhythm = dnaRhythm;
    preset.dnaHarmony = dnaHarmony;
    preset.dnaMotif = dnaMotif;
    preset.dnaRegister = dnaRegister;
    preset.dnaGroove = dnaGroove;
    preset.dnaEnergy = dnaEnergy;
    preset.dnaSurprise = dnaSurprise;
    
    preset.lockChords = lockChordsLayer;
    preset.lockBass = lockBassLayer;
    preset.lockMelody = lockMelodyLayer;
    preset.lockArp = lockArpLayer;
    
    // Save to file using PresetManager
    presets::PresetManager manager;
    manager.savePreset(preset);
}

// --- Generation ---------------------------------------------------------
void MidiForgeAudioProcessor::addChords(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
    // 0.38 Harmony Foundation.
    // Root, third and fifth are ALWAYS present: density no longer deletes chord
    // tones (it used to leave dyads, single notes or empty bars).  Density now
    // controls fullness (extensions / doubled root).  The voicing is chosen by
    // simple voice leading: the inversion closest to the previous chord that
    // fits the chord lane.
    juce::ignoreUnused(e);
    std::vector<int> chordDegrees={degree,degree+2,degree+4};
    const float hDNA = juce::jlimit(0.0f,1.0f,dnaHarmony);
    const float fullness = juce::jlimit(0.0f,1.0f,chordDensity);
    const float extScale = 0.55f + 0.45f*fullness;
    const bool addSeventh = chordExtensions && (complexity>0.40f || hDNA>0.48f) && r.nextFloat() < (0.28f+0.52f*hDNA)*extScale;
    const bool addNinth = chordExtensions && hDNA>0.62f && complexity>0.58f && r.nextFloat() < (0.12f+0.28f*hDNA)*extScale;
    if(addSeventh) chordDegrees.push_back(degree+6);
    if(addNinth) chordDegrees.push_back(degree+8);

    int lo=48, hi=72;
    registerLane(0,lo,hi);

    // Closed-position stack, ascending from the root (all notes diatonic).
    std::vector<int> stack;
    stack.push_back(foldIntoLane(degreeToPitch(degree,3),lo,lo+11));
    for(size_t k=1;k<chordDegrees.size();++k)
    {
        const int prev=stack.back();
        const int raw=degreeToPitch(chordDegrees[k],3);
        stack.push_back(prev+1+(((raw-prev-1)%12)+12)%12);
    }
    const int n=(int)stack.size();

    // Centre of the previous chord (voice leading reference).
    float prevCentre=-1.0f;
    if(barOffset>0)
    {
        // first chord hit of the previous bar (comping patterns do not always start on step 0)
        int firstStep=1000000;
        for(const auto& ev : s.notes)
            if(ev.channel==1 && ev.step>=(barOffset-1)*16 && ev.step<barOffset*16) firstStep=std::min(firstStep,ev.step);
        float sum=0.0f; int cnt=0;
        for(const auto& ev : s.notes)
            if(ev.channel==1 && ev.step==firstStep) { sum+=(float)ev.note; ++cnt; }
        if(cnt>0) prevCentre=sum/(float)cnt;
    }
    const float laneCentre=0.5f*(float)(lo+hi);

    std::vector<int> best=stack;
    float bestCost=1.0e9f;
    const int maxInv=inversions ? n : 1;
    for(int inv=0;inv<maxInv;++inv)
        for(int shift=-12;shift<=12;shift+=12)
        {
            std::vector<int> v=stack;
            for(int k=0;k<inv;++k){ v.push_back(v.front()+12); v.erase(v.begin()); }
            for(auto& p : v) p+=shift;
            float mean=0.0f; for(int p : v) mean+=(float)p; mean/=(float)n;
            const int span=v.back()-v.front();
            float cost=0.35f*std::abs(mean-laneCentre);
            cost += (prevCentre>=0.0f) ? std::abs(mean-prevCentre) : 0.6f*std::abs(mean-laneCentre);
            for(int p : v){ if(p<lo) cost+=2.0f*(float)(lo-p); if(p>hi) cost+=2.0f*(float)(p-hi); }
            cost += 0.8f*(float)juce::jmax(0,span-14);
            cost += 0.3f*(float)inv;
            for(size_t q=0;q+1<v.size();++q){ const int gapSt=v[q+1]-v[q]; if(gapSt==1) cost+=6.0f; else if(gapSt==2) cost+=1.0f; }
            cost += 2.0f*(float)(hash32(generationSeed ^ (uint32_t)(barOffset*97+inv*31+(shift+12)))%100u)/100.0f;
            if(cost<bestCost){ bestCost=cost; best=v; }
        }

    // Wider voicing: lift the middle voice by an octave (keeps the span sane).
    const int spread=juce::jlimit(0,2,(int)std::round(voicingWidth*2.0f));
    if(spread>=1 && best.size()>=3
       && (hash32(generationSeed ^ (uint32_t)(barOffset*53+degree*11))%100u) < (uint32_t)(35*spread))
    {
        const int lifted=best[1]+12;
        if(lifted<=hi+2 && (juce::jmax(best.back(),lifted)-best.front())<=19)
        {
            best[1]=lifted;
            std::sort(best.begin(),best.end());
        }
    }
    // Fullness: double the root on top of a plain triad.
    if(fullness>0.75f && best.size()==3 && r.nextFloat()<0.35f)
    {
        const int top=best.front()+12;
        if(top<=hi+4 && top>best.back()) best.push_back(top);
    }

    const auto prof=soundProfileFor(soundTarget);

    // 0.44 Chord comping.  Chords used to be one held block per bar.  Now they are played as
    // a rhythm: each genre has its own comping cells (offbeat house stabs, boom-bap
    // long-short-long, afro 3-3-2 ...).  The cell repeats (A A' B A'') like the melody, so the
    // chords form a pattern.  Sound profiles that already stab (Pluck / Brass / Guitar) keep
    // their own two hits; Chords = Held keeps the old sustained block.
    struct Hit { int step, len, dv; };
    std::vector<Hit> hits;
    bool comping=false;
    if(prof.chordTwoHits)
        hits={{0,prof.chordLen,0},{8,prof.chordLen,-6}};
    else
    {
        const bool rhythmicGenre = genre==House||genre==Techno||genre==DnB||genre==Trap||genre==Drill||genre==Jersey
                                   ||genre==Afro||genre==BoomBap||genre==RnB||genre==Lofi||genre==Hyperpop;
        comping = (chordStyle==2) || (chordStyle==0 && rhythmicGenre);
        if(!comping) hits={{0,prof.chordLen,0}};
        else
        {
            std::vector<std::vector<Hit>> cells;
            if(genre==House||genre==Techno)
                cells={{{2,2,0},{6,2,-4},{10,2,0},{14,2,-4}},{{0,2,0},{6,2,-4},{10,3,0}},{{0,3,0},{4,2,-6},{8,3,-2},{12,2,-6}}};
            else if(genre==DnB)
                cells={{{0,3,0},{6,2,-6},{10,4,-2}},{{0,4,0},{8,3,-4},{12,2,-6}}};
            else if(genre==Trap||genre==Drill)
                cells={{{0,10,0},{10,6,-8}},{{0,12,0},{12,4,-8}},{{0,6,0},{8,8,-6}}};
            else if(genre==Jersey)
                cells={{{0,3,0},{6,3,-4},{10,3,-4},{14,2,-6}},{{0,2,0},{6,2,-4},{8,3,-2},{12,3,-4}}};
            else if(genre==Afro)
                cells={{{0,3,0},{3,3,-6},{6,4,-4},{10,3,-4},{12,4,-6}},{{0,4,0},{6,3,-4},{10,3,-4}}};
            else if(genre==BoomBap)
                cells={{{0,6,0},{6,3,-6},{10,6,-4}},{{0,5,0},{6,4,-6},{10,6,-2}}};
            else if(genre==RnB||genre==Lofi)
                cells={{{0,6,0},{6,4,-6},{10,6,-4}},{{0,8,0},{10,6,-6}},{{0,4,0},{4,3,-6},{8,6,-4},{12,4,-6}}};
            else
                cells={{{0,4,0},{4,4,-4},{8,4,-2},{12,4,-4}},{{0,6,0},{6,2,-6},{8,8,-4}}};
            const int cyc=(barOffset%juce::jmax(1,bars))%4;
            const uint32_t cSeed=hash32(generationSeed ^ (uint32_t)genre*0xc2b2ae35u ^ 0x00C0FFEEu ^ (cyc==2 ? 0xB2B2B2B2u : 0u));
            hits=cells[(size_t)(cSeed%(uint32_t)cells.size())];
        }
    }
    for(size_t hi2=0;hi2<hits.size();++hi2)
    {
        const auto& ht=hits[hi2];
        for(int i=0;i<(int)best.size();++i)
        {
            const int vel=juce::jlimit(40,96,76-i*4+(i==0?5:0)+ht.dv+(int)(hash32(generationSeed ^ (uint32_t)(barOffset*61+ht.step*7+i))%5u)-2);
            s.notes.push_back({barOffset*16+ht.step,juce::jmax(1,ht.len),juce::jlimit(24,108,best[(size_t)i]),vel,1,false});
        }
    }

    // Genre-aware rhythmic chord punctuation, still scale-safe.
    if(!comping && !prof.chordTwoHits && (genre==House||genre==Techno||genre==Jersey) && r.nextFloat()<(0.35f+0.45f*e))
        s.notes.push_back({barOffset*16+8,4,juce::jlimit(24,108,foldIntoLane(degreeToPitch(degree,3),lo+12,hi+12)),63,1,false});
    if(!comping && !prof.chordTwoHits && chordExtensions && hDNA>0.55f && r.nextFloat()<(0.08f+0.20f*hDNA))
    {
        const int accent=juce::jlimit(24,108,foldIntoLane(degreeToPitch(degree+8,3),lo+12,hi+12));
        bool clash=false;
        for(int p : best) if(std::abs(p-accent)<=1) clash=true;
        if(!clash) s.notes.push_back({barOffset*16+8,8,accent,60,1,false});
    }
}
void MidiForgeAudioProcessor::addBass(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
    // 0.38 Bass Foundation: a guaranteed root on beat 1 of every bar, the rest
    // of the genre pattern is optional.  The bass lives in its own lane
    // (E1..E3) instead of drifting down to inaudible sub-frequencies.
    int lo=28, hi=52;
    registerLane(1,lo,hi);
    const int root=foldIntoLane(degreeToPitch(degree,2),lo,hi);
    std::vector<int> steps;
    if(genre==Trap || genre==Drill)steps={0,3,6,10,14};
    else if(genre==House || genre==Techno || genre==DnB)steps={0,4,8,12};
    else if(genre==Jersey || genre==Afro)steps={0,3,8,11,14};
    else if(genre==RnB || genre==Lofi)steps={0,8,12};
    else steps={0,8,12};
    const float hitChance=juce::jlimit(0.20f,0.95f,bassDensity*(0.62f+0.50f*e));
    for(int x:steps){
        const bool anchor=(x==0);
        if(!anchor && (!rhythmHit(x)||r.nextFloat()>hitChance))continue;
        int note=root;
        if(!anchor && complexity>0.5f&&r.nextFloat()<0.22f)note+=r.nextBool()?7:-5;
        note=foldIntoLane(note,lo,hi);
        bool ghost=r.nextFloat()<ghostChance*.5f&&x!=0;
        int len=(genre==House||genre==Techno||genre==DnB)?3:
                ((genre==RnB||genre==Lofi)?6:(x==0?7:3));
        int vel=juce::jlimit(1,127,92+(x==0?7:0)-(ghost?20:0));
        s.notes.push_back({barOffset*16+x,len,note,vel,2,ghost});
    }
    // FLAGSHIP: sub drop an octave down on the strong beat (trap/cinematic),
    // only when it stays above the audible floor.
    if((genre==Trap||genre==Cinematic) && e>0.5f && r.nextFloat()<0.35f && root-12>=lo)
        s.notes.push_back({barOffset*16,8,root-12,100,2,false});
}
void MidiForgeAudioProcessor::addDrums(Section& s,int barOffset,float e,juce::Random&,int variationSalt)
{
    // 0.44 Drums - an optional layer (DRUMS button, off by default).  Internal channel 5,
    // written to the MIDI file as General-MIDI channel 10 (kick 36, snare 38, clap 39,
    // closed hat 42, open hat 46, crash 49, toms 45/47/50, shaker 70).
    // Genre grooves; the kick cell repeats (A A' B A''); the last bar of a phrase may get a
    // fill (Fill Amount); the 808 line locks to the kick when drums are on.
    enum { Kick=36, Snare=38, Clap=39, HatC=42, HatO=46, Crash=49, Shaker=70 };
    const int loopBar=barOffset%juce::jmax(1,bars);
    const int cycle=loopBar%4;
    const uint32_t loopSeed=hash32(generationSeed ^ (uint32_t)variationSalt*0x9e3779b9u ^ (uint32_t)genre*0xc2b2ae35u ^ 0x00D20005u);
    const uint32_t idSeed=(cycle==2) ? hash32(loopSeed ^ 0xB2B2B2B2u ^ (uint32_t)(barOffset/4)*0x27d4eb2du) : loopSeed;

    using Cell=std::vector<int>;
    std::vector<Cell> kicks; Cell snares;
    int hatMode=1;                 // 0 none, 1 eighths, 2 sixteenths (with drop-outs), 3 open off-beats + light closed
    bool clap=false, both=false, shaker=false;
    switch(genre)
    {
        case Trap:     kicks={{0,10},{0,7,10},{0,6,10},{0,3,7,10}}; snares={8}; hatMode=2; clap=true; both=true; break;
        case Drill:    kicks={{0,10},{0,3,10},{0,6,11}};            snares={8}; hatMode=1; clap=true; both=true; break;
        case House:    kicks={{0,4,8,12}};                          snares={4,12}; hatMode=3; clap=true; break;
        case Techno:   kicks={{0,4,8,12}};                          snares={12};   hatMode=2; clap=true; break;
        case DnB:      kicks={{0,10},{0,6,10}};                     snares={4,12}; hatMode=1; break;
        case BoomBap:  kicks={{0,10},{0,7,10},{0,2,10}};            snares={4,12}; hatMode=1; break;
        case RnB:
        case Lofi:     kicks={{0,10},{0,7,10}};                     snares={4,12}; hatMode=1; break;
        case Afro:     kicks={{0,6,10},{0,3,6,10}};                 snares={4,12}; hatMode=0; shaker=true; break;
        case Jersey:   kicks={{0,3,6,10},{0,3,7,10}};               snares={4,12}; hatMode=1; clap=true; break;
        case Ambient:
        case Cinematic: kicks={{0},{0,8}};                          snares={};    hatMode=0; break;
        default:       kicks={{0,8},{0,6,8},{0,8,10}};              snares={4,12}; hatMode=1; break;
    }
    const Cell& kick=kicks[(size_t)(hash32(idSeed ^ 0x11u)%(uint32_t)kicks.size())];

    const bool phraseEnd=(cycle==3)||(barOffset==bars-1);
    const float fillChance=juce::jlimit(0.0f,0.95f,fillAmount*2.2f);
    const bool fill=phraseEnd && snares.size()>0 &&
        (float)(hash32(idSeed ^ 0xF111u ^ (uint32_t)(barOffset/4)*0x9e3779b9u)%1000u)/1000.0f < fillChance;
    const int fillStart=fill ? 12 : 16;
    const bool tomFill=fill && (hash32(idSeed ^ 0x70Du)%2u)==0u;

    auto jitter=[&](int x,int spread){ return (int)(hash32(idSeed ^ (uint32_t)(barOffset*131+x*17+3))%(uint32_t)(2*spread+1))-spread; };
    auto put=[&](int x,int len,int note,int vel){ s.notes.push_back({barOffset*16+x,len,note,juce::jlimit(1,127,vel),5,false}); };

    for(int x:kick) if(x<fillStart) put(x,1,Kick,(x==0 ? 118 : 108)+jitter(x,4));
    for(int x:snares)
        if(x<fillStart)
        {
            put(x,1,clap ? Clap : Snare,108+jitter(x,4));
            if(both) put(x,1,Snare,82+jitter(x+1,4));
        }
    if((genre==BoomBap||genre==RnB||genre==Lofi) && (float)(hash32(idSeed ^ 0x6057u)%1000u)/1000.0f < ghostChance*2.0f)
        put(11,1,Snare,40+jitter(11,3));                                  // ghost snare

    for(int x=0;x<16;++x)
    {
        if(x>=fillStart) break;
        const bool downbeat=(x%4==0), eighth=(x%2==0);
        if(hatMode==1 && eighth)                       put(x,1,HatC,(downbeat ? 84 : 66)+jitter(x,5));
        else if(hatMode==2)
        {
            if(!eighth && (hash32(idSeed ^ (uint32_t)(barOffset*29+x*13))%100u) < 14u) continue;      // drop-outs
            if(x==14 && (hash32(idSeed ^ 0x0FEu)%100u) < 40u) { put(14,2,HatO,80); ++x; continue; }  // open hat
            put(x,1,HatC,(downbeat ? 84 : eighth ? 68 : 56)+jitter(x,5));
        }
        else if(hatMode==3)
        {
            if(x%4==2)                                  put(x,2,HatO,78+jitter(x,3));
            else if(!eighth && (hash32(idSeed ^ (uint32_t)(barOffset*17+x))%100u) < 55u) put(x,1,HatC,48+jitter(x,4));
        }
        if(shaker) put(x,1,Shaker,(x%4==2 ? 56 : 42)+jitter(x,4));
    }
    if(fill)
    {
        if(tomFill)
        {
            static const int toms[4]={50,47,45,43};
            for(int i=0;i<4;++i) put(12+i,1,toms[i],84+6*i+jitter(i,3));
        }
        else
            for(int x=12;x<16;++x) put(x,1,Snare,68+11*(x-12)+jitter(x,3));         // snare roll, rising
    }
    if(barOffset==0 && (genre==House||genre==Techno||genre==GenrePop||genre==Hyperpop||genre==Universal)
       && (hash32(idSeed ^ 0xC2A5u)%100u)<30u)
        put(0,4,Crash,96);
}
void MidiForgeAudioProcessor::add808(Section& s,int barOffset,float e,juce::Random&,int variationSalt)
{
    // 0.43.2 - a real 808 line.  The 808 profile used to run the melody generator in a low
    // register: several long, overlapping, scale-wandering notes that read like held chords.
    // An 808 is ONE voice that plays the roots of the progression in a kick-locked rhythm:
    //  - strictly monophonic (one note at a time, no overlaps)
    //  - E1..D3, beat 1 of every bar is the chord root
    //  - other hits: mostly root, sometimes the fifth or the octave
    //  - phrase ends may step into the next chord root
    //  - the rhythm cell repeats (A A' B A'') so it becomes a pattern, not a random line
    int lo=28, hi=50;
    registerLane(2,lo,hi);
    const auto prog=progressionDegrees();
    const int degree=prog[(size_t)(barOffset%(int)prog.size())];
    const int nextDeg=prog[(size_t)((barOffset+1)%(int)prog.size())];
    const int loopBar=barOffset%juce::jmax(1,bars);
    const int cycle=loopBar%4;

    const uint32_t loopSeed=hash32(generationSeed ^ (uint32_t)variationSalt*0x9e3779b9u ^ (uint32_t)genre*0xc2b2ae35u ^ 0x00808808u);
    const uint32_t idSeed=(cycle==2) ? hash32(loopSeed ^ 0xB2B2B2B2u ^ (uint32_t)(barOffset/4)*0x27d4eb2du) : loopSeed;

    // Octave placement follows the previous note (a bass line moves in small steps between roots).
    int prevNote=-1;
    for(auto it=s.notes.rbegin(); it!=s.notes.rend(); ++it)
        if(it->channel==3 && it->step<barOffset*16) { prevNote=it->note; break; }
    auto nearestPlacement=[&](int pitchAny,int ref)
    {
        int best=foldIntoLane(pitchAny,lo,hi);
        for(int k=-3;k<=3;++k)
        {
            const int cand=pitchAny+12*k;
            if(cand<lo||cand>hi) continue;
            if(std::abs(cand-ref)<std::abs(best-ref)) best=cand;
        }
        return best;
    };
    const int root=nearestPlacement(degreeToPitch(degree,2), prevNote>=0 ? prevNote : 38);
    const int fifth=(root+7<=hi) ? root+7 : root-5;
    const int nextRoot=nearestPlacement(degreeToPitch(nextDeg,2),root);
    int octaveNote=root+12; if(octaveNote>hi) octaveNote=root-12; if(octaveNote<lo) octaveNote=root;

    // rhythm cells, sorted from sparse to busy (Melody Density and energy pick the busier ones)
    using Cell=std::vector<int>;
    std::vector<Cell> fam;
    if(genre==Trap||genre==Drill)                     fam={{0},{0,10},{0,6,10},{0,7,10},{0,6,8,14},{0,3,8,11},{0,3,6,10,14}};
    else if(genre==House||genre==Techno||genre==DnB)  fam={{0,8},{0,4,10},{0,6,8,14},{0,4,8,12}};
    else if(genre==Jersey||genre==Afro)               fam={{0,10},{0,3,6,10},{0,6,10},{0,3,8,11,14}};
    else if(genre==RnB||genre==Lofi||genre==BoomBap||genre==Ambient||genre==Cinematic)
                                                      fam={{0},{0,10},{0,8},{0,6,12}};
    else                                              fam={{0},{0,10},{0,8},{0,6,10},{0,4,8,12}};
    std::stable_sort(fam.begin(),fam.end(),[](const Cell& a,const Cell& b){ return a.size()<b.size(); });
    const float uRnd=(float)(hash32(idSeed ^ 0x808u)%1000u)/1000.0f;
    const float uDens=juce::jlimit(0.0f,0.999f,0.45f*uRnd+0.55f*melodyDensity+0.10f*(e-0.5f));
    Cell pat=fam[(size_t)((int)(uDens*(float)fam.size()))];
    if(drumsEnabled)
    {
        // lock to the kick drum: the 808 plays where the kick plays (0.44)
        Cell kp;
        for(const auto& ev : s.notes)
            if(ev.channel==5 && ev.note==36 && ev.step>=barOffset*16 && ev.step<barOffset*16+16) kp.push_back(ev.step-barOffset*16);
        std::sort(kp.begin(),kp.end());
        kp.erase(std::unique(kp.begin(),kp.end()),kp.end());
        while(kp.size()>5) kp.erase(kp.begin()+1+(long)(hash32(idSeed ^ (uint32_t)kp.size())%(uint32_t)(kp.size()-1)));
        if(!kp.empty()) pat=kp;
    }

    const bool phraseEnd=(cycle==3)||(barOffset==bars-1);
    for(size_t k=0;k<pat.size();++k)
    {
        const int x=pat[k];
        const uint32_t hk=hash32(idSeed ^ (uint32_t)(k*197+31));
        const unsigned pr=hk%100u;
        int pitch=root;
        if(k>0)
        {
            if(pr<12) pitch=fifth;
            else if(pr<20) pitch=octaveNote;
        }
        const bool lastHit=(k+1==pat.size());
        if(lastHit && phraseEnd && k>0 && (hash32(idSeed ^ 0x51ed270bu ^ (uint32_t)(barOffset/4))%100u)<60u)
            pitch=foldIntoLane(snapToScale(nextRoot + (nextRoot>=root ? -2 : 2)),lo,hi);   // step into the next root
        const int nextX=lastHit ? 16 : pat[k+1];
        const int gap=nextX-x;
        int len=(gap<=3) ? gap : gap-1;                       // small breath before the next hit, never an overlap
        len=juce::jlimit(1,juce::jmax(1,16-x),len);
        const int vel=juce::jlimit(70,118,(x==0 ? 108 : 98)+(int)(hk%9u)-4);
        s.notes.push_back({barOffset*16+x,len,juce::jlimit(lo,hi,pitch),vel,3,false});
    }
}
void MidiForgeAudioProcessor::addMelody(
Section& s, int barOffset, float e, juce::Random& r,
const std::vector<NoteEvent>* inherited, int variationSalt)
{
    // 0.18 Magic Composition Engine
    // Melody is composed as a small loop idea, not as a stream of locally
    // scored notes.  The important units are: archetype -> rhythm -> motif ->
    // pitch contour -> mutation.  This deliberately leaves space and avoids
    // the old "walk the scale" behaviour (1-2-1-2-1-2).
    juce::ignoreUnused(inherited, r);

    const bool soundCloud = leadStyleSoundCloud;
    const bool hook = hookMode;

    // 0.22 Phrase Memory + Humanization: Musical DNA is now multi-axis. Genre is only one
    // dimension; mood, melody role and era alter the composition language too.
    float moodSpace = 0.0f, moodLeap = 0.0f, moodDensity = 0.0f, moodTension = 0.0f;
    switch (mood)
    {
        case DarkMood:        moodSpace=.08f; moodLeap=.10f; moodDensity=-.04f; moodTension=.24f; break;
        case MelancholicMood: moodSpace=.16f; moodLeap=-.05f; moodDensity=-.08f; moodTension=.18f; break;
        case EuphoricMood:    moodSpace=-.10f; moodLeap=.10f; moodDensity=.10f; moodTension=-.08f; break;
        case AggressiveMood:  moodSpace=-.08f; moodLeap=.24f; moodDensity=.14f; moodTension=.12f; break;
        case DreamyMood:      moodSpace=.24f; moodLeap=-.10f; moodDensity=-.12f; moodTension=.04f; break;
        case NostalgicMood:   moodSpace=.08f; moodLeap=-.02f; moodDensity=-.02f; moodTension=.10f; break;
        case MysteriousMood:  moodSpace=.18f; moodLeap=.12f; moodDensity=-.06f; moodTension=.22f; break;
        case EnergeticMood:   moodSpace=-.14f; moodLeap=.16f; moodDensity=.18f; moodTension=-.02f; break;
        default: break;
    }
    const float typeSpace[]   = {-.04f,.16f,-.02f,.18f,.02f,.10f,.28f,.04f};
    const float typeDensity[] = {.04f,-.10f,.06f,-.08f,.12f,-.04f,-.18f,.02f};
    const float typeLeap[]    = {.02f,.04f,.18f,-.02f,.08f,.12f,.06f,.10f};
    const float typeMotif[]   = {.16f,.12f,.10f,.18f,.04f,.08f,.14f,.10f};
    const float eraSync[] = {-.05f,.02f,.08f,.12f,.16f,.20f};
    const float eraSpace[] = {.02f,-.02f,.02f,-.01f,.02f,.04f};
    const float eraNovelty[] = {.04f,.02f,.06f,.08f,.12f,.16f};
    const float roleSpace = typeSpace[juce::jlimit(0,7,melodyType)];
    const float roleDensity = typeDensity[juce::jlimit(0,7,melodyType)];
    const float roleLeap = typeLeap[juce::jlimit(0,7,melodyType)];
    const float roleMotif = typeMotif[juce::jlimit(0,7,melodyType)];

    // 0.19 Musical DNA: genre is no longer a cosmetic label.  Each genre
    // supplies a compact compositional bias that affects rhythm, space,
    // register, leap size and motif behaviour.  The values are deliberately
    // soft preferences so MAGIC can still surprise us instead of producing
    // a rigid genre template.
    float dnaSpace = 0.50f, dnaLeap = 0.25f, dnaSync = 0.25f;
    float dnaDensity = 0.50f, dnaRegister = 0.50f, dnaMotif = 0.65f;
    int dnaRhythmBias = 0;
    switch (genre)
    {
        case Trap:       dnaSpace=.68f; dnaLeap=.38f; dnaSync=.58f; dnaDensity=.40f; dnaRegister=.58f; dnaMotif=.82f; dnaRhythmBias=0; break;
        case House:      dnaSpace=.28f; dnaLeap=.18f; dnaSync=.38f; dnaDensity=.72f; dnaRegister=.46f; dnaMotif=.74f; dnaRhythmBias=1; break;
        case Techno:     dnaSpace=.38f; dnaLeap=.20f; dnaSync=.42f; dnaDensity=.62f; dnaRegister=.42f; dnaMotif=.72f; dnaRhythmBias=2; break;
        case BoomBap:    dnaSpace=.54f; dnaLeap=.30f; dnaSync=.50f; dnaDensity=.46f; dnaRegister=.52f; dnaMotif=.88f; dnaRhythmBias=3; break;
        case Ambient:    dnaSpace=.82f; dnaLeap=.22f; dnaSync=.18f; dnaDensity=.25f; dnaRegister=.62f; dnaMotif=.48f; dnaRhythmBias=7; break;
        case Cinematic:  dnaSpace=.58f; dnaLeap=.55f; dnaSync=.22f; dnaDensity=.36f; dnaRegister=.70f; dnaMotif=.55f; dnaRhythmBias=5; break;
        case RnB:        dnaSpace=.70f; dnaLeap=.32f; dnaSync=.54f; dnaDensity=.38f; dnaRegister=.58f; dnaMotif=.80f; dnaRhythmBias=5; break;
        case GenrePop:        dnaSpace=.48f; dnaLeap=.28f; dnaSync=.32f; dnaDensity=.55f; dnaRegister=.55f; dnaMotif=.94f; dnaRhythmBias=2; break;
        case Drill:      dnaSpace=.62f; dnaLeap=.48f; dnaSync=.72f; dnaDensity=.36f; dnaRegister=.64f; dnaMotif=.84f; dnaRhythmBias=3; break;
        case DnB:        dnaSpace=.32f; dnaLeap=.42f; dnaSync=.66f; dnaDensity=.76f; dnaRegister=.60f; dnaMotif=.70f; dnaRhythmBias=0; break;
        case Jersey:     dnaSpace=.40f; dnaLeap=.34f; dnaSync=.78f; dnaDensity=.68f; dnaRegister=.54f; dnaMotif=.86f; dnaRhythmBias=3; break;
        case Afro:       dnaSpace=.42f; dnaLeap=.25f; dnaSync=.74f; dnaDensity=.62f; dnaRegister=.48f; dnaMotif=.76f; dnaRhythmBias=5; break;
        case Hyperpop:   dnaSpace=.34f; dnaLeap=.68f; dnaSync=.60f; dnaDensity=.70f; dnaRegister=.76f; dnaMotif=.72f; dnaRhythmBias=7; break;
        case Experimental:dnaSpace=.55f; dnaLeap=.72f; dnaSync=.70f; dnaDensity=.45f; dnaRegister=.78f; dnaMotif=.58f; dnaRhythmBias=7; break;
        case Lofi:       dnaSpace=.76f; dnaLeap=.18f; dnaSync=.36f; dnaDensity=.32f; dnaRegister=.44f; dnaMotif=.68f; dnaRhythmBias=4; break;
        default:         break;
    }

    // Generation identity is part of the musical seed.  Previously the melody
    // seed depended only on variationSalt/genre, so every GENERATE rebuilt the
    // exact same melody when the UI seed was unchanged.
    dnaSpace = juce::jlimit(0.0f, 1.0f, dnaSpace + moodSpace + roleSpace + eraSpace[juce::jlimit(0,5,era)]);
    dnaLeap = juce::jlimit(0.0f, 1.0f, dnaLeap + moodLeap + roleLeap);
    dnaDensity = juce::jlimit(0.0f, 1.0f, dnaDensity + moodDensity + roleDensity);
    dnaMotif = juce::jlimit(0.0f, 1.0f, dnaMotif + roleMotif);

    const uint32_t seed = hash32(generationSeed
                                 ^ (uint32_t) variationSalt * 0x9e3779b9u
                                 ^ (uint32_t) (barOffset + 1) * 0x85ebca6bu
                                 ^ (uint32_t) genre * 0xc2b2ae35u);
    const int loopBar = barOffset % juce::jmax(1, bars);
    const int cycle = loopBar % 4;
    const int phraseCell = (barOffset / 4) % 4;
    const int phraseIdentity = (int)(hash32(seed ^ (uint32_t)(phraseCell + 1) * 0x27d4eb2du) % 4u);

    // 0.38 Loop identity: rhythm, motif and contour grammar are decided once per
    // loop (A) and once per phrase for the contrast bar (B).  Bars of the same
    // role therefore repeat instead of being re-rolled every bar - this is what
    // makes a hook recognisable.  Small per-bar variation still comes from `seed`.
    const uint32_t loopSeed = hash32(generationSeed
                                     ^ (uint32_t) variationSalt * 0x9e3779b9u
                                     ^ (uint32_t) genre * 0xc2b2ae35u
                                     ^ 0x7a3c19e5u);
    const uint32_t identitySeed = (cycle == 2)
        ? hash32(loopSeed ^ 0xB2B2B2B2u ^ (uint32_t)(barOffset / 4) * 0x27d4eb2du)
        : loopSeed;
    int melLo = 62, melHi = 86;
    registerLane(2, melLo, melHi);
    const auto prof = soundProfileFor(soundTarget);
    const bool sparseAllowed = (melodyType == SparseLeadMelody || genre == Ambient);
    const int minNotes = sparseAllowed ? juce::jmin(2, prof.minNotes) : prof.minNotes;
    const int minHits = sparseAllowed ? juce::jmin(2, prof.minHits) : prof.minHits;

    // 0.26 Melody Engine 3.0: give every 4-bar phrase a compositional grammar.
    // A = statement, A' = variation, B = contrast/peak, A'' = return/cadence.
    // The grammar changes the destination of notes rather than merely adding
    // random pitch offsets, so a loop develops an audible arc.
    const int phraseStyle = (int)(hash32(identitySeed ^ 0x4f1bbcd3u) % 6u);
    const float phraseStrength = juce::jlimit(0.35f, 0.92f,
        0.45f + 0.30f * dnaMotif + 0.15f * (1.0f - dnaSurprise));

    auto phraseTargetOffset = [&](int noteIndex, int noteCount) -> int
    {
        if (noteCount <= 0) return 0;
        const float pos = (float)noteIndex / (float)juce::jmax(1, noteCount - 1);

        // Six broad contour grammars. They operate in scale degrees, keeping
        // the result scale-safe while creating different phrase shapes.
        static const int contours[6][5] =
        {
            { 0,  1,  2,  1,  0 }, // rise / settle
            { 0,  2,  1,  3,  0 }, // hook peak
            { 1,  0, -1,  1,  0 }, // fall / return
            { 0, -1,  1,  2,  0 }, // delayed rise
            { 0,  2,  3,  1, -1 }, // high point / release
            { 0, -2,  0,  2,  0 }  // dip / rebound
        };
        int slot = juce::jlimit(0, 4, (int)std::floor(pos * 4.999f));
        return contours[phraseStyle][slot];
    };
    const int archetype = (int) (hash32(generationSeed
                                        ^ (uint32_t) variationSalt * 0x27d4eb2du
                                        ^ (uint32_t) genre * 0x165667b1u
                                        ^ (uint32_t)(dnaRhythmBias + 17) * 0x9e3779b9u) % 16u);

    // Rhythm is intentionally sparse.  These are positions, not mandatory
    // notes: later filtering creates breathing room and phrase punctuation.
    // 16 rhythm identities.  Even positions are the default 1/8 grid; a few
    // profiles deliberately use 16th-note syncopation.  The generator chooses
    // one identity per bar from the generation seed, so repeated GENERATE calls
    // do not collapse onto one groove.
    // 0.27 Rhythm Engine 2.0: a larger vocabulary of phrase-level grooves.
    // Patterns are deliberately grouped by feel: straight, syncopated,
    // sparse, driving and broken.  Odd 16th positions are used only by
    // syncopated patterns; the engine never adds arbitrary timing jitter.
    static const int rhythms[][10] =
    {
        // straight / pocket
        {0, 4, 8, 12, -1,-1,-1,-1,-1,-1},
        {0, 2, 6, 8, 12, 14, -1,-1,-1,-1},
        {0, 4, 6, 10, 12, -1,-1,-1,-1,-1},
        {0, 2, 4, 8, 10, 12, 14, -1,-1,-1},
        // sparse
        {0, 8, -1,-1,-1,-1,-1,-1,-1,-1},
        {0, 6, 12, -1,-1,-1,-1,-1,-1,-1},
        {2, 8, 14, -1,-1,-1,-1,-1,-1,-1},
        {0, 4, 12, 14, -1,-1,-1,-1,-1,-1},
        // syncopated
        {0, 3, 8, 11, 14, -1,-1,-1,-1,-1},
        {0, 6, 7, 12, 15, -1,-1,-1,-1,-1},
        {1, 4, 8, 11, 14, -1,-1,-1,-1,-1},
        {0, 3, 6, 10, 13, -1,-1,-1,-1,-1},
        {0, 5, 8, 11, 15, -1,-1,-1,-1,-1},
        {2, 4, 9, 12, 15, -1,-1,-1,-1,-1},
        // driving / repeated pulse
        {0, 2, 4, 6, 8, 10, 12, 14, -1,-1},
        {0, 4, 6, 8, 12, 14, -1,-1,-1,-1},
        {0, 2, 6, 8, 10, 14, -1,-1,-1,-1},
        // broken / conversational
        {0, 2, 7, 12, -1,-1,-1,-1,-1,-1},
        {0, 5, 6, 12, -1,-1,-1,-1,-1,-1},
        {2, 4, 10, 14, -1,-1,-1,-1,-1,-1},
        {0, 6, 10, 15, -1,-1,-1,-1,-1,-1},
        {0, 4, 9, 12, -1,-1,-1,-1,-1,-1},
        {0, 2, 8, 10, 14, -1,-1,-1,-1,-1},
        {1, 6, 8, 13, -1,-1,-1,-1,-1,-1},
        // 0.39.1 additional grooves (on the 8th/16th grid): pickups, late starts, long-short shapes
        {2, 4, 8, 10, 14, -1,-1,-1,-1,-1},
        {0, 2, 6, 10, 12, 14, -1,-1,-1,-1},
        {2, 6, 8, 10, 12, -1,-1,-1,-1,-1},
        {0, 4, 8, 10, 12, 14, -1,-1,-1,-1},
        {2, 4, 6, 10, 12, 14, -1,-1,-1,-1},
        {0, 2, 4, 8, 12, 14, -1,-1,-1,-1},
        {0, 6, 8, 10, 12, 14, -1,-1,-1,-1},
        {4, 6, 8, 12, 14, -1,-1,-1,-1,-1},
        {0, 2, 8, 12, 14, -1,-1,-1,-1,-1},
        {2, 4, 8, 12, 14, -1,-1,-1,-1,-1},
        {0, 4, 6, 10, 14, -1,-1,-1,-1,-1},
        {2, 6, 10, 12, 14, -1,-1,-1,-1,-1},
        {0, 2, 4, 6, 10, 14, -1,-1,-1,-1},
        {4, 8, 10, 12, 14, -1,-1,-1,-1,-1},
        {0, 4, 8, 10, 14, -1,-1,-1,-1,-1},
        {2, 4, 6, 8, 14, -1,-1,-1,-1,-1}
    };

    // Melodic loops need enough onsets to be heard as a line, not as scattered notes.
    // The groove is picked uniformly among the patterns that qualify (0.38 used to
    // slide to "the next pattern with enough hits", which made one groove
    // - {0,2,8,10,14} - about a quarter of all bars).
    constexpr int kRhythmCount = (int)(sizeof(rhythms) / sizeof(rhythms[0]));
    auto hitCount = [&](int t) { int c = 0; for (int k = 0; k < 10; ++k) if (rhythms[t][k] >= 0) ++c; return c; };
    std::vector<int> eligible;
    for (int t = 0; t < kRhythmCount; ++t)
        if (hitCount(t) >= minHits) eligible.push_back(t);
    if (eligible.empty()) eligible.push_back(0);
    const int eligibleN = (int)eligible.size();
    // Genre DNA nudges the rhythmic family without hard-locking it.
    int rhythmType = eligible[(size_t)((((int)(hash32(identitySeed ^ 0x51ed270bu) % (uint32_t)eligibleN)
                                          + dnaRhythmBias + (int)(dnaSync * 3.0f)) % eligibleN + eligibleN) % eligibleN)];

    std::vector<int> positions;
    for (int i = 0; i < 10; ++i)
    {
        const int x = rhythms[rhythmType][i];
        if (x < 0) break;
        positions.push_back(x);
    }

    // Keep the core rhythm locked to the 1/8-note grid (even 16th-step
    // positions). Off-grid 16th-note syncopation is allowed only for
    // deliberately syncopated archetypes, and only as a small accent.
    const bool allowsOffGrid = dnaSync > 0.55f || rhythmType == 0 || rhythmType == 3 || rhythmType == 5;
    for (auto& x : positions)
    {
        if ((x & 1) != 0 && !allowsOffGrid)
            x = juce::jlimit(0, 15, x - 1);
    }

    // The second half of a 4-bar cell can breathe, but never introduce an
    // arbitrary 1-step shift into an otherwise straight groove.
    if (cycle == 3)
    {
        for (auto& x : positions)
        {
            const uint32_t h = hash32(seed ^ (uint32_t)(x + 17));
            if ((h % 100u) < 12u)
            {
                const int delta = allowsOffGrid ? ((h & 1u) ? 1 : -1) : ((h & 1u) ? 2 : -2);
                x = juce::jlimit(0, 15, x + delta);
            }
        }
    }

    // Always guarantee at least one real rest.  Unlike the previous generator,
    // density is not allowed to turn a melody into a continuous stream.
    const float density = juce::jlimit(0.25f, 0.95f, prof.densityMul *
        (0.64f + 0.30f * melodyDensity + 0.12f * e
        + 0.16f * (dnaDensity - 0.50f) - 0.10f * (dnaSpace - 0.50f)
        - 0.60f * (pauseChance - 0.10f)));
    std::vector<int> chosen;
    auto posRank = [&](int x) { return hash32(identitySeed ^ (uint32_t)(x * 97 + 31)) % 1000u; };
    for (int x : positions)
        if ((float)posRank(x) / 1000.0f < density)
            chosen.push_back(x);
    // Never fall below a playable number of notes: bring back the most
    // "important" removed positions (deterministic per loop identity).
    while ((int)chosen.size() < minNotes && chosen.size() < positions.size())
    {
        int bestPos = -1; uint32_t bestRank = 100000u;
        for (int x : positions)
        {
            if (std::find(chosen.begin(), chosen.end(), x) != chosen.end()) continue;
            const uint32_t rk = posRank(x);
            if (rk < bestRank) { bestRank = rk; bestPos = x; }
        }
        if (bestPos < 0) break;
        chosen.push_back(bestPos);
    }
    std::sort(chosen.begin(), chosen.end());

    // Phrase punctuation: don't fill every bar.  Some loops enter late or leave
    // the last quarter empty, which makes the loop breathe when repeated.
    if ((archetype == 1 || archetype == 7) && cycle == 0 && (int)chosen.size() > minNotes)
        chosen.erase(chosen.begin());
    if (cycle == 3 && (archetype == 2 || archetype == 5 || archetype == 7)
        && (int)chosen.size() > minNotes)
        chosen.pop_back();

    if (chosen.empty())
        chosen.push_back(positions.front());

    // Melody role changes phrasing, not just a label.
    if (melodyType == SparseLeadMelody && chosen.size() > 3)
        chosen.resize(juce::jmax<size_t>(2, chosen.size() - 1));
    else if (melodyType == RiffMelody && chosen.size() < 4 && positions.size() >= 4)
        chosen.push_back(positions[2]);
    else if (melodyType == VocalLikeMelody && cycle == 0 && chosen.size() > 0)
        chosen[0] = 0;
    else if (melodyType == OstinatoMelody && !chosen.empty())
        std::sort(chosen.begin(), chosen.end());

    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

    // Harmonic context is used as gravity, not as a command to resolve every bar.
    const auto prog = progressionDegrees();
    const int degree = prog[(size_t)(barOffset % (int)prog.size())];
    const auto scaleNotes = scaleSemitones();
    const int scaleCount = (int) scaleNotes.size();

    auto pitchForDegree = [&](int d, int oct) -> int
    {
        return degreeToPitch(d, oct);
    };

    auto chordTone = [&](int note) -> bool
    {
        const int tones[] = { degree, degree + 2, degree + 4 };
        for (int d : tones)
        {
            const int pc = pitchForDegree(d, octave) % 12;
            if (((note % 12) + 12) % 12 == ((pc % 12) + 12) % 12)
                return true;
        }
        return false;
    };

    // One compact motif per variation.  Different archetypes use different
    // shapes, so changing a seed changes the identity rather than only the RNG.
    // Expanded motif vocabulary.  The motif is transformed per generation
    // (reverse / inversion / rotation / octave lift) instead of merely picking
    // one fixed six-note pattern.  This is the main source of melodic identity.
    static const int motifs[][6] =
    {
        { 0, 0, 2, 4, 2, -1 }, { 4, 2, 0, 2, 5, -1 },
        { 0, 4, 1, 5, 3, -1 }, { 2, 2, 4, 1, 3, -1 },
        { 0, 3, 5, 2, 0, -1 }, { 4, 1, 4, 6, 2, -1 },
        { 5, 3, 0, 2, 6, -1 }, { 0, 5, 0, 3, 1, -1 },
        { 2, 5, 3, 1, 4, -1 }, { 5, 2, 2, 0, 4, -1 },
        { 1, 4, 0, 3, 5, -1 }, { 3, 0, 2, 6, 4, -1 },
        { 0, 2, 5, 3, 1, -1 }, { 6, 3, 1, 4, 0, -1 },
        { 2, 0, 5, 5, 3, -1 }, { 4, 6, 2, 1, 5, -1 }
    };

    const int motifType = (int)(hash32(identitySeed ^ (uint32_t)(archetype * 0x51ed270bu)
                                           ^ (uint32_t)(dnaMotif * 1000.0f)) % 16u);
    const int motifShift = (int)((identitySeed >> 16) % (uint32_t)scaleCount);

    const int motifTransform = (int)(hash32(identitySeed ^ 0x6d2b79f5u) % 4u);
    const int motifRotation = (int)(hash32(identitySeed ^ 0x1b873593u) % 5u);

    auto motifDegree = [&](int index) -> int
    {
        int pos = (index + motifRotation) % 5;
        if (motifTransform == 1) pos = 4 - pos;
        int raw = motifs[motifType][pos];
        if (motifTransform == 2) raw = 4 - raw;
        if (motifTransform == 3 && (pos & 1)) raw += 2;
        return degree + raw + motifShift - 2;
    };

    // Determine the previous melody note only for continuity at the loop seam.
    int previous = pitchForDegree(motifDegree(0), octave);
    for (const auto& ev : s.notes)
    {
        if (ev.channel == 3 && ev.step >= barOffset * 16 - 16 && ev.step < barOffset * 16)
            previous = ev.note;
    }

    // Build pitches from the motif, with occasional octave displacement and
    // deliberate leaps.  We explicitly reject alternating neighbour-note motion.
    // 0.22 Phrase Memory: the loop remembers the contour of the previous bar.
    // A/A' and A'' should feel related without becoming literal copies.
    // We only use the previous bar as a contour reference; the harmonic context
    // and current bar candidate remain authoritative.
    std::vector<NoteEvent> memoryBar;
    if (barOffset > 0)
    {
        const int prevStart = (barOffset - 1) * 16;
        for (const auto& ev : s.notes)
            if (ev.channel == 3 && ev.step >= prevStart && ev.step < prevStart + 16)
                memoryBar.push_back(ev);
        std::sort(memoryBar.begin(), memoryBar.end(),
                  [](const NoteEvent& a, const NoteEvent& b) { return a.step < b.step; });
    }

    // 0.39 Degree-space planning.  The scale-degree path of the whole bar is planned
    // first (same rules as before), then smoothed and octave-centred as a unit.
    // Working in scale degrees keeps the shape of repeated bars identical even
    // though the chord (and so the transposition) changes from bar to bar - that is
    // what keeps a hook recognisable - while removing the random octave jumps that
    // made lines angular (about half of all intervals used to be >= a minor sixth).
    auto planDegree = [&](size_t i) -> int
    {
        int d = motifDegree((int)i);

        // Each 4-bar cell has a role: A, A', B, A''.  B is the main contrast.
        if (cycle == 2)
        {
            if (archetype == 2 || archetype == 6 || phraseIdentity == 1) d += 2;
            if (dnaLeap > 0.55f && ((i + phraseCell) % 4 == 2))
                d += (i & 1) ? 2 : -2;
            else if (dnaSpace > 0.70f && (i % 3 == 1))
                d = motifDegree((int)i);
            else if (phraseIdentity == 2) d -= 1;
            else d += ((i & 1u) ? -2 : 2);
        }
        else if (cycle == 3)
        {
            // Return toward the motif without forcing a textbook cadence.
            d += (phraseIdentity == 3 ? (i == 0 ? 2 : 0) : (i == 0 ? 1 : -1));
        }

        // Apply the phrase grammar after the A/A'/B/A'' transformation.
        // B receives the strongest contour deviation; A' and A'' retain the
        // identity of the statement but move its destination slightly.
        const int contour = phraseTargetOffset((int)i, (int)chosen.size());
        float contourWeight = (cycle == 2 ? phraseStrength
                               : (cycle == 1 ? phraseStrength * 0.62f
                                             : phraseStrength * 0.48f));
        if (melodyType == OstinatoMelody)
            contourWeight *= 0.45f;
        if (soundCloud)
            contourWeight *= 0.55f;
        if (contour != 0 && contourWeight > 0.1f)
        {
            const int scaled = (int)std::round((float)contour * contourWeight);
            d += scaled;
        }

        // A real phrase peak: on B, one note near the middle of the phrase can
        // enter a higher register. The return bar deliberately avoids repeating
        // that peak at the same location.
        if (cycle == 2 && i == chosen.size() / 2 && dnaRegister > 0.42f)
            d += (dnaRegister > 0.72f ? 4 : 2);
        if (cycle == 3 && i == chosen.size() / 2 && dnaRegister > 0.72f)
            d -= 1;

        return d;
    };

    std::vector<int> dPlan(chosen.size());
    {
        const int big = (scaleCount >= 7) ? 5 : 4;
        std::vector<int> raw(chosen.size());
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            raw[i] = planDegree(i);
            // Octave displacement (formerly `note += 12`) is part of the plan.
            if (((identitySeed >> ((i * 7) & 23)) & 1u) != 0u && archetype >= 2)
                raw[i] += scaleCount;
        }
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            if (i == 0) { dPlan[i] = raw[i]; continue; }
            int delta = raw[i] - raw[i - 1];
            if (std::abs(delta) >= big)
            {
                const bool deliberateLeap =
                    (float)(hash32(identitySeed ^ (uint32_t)(i * 197 + 13)) % 1000u) / 1000.0f < leapChance;
                if (!deliberateLeap)   // nearest octave equivalent: same pitch class, smaller interval
                    delta -= scaleCount * (int)std::lround((double)delta / (double)scaleCount);
            }
            // Weak beats prefer thirds over skips (strong beats keep their harmonic anchor).
            if ((chosen[i] % 4) != 0 && melodyType != OstinatoMelody && melodyType != ArpMelody
                && std::abs(delta) >= 3 && std::abs(delta) <= 4
                && (hash32(identitySeed ^ (uint32_t)(i * 71 + 29)) % 100u) < 35u)
                delta = (delta > 0) ? 2 : -2;
            dPlan[i] = dPlan[i - 1] + delta;
        }

        // Bar-level octave placement: first note near the previous melody note,
        // whole bar inside the melody lane.
        int bestK = 0; float bestCost = 1.0e9f;
        const float laneCentre = 0.5f * (float)(melLo + melHi);
        for (int k = -3; k <= 3; ++k)
        {
            float cost = 0.0f;
            std::vector<int> ps;
            for (size_t i = 0; i < chosen.size(); ++i)
            {
                const int pch = pitchForDegree(dPlan[i] + k * scaleCount, octave);
                ps.push_back(pch);
                if (pch < melLo) cost += 3.0f * (float)(melLo - pch);
                if (pch > melHi) cost += 3.0f * (float)(pch - melHi);
            }
            std::sort(ps.begin(), ps.end());
            cost += 0.25f * std::abs((float)ps[ps.size() / 2] - laneCentre);
            if (barOffset > 0)
                cost += 1.0f * (float)std::abs(pitchForDegree(dPlan[0] + k * scaleCount, octave) - previous);
            if (cost < bestCost) { bestCost = cost; bestK = k; }
        }
        for (auto& dv : dPlan) dv += bestK * scaleCount;
    }

    std::vector<int> generated;
    generated.reserve(chosen.size());

    for (size_t i = 0; i < chosen.size(); ++i)
    {
        const int x = chosen[i];
        int d = dPlan[i];

        int note = pitchForDegree(d, octave);

        // Keep a comfortable lead register and find the nearest useful octave.
        note = foldIntoLane(note, melLo, melHi);
        note = juce::jlimit(melLo, melHi, snapToScale(note));

        if (!generated.empty())
        {
            const int last = generated.back();
            int interval = note - last;

            // Reject the old 1-2-1-2 pathology.  If the last two moves are
            // opposite and tiny, force a different contour or a real jump.
            if (generated.size() >= 2)
            {
                const int a = generated[generated.size() - 1] - generated[generated.size() - 2];
                const int b = note - last;
                if (std::abs(a) <= 3 && std::abs(b) <= 3 && a != 0 && b != 0
                    && ((a > 0) != (b > 0)))
                {
                    const int alternatives[] = { last + (a > 0 ? 5 : -5),
                                                 last + (a > 0 ? 7 : -7),
                                                 last + (a > 0 ? -4 : 4) };
                    for (int candidate : alternatives)
                    {
                        candidate = juce::jlimit(melLo, melHi, snapToScale(candidate));
                        if (std::abs(candidate - last) >= 4
                            && std::abs(candidate - last) <= 8)
                        {
                            note = candidate;
                            break;
                        }
                    }
                }
            }

            // Controlled leap: some archetypes need a recognizable interval,
            // otherwise the generator falls back to scalar motion too often.
            interval = note - last;
            const bool wantsLeap = ((archetype == 2 || archetype == 5 || archetype == 6)
                && (((int)i + (int)(identitySeed & 7u)) % 5 == 2))
                || ((float)(hash32(identitySeed ^ (uint32_t)(i * 131 + 7)) % 1000u) / 1000.0f < leapChance * 0.45f);
            if (wantsLeap && std::abs(interval) < 4)
            {
                const int dir = ((hash32(identitySeed ^ (uint32_t)i) & 1u) ? 1 : -1);
                note = juce::jlimit(melLo, melHi, snapToScale(last + dir * (5 + (int)(identitySeed % 3u))));
            }

            // After a large leap, don't immediately walk back by one step.
            interval = note - last;
            if (generated.size() >= 2)
            {
                const int prior = last - generated[generated.size() - 2];
                if (std::abs(prior) >= 5 && std::abs(interval) <= 2)
                    note = juce::jlimit(melLo, melHi, snapToScale(last + (prior > 0 ? -3 : 3)));
            }
        }

        // Composition profiles: each generation has a different balance of
        // anchor / contrast / register / repetition.  These are musical rules,
        // not random pitch noise, and they are intentionally subtle.
        const int profile = (int)(hash32(identitySeed ^ 0x9e3779b9u) % 8u);
        if (profile == 1 && (i & 1u) == 0)
            note = juce::jlimit(melLo, melHi, snapToScale(note + ((i % 3 == 0) ? 2 : -2)));
        else if (profile == 2 && i == chosen.size() / 2)
            note = juce::jlimit(melLo, melHi, snapToScale(note + 5));
        else if (profile == 3 && (i % 3 == 1))
            note = juce::jlimit(melLo, melHi, snapToScale(note - 5));
        else if (profile == 4 && (i == 0 || i + 1 == chosen.size()))
            note = juce::jlimit(melLo, melHi, snapToScale(note + (i == 0 ? -3 : 3)));
        else if (profile == 5 && i % 4 == 2)
            note = juce::jlimit(melLo, melHi, snapToScale(note + 7));
        else if (profile == 6 && i % 4 == 3)
            note = juce::jlimit(melLo, melHi, snapToScale(note - 7));

        // Reuse a previous-bar contour at controlled strength.  The B bar
        // (cycle 2) gets less memory so it can provide contrast; A'/A'' keep
        // more of the identity. This is phrase memory, not copy/paste.
        if (memoryBar.size() >= 2 && !generated.empty())
        {
            const float memoryStrength = juce::jlimit(0.0f, 0.72f,
                motifStrength * (cycle == 2 ? 0.24f : (cycle == 1 ? 0.52f : 0.44f)));
            const uint32_t mh = hash32(seed ^ (uint32_t)(i * 113 + 701));
            if ((float)(mh % 1000u) / 1000.0f < memoryStrength)
            {
                const auto& ref = memoryBar[i % memoryBar.size()];
                const int refAnchor = memoryBar.front().note;
                const int currentAnchor = generated.front();
                const int contourOffset = ref.note - refAnchor;
                const int target = currentAnchor + contourOffset;
                const int blended = juce::roundToInt((float)note * (1.0f - memoryStrength)
                                                   + (float)target * memoryStrength);
                note = juce::jlimit(melLo, melHi, snapToScale(blended));
            }
        }

        // Harmonic anchor on strong positions, but leave weak positions free.
        if ((x == 0 || x == 8) && !chordTone(note))
        {
            const int root = pitchForDegree(degree, octave);
            const int third = pitchForDegree(degree + 2, octave);
            if ((hash32(identitySeed ^ (uint32_t)(x + 101)) % 100u) < 82u)
                note = (std::abs(root - previous) <= std::abs(third - previous)) ? root : third;
            note = foldIntoLane(note, melLo, melHi);
            note = juce::jlimit(melLo, melHi, snapToScale(note));
        }

        // Final safety net (post-processing above can still create a wide interval):
        // unless it is a deliberate leap, move the note to the octave nearest the
        // previous note.  Pitch class - and so the harmony - is untouched.
        if ((barOffset > 0 || !generated.empty()) && std::abs(note - previous) >= 8
            && (float)(hash32(identitySeed ^ (uint32_t)(i * 197 + 13)) % 1000u) / 1000.0f >= leapChance)
        {
            int bestNote = note;
            for (int k = -3; k <= 3; ++k)
            {
                const int cand = note + 12 * k;
                if (cand < melLo || cand > melHi) continue;
                if (std::abs(cand - previous) < std::abs(bestNote - previous)) bestNote = cand;
            }
            note = bestNote;
        }

        // Sound profile: singable/playable interval limit for this kind of sound.
        if (prof.maxLeap > 0 && (!generated.empty() || barOffset > 0)
            && std::abs(note - previous) > prof.maxLeap)
        {
            int cand = note;
            while (cand - previous > prof.maxLeap && cand - 12 >= melLo) cand -= 12;
            while (previous - cand > prof.maxLeap && cand + 12 <= melHi) cand += 12;
            if (std::abs(cand - previous) > prof.maxLeap)
                cand = juce::jlimit(melLo, melHi, snapToScale(previous + (note > previous ? prof.maxLeap : -prof.maxLeap)));
            note = cand;
        }

        generated.push_back(note);
        previous = note;

        // Duration is part of the phrase identity.  Long notes create space;
        // short notes are reserved for the rhythmic hook.
        int len = 1;
        const uint32_t h = hash32(identitySeed ^ (uint32_t)((int)i * 41 + 9));
        if (archetype == 1 || archetype == 7)
            len = (h % 100u < 42u) ? 2 : 1;
        else if (archetype == 0 || archetype == 5)
            len = (h % 100u < 34u) ? 2 : 1;
        else
            len = (h % 100u < 55u) ? 2 : 1;

        if ((cycle == 0 && i == 0 && archetype != 3) ||
            (cycle == 3 && i == chosen.size() - 1 && (h % 100u < 45u)))
            len = juce::jmin(4, len + 1);
        if (soundCloud)
            len = (h % 100u < 60u) ? 2 : 1;

        // Sparse grooves leave more air; driving grooves connect pulses.
        if (rhythmType >= 4 && rhythmType <= 7 && (h % 100u) < (uint32_t)(25.0f * dnaGroove))
            len = juce::jmin(4, len + 1);
        if (rhythmType >= 14 && (h % 100u) < (uint32_t)(22.0f * dnaGroove))
            len = juce::jmax(1, len - 1);

        len = juce::jmin(len, 16 - x);

        int velocity = 70 + (x % 4 == 0 ? 8 : 0);
        if (cycle == 2) velocity += 5;
        if (hook && (x == 0 || x == 8)) velocity += 4;

        // Rhythm Engine 2.0: groove changes accents and sustain according to
        // the rhythmic identity.  This affects feel without moving the note
        // onset off the selected grid.
        const bool offBeat = (x % 4) != 0;
        const bool backBeat = (x % 8) == 4;
        const float groove = juce::jlimit(0.0f, 1.0f, dnaGroove);
        if (offBeat)
            velocity += (int)std::round(4.0f * groove);
        if (backBeat)
            velocity += (int)std::round(5.0f * groove);
        if (rhythmType >= 8 && rhythmType <= 13 && offBeat)
            velocity += (int)std::round(3.0f * groove);
        if (rhythmType >= 14 && (x % 4) == 0)
            velocity += 2;

        velocity += (int)(h % 7u) - 3;

        // 0.22 Humanization: vary accents and sustain in a musically bounded
        // way. Timing stays on the chosen grid; "human" here means phrasing
        // and dynamics, not random off-grid MIDI.
        const float human = juce::jlimit(0.0f, 1.0f, humanize);
        const int accent = juce::jlimit(-10, 10, (int)std::round((float)((int)(h % 9u) - 4) * (2.0f + 7.0f * human)));
        if (x % 4 == 0) velocity += 2;
        if (cycle == 2 && (x % 8) == 4) velocity += 3;
        velocity += accent;
        if (human > 0.12f && (h % 100u) < (uint32_t)(18.0f * human))
            len = juce::jmin(4, len + 1);
        if (human > 0.18f && (h % 100u) > 88u)
            len = juce::jmax(1, len - 1);
        velocity = juce::jlimit(48, 112, velocity);
        // Synth-like sounds ignore velocity; keep their dynamics flat and consistent.
        velocity = prof.velCenter + (int) std::round((float) (velocity - prof.velCenter) * prof.velSpread);
        velocity = juce::jlimit(40, 118, velocity + (int)(hash32(seed ^ (uint32_t)(i * 13 + 5)) % 5u) - 2);

        // Melody Length: how much of the gap to the next note is sustained.
        // (This slider used to be stored but never applied.)
        {
            const int nextX = (i + 1 < chosen.size()) ? chosen[i + 1] : 16;
            const int gap = juce::jmax(1, nextX - x);
            const float legatoAmt = (prof.legato < 0.0f) ? melodyLength : prof.legato;
            const int sustained = 1 + (int) std::round(legatoAmt * (float) (gap - 1));
            len = juce::jmax(len, juce::jmin(sustained, gap));
            len = juce::jmin(len, prof.maxLen);
            len = juce::jmax(len, juce::jmin(prof.minLen, gap));
            len = juce::jlimit(1, juce::jmax(1, 16 - x), len);
        }
        s.notes.push_back({ barOffset * 16 + x, len, note, velocity, 3, false });
    }

    // 0.41 Fill Amount (this slider used to be stored but never applied).
    // At the end of a phrase (4th bar of the cycle, and the last bar of the loop, which
    // leads back to bar 1) a short scale run of 1-3 sixteenth notes leads into the next
    // bar's chord.  Amount = how often it happens and how long the run is.  Bells and pads
    // keep their long notes and never get fills.
    if ((cycle == 3 || barOffset == bars - 1) && prof.minLen < 3 && fillAmount > 0.02f && !chosen.empty())
    {
        const float chance = juce::jlimit(0.0f, 0.95f, fillAmount * 2.2f);
        const bool doFill = (float)(hash32(identitySeed ^ 0x0F111A5Eu ^ (uint32_t)(barOffset / 4) * 0x9e3779b9u) % 1000u) / 1000.0f < chance;
        int nFill = 1 + (fillAmount > 0.30f ? 1 : 0) + (fillAmount > 0.60f ? 1 : 0);
        const int lastStep = chosen.back();
        while (nFill > 0 && lastStep > 16 - nFill - 1) --nFill;   // the last regular note keeps at least 1 step
        if (doFill && nFill > 0)
        {
            const int nextDeg = prog[(size_t)((barOffset + 1) % (int)prog.size())];
            int target = pitchForDegree(nextDeg, octave);
            int nearest = target;
            for (int k = -3; k <= 3; ++k)
            {
                const int cand = target + 12 * k;
                if (cand < melLo || cand > melHi) continue;
                if (nearest < melLo || nearest > melHi || std::abs(cand - previous) < std::abs(nearest - previous)) nearest = cand;
            }
            target = juce::jlimit(melLo, melHi, nearest);
            const int dir = (previous <= target) ? 1 : -1;
            const int firstFill = 16 - nFill;
            for (auto it = s.notes.rbegin(); it != s.notes.rend(); ++it)
                if (it->channel == 3 && it->step == barOffset * 16 + lastStep)
                { it->length = juce::jmax(1, juce::jmin(it->length, firstFill - lastStep)); break; }
            for (int i = 0; i < nFill; ++i)
            {
                const int pitch = juce::jlimit(melLo, melHi, snapToScale(target - dir * 2 * (nFill - i)));
                const int vel = juce::jlimit(48, 100, 60 + 6 * i + (int)(hash32(seed ^ (uint32_t)(i * 31 + 7)) % 5u));
                s.notes.push_back({ barOffset * 16 + firstFill + i, 1, pitch, vel, 3, false });
            }
        }
    }
}
void MidiForgeAudioProcessor::addArp(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
if(arpDensity<=0.001f)return;
std::array<int,4> c={degreeToPitch(degree,octave),degreeToPitch(degree+2,octave),degreeToPitch(degree+4,octave),degreeToPitch(degree+6,octave)};
int stride=juce::jmax(1,8/juce::jmax(1,arpRate));
for(int x=0;x<16;x+=stride){
if(r.nextFloat()>arpDensity*(0.55f+0.65f*e))continue;
int idx=(x/stride+barOffset)%4;
if((barOffset/2)%2==1)idx=3-idx;
s.notes.push_back({barOffset*16+x,1,snapToScale(c[(size_t)idx]),
64+(x%4==0?8:0),4,false});
}
}
void MidiForgeAudioProcessor::buildSection(Section& section,int sectionIndex,
const std::vector<int>& prog,juce::Random& r,
const std::vector<NoteEvent>* inherited, int variationSalt)
{
const juce::String names[]={"INTRO","VERSE","PRE-CHORUS","CHORUS","BREAK","DROP","OUTRO"};
section.name="LOOP";
section.bars=bars;
// Loop-only architecture: every bar belongs to the musical idea.
// No intro/verse/chorus energy ramps, so generation stays focused on a usable loop.
const float targetEnergy=energy;
section.energy=targetEnergy;
section.densityMultiplier=0.55f+0.65f*targetEnergy;
for(int bar=0;bar<bars;++bar){
int deg=prog[(size_t)((bar+sectionIndex)%prog.size())];
const bool solo=soundProfileFor(soundTarget).soloLine;
if(drumsEnabled)
addDrums(section,bar,targetEnergy,r,variationSalt);
if(chordsEnabled && !solo)
addChords(section,bar,deg,targetEnergy,r);
if(bassEnabled && !soundProfileFor(soundTarget).bassOff)
addBass(section,bar,deg,targetEnergy,r);
if(melodyEnabled)
{
    if(solo) add808(section,bar,targetEnergy,r,variationSalt);
    else addMelody(section,bar,targetEnergy,r,inherited,variationSalt);
}
if(arpEnabled && !solo)
addArp(section,bar,deg,targetEnergy,r);
}
}
void MidiForgeAudioProcessor::buildBaseSong(SongData& song,juce::Random& r, int variationSalt)
{
song.sections.clear();
const auto prog=progressionDegrees();
const int sectionCount=1;
for(int i=0;i<sectionCount;++i){
Section sec;
const std::vector<NoteEvent>* inherited=nullptr;
if(i>0 && !song.sections.empty()){
for(const auto& ev:song.sections[i-1].notes)
if(ev.channel==3){ inherited=&song.sections[i-1].notes; break; }
}
buildSection(sec,i,prog,r,inherited,variationSalt);
song.sections.push_back(std::move(sec));
}
}
void MidiForgeAudioProcessor::buildVariationBank()
{
    // Shared genre DNA for the candidate judge. Keep these targets aligned with
    // addMelody() so the judge rewards the musical language it asked the
    // generator to produce.
    float dnaSpace = 0.50f, dnaDensity = 0.50f, dnaRegister = 0.50f;
    switch (genre)
    {
        case Trap: dnaSpace=.68f; dnaDensity=.40f; dnaRegister=.58f; break;
        case House: dnaSpace=.28f; dnaDensity=.72f; dnaRegister=.46f; break;
        case Techno: dnaSpace=.38f; dnaDensity=.62f; dnaRegister=.42f; break;
        case BoomBap: dnaSpace=.54f; dnaDensity=.46f; dnaRegister=.52f; break;
        case Ambient: dnaSpace=.82f; dnaDensity=.25f; dnaRegister=.62f; break;
        case Cinematic: dnaSpace=.58f; dnaDensity=.36f; dnaRegister=.70f; break;
        case RnB: dnaSpace=.70f; dnaDensity=.38f; dnaRegister=.58f; break;
        case GenrePop: dnaSpace=.48f; dnaDensity=.55f; dnaRegister=.55f; break;
        case Drill: dnaSpace=.62f; dnaDensity=.36f; dnaRegister=.64f; break;
        case DnB: dnaSpace=.32f; dnaDensity=.76f; dnaRegister=.60f; break;
        case Jersey: dnaSpace=.40f; dnaDensity=.68f; dnaRegister=.54f; break;
        case Afro: dnaSpace=.42f; dnaDensity=.62f; dnaRegister=.48f; break;
        case Hyperpop: dnaSpace=.34f; dnaDensity=.70f; dnaRegister=.76f; break;
        case Experimental: dnaSpace=.55f; dnaDensity=.45f; dnaRegister=.78f; break;
        case Lofi: dnaSpace=.76f; dnaDensity=.32f; dnaRegister=.44f; break;
        default: break;
    }

    const float moodDensityTarget[] = {0.00f,-.06f,-.10f,.10f,.14f,-.12f,-.02f,-.05f,.16f};
    const float moodSpaceTarget[]   = {0.00f,.10f,.16f,-.08f,-.10f,.22f,.08f,.16f,-.12f};
    const int mi = juce::jlimit(0,8,mood);
    dnaDensity = juce::jlimit(0.0f,1.0f,dnaDensity + moodDensityTarget[mi]);
    dnaSpace = juce::jlimit(0.0f,1.0f,dnaSpace + moodSpaceTarget[mi]);
    const float typeDensityTarget[] = {0.04f,-.08f,.08f,-.10f,.10f,-.02f,-.18f,.02f};
    const float typeSpaceTarget[] = {-.04f,.14f,-.02f,.16f,.02f,.08f,.24f,.02f};
    const int ti = juce::jlimit(0,7,melodyType);
    dnaDensity = juce::jlimit(0.0f,1.0f,dnaDensity + typeDensityTarget[ti]);
    dnaSpace = juce::jlimit(0.0f,1.0f,dnaSpace + typeSpaceTarget[ti]);

    // 0.22 MAGIC 1000-CANDIDATE SEARCH ENGINE + PHRASE JUDGE
    // We no longer accept the first eight generations as "variations".
    // Instead we explore a much larger space, score complete loops, and then
    // greedily select a diverse set of winners.  1000 candidates give the
    // judge a substantially wider search space without changing the final UI
    // bank of eight variations. This makes MAGIC a search process rather than
    // a random-note button.
    ++generationNonce;
    const uint32_t uiSeed = static_cast<uint32_t>(seed);
    const uint32_t nonce = generationNonce * 0x9e3779b9u;
    generationSeed = uiSeed ^ nonce ^ 0xA53C9E71u;

    auto hash32 = [](uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };

    Section previousSelected;
    {
        const juce::ScopedLock sl (variationsLock);
        if (!variations.empty())
            previousSelected = variations[(size_t) selectedVariation];
    }

    struct Candidate
    {
        Section section;
        float quality = 0.0f;
        uint32_t identity = 0;
    };

    auto melodyFeatures = [&](const Section& sec, uint32_t identity)
    {
        struct F {
            float density=0, space=0, leap=0, repetition=0, contour=0, variety=0, harmony=0, hook=0;
            float rhythmIdentity=0, motifIdentity=0, phraseMemory=0, seam=0, phraseArc=0, stepPenalty=0, registerScore=0, surprise=0, velocity=0.5f, noteLength=0.5f;
        };
        F f;
        std::vector<const NoteEvent*> m;
        for (const auto& n : sec.notes)
            if (n.channel == 3) m.push_back(&n);

        const int totalSteps = juce::jmax(1, sec.bars * 16);
        f.density = juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 6));
        f.space = 1.0f - juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 9));

        if (m.size() >= 2)
        {
            int leaps=0, turns=0;
            std::vector<int> intervals;
            intervals.reserve(m.size()-1);
            for (size_t i=1;i<m.size();++i)
            {
                const int d=m[i]->note-m[i-1]->note;
                intervals.push_back(d);
                if (std::abs(d)>=5) ++leaps;
                if (i>=2)
                {
                    const int a=m[i-1]->note-m[i-2]->note;
                    if (a!=0 && d!=0 && ((a>0)!=(d>0))) ++turns;
                }
            }
            f.leap=juce::jlimit(0.0f,1.0f,(float)leaps/(float)intervals.size());
            f.contour=juce::jlimit(0.0f,1.0f,(float)turns/(float)intervals.size());

            int repeated=0;
            for (size_t i=2;i<intervals.size();++i)
                if (std::abs(intervals[i])==std::abs(intervals[i-1])) ++repeated;
            f.repetition=1.0f-juce::jlimit(0.0f,1.0f,(float)repeated/(float)juce::jmax<size_t>(1,intervals.size()-2));

            std::vector<int> pcs;
            for (auto* n:m) pcs.push_back((n->note%12+12)%12);
            std::sort(pcs.begin(),pcs.end());
            pcs.erase(std::unique(pcs.begin(),pcs.end()),pcs.end());
            f.variety=juce::jlimit(0.0f,1.0f,(float)pcs.size()/6.0f);
        }

        // Rhythm identity: reward a phrase whose onset pattern has a clear
        // signature instead of evenly filling the grid.
        {
            std::vector<int> onsets;
            for (auto* n : m) onsets.push_back(n->step % 16);
            if (onsets.size() >= 2)
            {
                std::vector<int> gaps;
                for (size_t i=1;i<onsets.size();++i) gaps.push_back(onsets[i]-onsets[i-1]);
                std::sort(gaps.begin(),gaps.end());
                gaps.erase(std::unique(gaps.begin(),gaps.end()),gaps.end());
                f.rhythmIdentity=juce::jlimit(0.0f,1.0f,(float)gaps.size()/4.0f);
                int offbeats=0;
                for (auto x:onsets) if ((x%4)!=0) ++offbeats;
                f.rhythmIdentity=0.65f*f.rhythmIdentity+0.35f*juce::jlimit(0.0f,1.0f,(float)offbeats/(float)onsets.size());
            }
        }

        // Motif identity: a strong loop normally repeats a short interval/rhythm
        // idea at least once, but not as a literal bar-for-bar copy.
        if (m.size() >= 4)
        {
            int matched=0, comparisons=0;
            for (size_t i=2;i<m.size();++i)
            {
                const int a=m[i-1]->note-m[i-2]->note;
                const int b=m[i]->note-m[i-1]->note;
                if (a==b) ++matched;
                ++comparisons;
            }
            f.motifIdentity=juce::jlimit(0.0f,1.0f,(float)matched/(float)juce::jmax(1,comparisons));

            // Phrase memory score: compare the first and second bars by relative
            // contour and onset positions. Reward recurrence, but only softly.
            if (sec.bars >= 2)
            {
                std::vector<const NoteEvent*> firstBar, laterBar;
                for (auto* n : m)
                {
                    if (n->step < 16) firstBar.push_back(n);
                    else if (n->step < 32) laterBar.push_back(n);
                }
                if (firstBar.size() >= 2 && laterBar.size() >= 2)
                {
                    const size_t pairs = juce::jmin(firstBar.size(), laterBar.size());
                    int sameRhythm = 0;
                    int contourMatches = 0;
                    for (size_t k = 0; k < pairs; ++k)
                    {
                        if ((firstBar[k]->step % 16) == (laterBar[k]->step % 16))
                            ++sameRhythm;
                        if (k > 0)
                        {
                            const int a = firstBar[k]->note - firstBar[k-1]->note;
                            const int b = laterBar[k]->note - laterBar[k-1]->note;
                            if ((a == 0 && b == 0) || (a > 0 && b > 0) || (a < 0 && b < 0))
                                ++contourMatches;
                        }
                    }
                    const float rhythmMatch = (float)sameRhythm / (float)pairs;
                    const float contourMatch = (float)contourMatches
                        / (float)juce::jmax<size_t>(1, pairs - 1);
                    f.phraseMemory = 0.55f * rhythmMatch + 0.45f * contourMatch;
                }
            }
        }

        // 0.26 Phrase Judge: reward an audible rise/peak/return rather than
        // four bars with the same average register. This is intentionally a
        // soft score; minimal loops can still win if their other features are strong.
        if (m.size() >= 4 && sec.bars >= 4)
        {
            float barAvg[4] = {0,0,0,0};
            int barCount[4] = {0,0,0,0};
            for (auto* n : m)
            {
                const int b = juce::jlimit(0, 3, n->step / 16);
                barAvg[b] += (float)n->note;
                ++barCount[b];
            }
            for (int b = 0; b < 4; ++b)
                if (barCount[b] > 0) barAvg[b] /= (float)barCount[b];

            if (barCount[0] > 0 && barCount[1] > 0 && barCount[2] > 0 && barCount[3] > 0)
            {
                const float rise = barAvg[2] - barAvg[0];
                const float release = barAvg[2] - barAvg[3];
                const bool hasPeak = rise >= 1.5f && release >= 0.5f;
                const bool hasContrast = std::abs(barAvg[1] - barAvg[0]) >= 1.0f
                                      || std::abs(barAvg[2] - barAvg[1]) >= 1.5f;
                f.phraseArc = hasPeak ? (hasContrast ? 1.0f : 0.78f)
                                      : (hasContrast ? 0.50f : 0.18f);
            }
        }

        // Penalise endless scalar walking. Two-step alternation is especially
        // undesirable because it produces the old 1-2-1-2 sound.
        if (m.size() >= 3)
        {
            int bad=0, total=0;
            for (size_t i=2;i<m.size();++i)
            {
                const int a=m[i-1]->note-m[i-2]->note;
                const int b=m[i]->note-m[i-1]->note;
                if (std::abs(a)<=3 && std::abs(b)<=3 && a!=0 && b!=0
                    && ((a>0)!=(b>0))) ++bad;
                ++total;
            }
            f.stepPenalty=juce::jlimit(0.0f,1.0f,(float)bad/(float)juce::jmax(1,total));
        }

        // Loop seam: the end should connect to the beginning without requiring
        // a textbook cadence. Reward a reasonable seam and penalise an awkward
        // giant jump or identical terminal repetition.
        if (m.size() >= 2)
        {
            const int seamInterval=m.front()->note-m.back()->note;
            f.seam=1.0f-juce::jlimit(0.0f,1.0f,(float)juce::jmax(0,std::abs(seamInterval)-7)/10.0f);
            if (m.front()->note==m.back()->note && m.size()<5) f.seam*=0.65f;
        }

        // Register and surprise: a little controlled contrast is useful, but
        // huge random jumps should not dominate the loop.
        if (!m.empty())
        {
            float mean=0; for(auto* n:m) mean+=(float)n->note; mean/=(float)m.size();
            float spread=0; for(auto* n:m) spread+=std::abs((float)n->note-mean);
            f.registerScore=juce::jlimit(0.0f,1.0f,(spread/(float)m.size())/14.0f);
            int unusual=0;
            for(size_t i=1;i<m.size();++i) if(std::abs(m[i]->note-m[i-1]->note)>=8) ++unusual;
            f.surprise=juce::jlimit(0.0f,1.0f,(float)unusual/(float)juce::jmax<size_t>(1,m.size()-1));
        }

        // Reward intentional space and a memorable amount of repetition, but
        // penalise mechanical stepwise walking and excessive note density.
        f.hook = 0.24f*f.repetition + 0.16f*f.contour + 0.16f*f.leap
               + 0.16f*f.variety + 0.14f*f.rhythmIdentity + 0.10f*f.motifIdentity
               + 0.04f*f.surprise;
        const float densityTarget = (hookMode ? 0.86f : 0.74f) * soundProfileFor(soundTarget).densityMul;
        const float densityFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.density-densityTarget)/0.42f);
        f.hook = 0.65f*f.hook + 0.35f*densityFit;

        // Deterministic micro-jitter keeps ties from always favouring the same
        // candidate while remaining reproducible for a generation.
        const float jitter=(float)((hash32(identity)^0x55aa33u)%1000u)/100000.0f;
        f.hook=juce::jlimit(0.0f,1.0f,f.hook+jitter);
        juce::ignoreUnused(totalSteps);
        // Taste Learning 2.0 features.
        if (!m.empty())
        {
            float velSum = 0.0f, lenSum = 0.0f;
            int minPitch = 127, maxPitch = 0;
            for (auto* n : m)
            {
                velSum += (float)n->velocity / 127.0f;
                lenSum += (float)n->length / 16.0f;
                minPitch = juce::jmin(minPitch, n->note);
                maxPitch = juce::jmax(maxPitch, n->note);
            }
            f.velocity = juce::jlimit(0.0f, 1.0f, velSum / (float)m.size());
            f.noteLength = juce::jlimit(0.0f, 1.0f, lenSum / (float)m.size());
            // 24 semitones is a useful musical register reference.
            f.registerScore = juce::jlimit(0.0f, 1.0f, (float)(maxPitch - minPitch) / 24.0f);
        }

        return f;
    };

    auto similarity = [&](const Section& a, const Section& b)
    {
        std::vector<int> ap, bp, ar, br;
        for (const auto& n:a.notes) if(n.channel==3){ap.push_back((n.note%12+12)%12); ar.push_back(n.step%16);}
        for (const auto& n:b.notes) if(n.channel==3){bp.push_back((n.note%12+12)%12); br.push_back(n.step%16);}
        if(ap.empty()||bp.empty()) return 0.0f;
        const size_t n=juce::jmin(ap.size(),bp.size());
        float samePitch=0, sameRhythm=0;
        for(size_t i=0;i<n;++i){ if(ap[i]==bp[i])samePitch+=1.0f; if(ar[i]==br[i])sameRhythm+=1.0f; }
        const float lengthSim=1.0f-juce::jlimit(0.0f,1.0f,(float)std::abs((int)ap.size()-(int)bp.size())/8.0f);
        float sameIntervals=0.0f;
        if (n >= 3)
        {
            int count=0;
            for(size_t i=1;i<n;++i)
            {
                int da=(int)ap[i]-(int)ap[i-1];
                int db=(int)bp[i]-(int)bp[i-1];
                if(da==db) sameIntervals+=1.0f;
                ++count;
            }
            sameIntervals/=juce::jmax(1,count);
        }
        return juce::jlimit(0.0f,1.0f,0.32f*(samePitch/(float)n)+0.30f*(sameRhythm/(float)n)
                                      +0.18f*sameIntervals+0.20f*lengthSim);
    };

    int mLo = 62, mHi = 86;
    registerLane(2, mLo, mHi);
    auto flatten = [&](const SongData& song, int candidateIndex, juce::Random& local)
    {
        Section flat;
        flat.name="CANDIDATE "+juce::String(candidateIndex+1);
        flat.bars=0;
        for(const auto& sec:song.sections)
        {
            const int sectionBarsBefore=flat.bars;
            flat.bars+=sec.bars;
            for(auto n:sec.notes)
            {
                n.step+=sectionBarsBefore*16;
                // Candidate-level mutations are intentionally structural, not
                // just octave changes: entrance, note deletion and phrase
                // displacement produce genuinely different loop identities.
                if(n.channel==3 && !soundProfileFor(soundTarget).soloLine)
                {
                    const uint32_t h=hash32(generationSeed ^ (uint32_t)(candidateIndex*977 + n.step*31));
                    const unsigned mode = h % 100u;
                    if(mode < (unsigned)(7 + (candidateIndex % 6)))
                        n.velocity=juce::jlimit(40,112,n.velocity+(int)(h%17)-8);
                    // Candidate search is allowed to alter phrase identity.
                    // These are deliberately small structural mutations rather
                    // than random note spam.
                    if(mode >= 14u && mode < 19u)
                    {
                        const int localStep = n.step % 16;
                        const bool deliberateSyncopation = (candidateIndex % 8 == 0 || candidateIndex % 8 == 3 || candidateIndex % 8 == 5);
                        int delta = deliberateSyncopation ? (((h >> 8) & 1u) ? 1 : -1)
                                                          : (((h >> 8) & 1u) ? 2 : -2);
                        // Never push a non-syncopated note onto an odd 16th.
                        if (!deliberateSyncopation && ((localStep + delta) & 1))
                            delta += (delta > 0 ? 1 : -1);
                        n.step = juce::jlimit(0, juce::jmax(0, flat.bars*16-1), n.step + delta);
                    }
                    if(mode >= 19u && mode < 23u)
                        n.note = foldIntoLane(snapToScale(n.note + (((h>>9)&1u) ? 4 : -4)), mLo, mHi);
                    if(mode >= 23u && mode < 27u && n.length > 2)
                        n.length = juce::jmax(2, n.length - (int)(h % 4u));
                    if(mode >= 27u && mode < 31u)
                        n.velocity = juce::jlimit(35,118,n.velocity - 10);
                    // Advanced Magic: rare, scale-safe happy accidents.
                    if(mode >= 31u && mode < 35u)
                    {
                        const int accident = (int)((h >> 14) % 5u);
                        if(accident == 0)
                            n.length = juce::jlimit(1, 16, n.length + 2);
                        else if(accident == 1)
                            n.length = juce::jmax(1, n.length - 2);
                        else if(accident == 2)
                            n.note = foldIntoLane(snapToScale(n.note + 5), mLo, mHi);
                        else if(accident == 3)
                            n.note = foldIntoLane(snapToScale(n.note - 5), mLo, mHi);
                        else
                            n.velocity = juce::jlimit(35, 118, n.velocity + 9);
                    }
                }
                flat.notes.push_back(n);
            }
        }
        juce::ignoreUnused(local);
        removeDuplicateNotes(flat.notes);
        cleanMelodyLine(flat.notes);
        return flat;
    };

    const bool sparseTypeAllowed = (melodyType == SparseLeadMelody || genre == Ambient);
    const auto judgeProf = soundProfileFor(soundTarget);
    std::vector<Candidate> candidates;
    std::vector<taste::Vec> tasteFeatures;
    constexpr int candidateCount=1000;
    candidates.reserve(candidateCount);
    tasteFeatures.reserve(candidateCount);

    for(int c=0;c<candidateCount;++c)
    {
        const uint32_t identity=hash32(generationSeed ^ (uint32_t)(c+1)*0x45d9f3bu);
        juce::Random local((juce::int64)identity);
        SongData song;
        const float oldVariation=variationAmount;
        // Spread the search deliberately: some candidates are sparse, some
        // hook-heavy, some rhythm-first.  This is exploration, not noise.
        variationAmount=juce::jlimit(0.f,1.f,oldVariation + ((int)(identity%17u)-8)*0.035f);
        buildBaseSong(song,local,c+1);
        variationAmount=oldVariation;

        Section flat=flatten(song,c,local);
        const auto f=melodyFeatures(flat,identity);
        float quality=0.0f;
        // Magic DNA 2.0: candidate features are judged against the same
        // musical universe created by MAGIC. The generic judge remains
        // dominant, while DNA steers the final selection.
        const float melodyFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.variety - dnaMelody));
        const float rhythmFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.rhythmIdentity - dnaRhythm));
        const float motifFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.motifIdentity - dnaMotif));
        const float registerFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.registerScore - dnaRegister));
        const float surpriseFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.surprise - dnaSurprise));
        quality += 0.22f*f.hook;
        quality += 0.10f*f.space;
        quality += 0.10f*f.repetition;
        quality += 0.10f*f.variety;
        quality += 0.08f*f.contour;
        quality += 0.08f*f.leap;
        quality += 0.08f*f.rhythmIdentity;
        quality += 0.08f*f.motifIdentity;
        quality += 0.06f*f.phraseMemory;
        quality += 0.06f*f.phraseArc;
        quality += 0.07f*f.seam;
        quality += 0.05f*f.registerScore;
        quality += 0.05f*f.surprise;
        quality += 0.045f*melodyFit + 0.045f*rhythmFit + 0.045f*motifFit;
        quality += 0.030f*registerFit + 0.025f*surpriseFit;
        // Hybrid DNA 1.0: combine Genre + Mood + Era + Melody Type into one
        // coherent target fingerprint. Each axis contributes softly, so no single
        // preset can collapse the search into one exact pattern.
        float hybridLeap = 0.30f, hybridRhythm = 0.48f, hybridMotif = 0.48f;
        float hybridSurprise = 0.30f, hybridRepeat = 0.50f;
        // Mood shifts energy/space/novelty.
        hybridSurprise += (mood - 4) * 0.025f;
        hybridRepeat   += (4 - std::abs(mood - 4)) * 0.010f;
        // Melody type defines the phrase grammar bias.
        switch (melodyType)
        {
            case HookMelody:       hybridMotif += .14f; hybridRepeat += .10f; break;
            case SparseLeadMelody:     hybridRhythm -= .10f; hybridRepeat -= .04f; break;
            case RiffMelody:       hybridLeap += .16f; hybridSurprise += .08f; break;
            case ArpMelody:       hybridRhythm += .16f; hybridSurprise += .04f; break;
            case OstinatoMelody:     hybridRhythm += .10f; hybridMotif -= .06f; break;
            case CounterMelody:     hybridMotif += .06f; hybridLeap += .06f; break;
            case VocalLikeMelody:  hybridRhythm += .04f; hybridRepeat += .02f; break;
            case PhraseMelody: hybridRhythm -= .16f; hybridRepeat -= .08f; break;
            default: break;
        }
        // Era is deliberately a small modifier, not a historical stereotype.
        hybridSurprise += (era - 2.5f) * .018f;
        hybridRhythm += (era < 2 ? -.04f : (era > 3 ? .05f : 0.0f));
        hybridLeap += (era > 3 ? .025f : -.01f);
        hybridLeap = juce::jlimit(.05f,.90f,hybridLeap);
        hybridRhythm = juce::jlimit(.05f,.90f,hybridRhythm);
        hybridMotif = juce::jlimit(.05f,.90f,hybridMotif);
        hybridSurprise = juce::jlimit(.05f,.90f,hybridSurprise);
        hybridRepeat = juce::jlimit(.05f,.90f,hybridRepeat);

        // Blend Genre fingerprint with the current DNA/mood/type/era fingerprint.
        // This is intentionally low-weight: the candidate judge remains dominant.
        // Genre DNA 2.0: every genre gets a broader fingerprint than density,
        // space and register. These targets bias rhythm, contour, motif, leap,
        // repetition and surprise while the generic judge remains dominant.
        float genreLeapTarget = 0.30f;
        float genreRhythmTarget = 0.45f;
        float genreMotifTarget = 0.45f;
        float genreSurpriseTarget = 0.28f;
        float genreRepeatTarget = 0.50f;
        switch (genre)
        {
            case Trap:       genreLeapTarget=.34f; genreRhythmTarget=.62f; genreMotifTarget=.58f; genreSurpriseTarget=.30f; genreRepeatTarget=.62f; break;
            case House:      genreLeapTarget=.18f; genreRhythmTarget=.52f; genreMotifTarget=.48f; genreSurpriseTarget=.18f; genreRepeatTarget=.54f; break;
            case Techno:     genreLeapTarget=.16f; genreRhythmTarget=.48f; genreMotifTarget=.50f; genreSurpriseTarget=.16f; genreRepeatTarget=.58f; break;
            case BoomBap:    genreLeapTarget=.28f; genreRhythmTarget=.66f; genreMotifTarget=.60f; genreSurpriseTarget=.25f; genreRepeatTarget=.64f; break;
            case Ambient:    genreLeapTarget=.24f; genreRhythmTarget=.30f; genreMotifTarget=.38f; genreSurpriseTarget=.34f; genreRepeatTarget=.36f; break;
            case Cinematic:  genreLeapTarget=.48f; genreRhythmTarget=.42f; genreMotifTarget=.42f; genreSurpriseTarget=.48f; genreRepeatTarget=.36f; break;
            case RnB:        genreLeapTarget=.30f; genreRhythmTarget=.58f; genreMotifTarget=.68f; genreSurpriseTarget=.24f; genreRepeatTarget=.66f; break;
            case GenrePop:   genreLeapTarget=.24f; genreRhythmTarget=.50f; genreMotifTarget=.62f; genreSurpriseTarget=.22f; genreRepeatTarget=.68f; break;
            case Drill:      genreLeapTarget=.42f; genreRhythmTarget=.70f; genreMotifTarget=.54f; genreSurpriseTarget=.34f; genreRepeatTarget=.58f; break;
            case DnB:        genreLeapTarget=.30f; genreRhythmTarget=.78f; genreMotifTarget=.42f; genreSurpriseTarget=.38f; genreRepeatTarget=.42f; break;
            case Jersey:     genreLeapTarget=.26f; genreRhythmTarget=.76f; genreMotifTarget=.46f; genreSurpriseTarget=.34f; genreRepeatTarget=.46f; break;
            case Afro:       genreLeapTarget=.22f; genreRhythmTarget=.72f; genreMotifTarget=.54f; genreSurpriseTarget=.28f; genreRepeatTarget=.52f; break;
            case Hyperpop:   genreLeapTarget=.55f; genreRhythmTarget=.68f; genreMotifTarget=.46f; genreSurpriseTarget=.62f; genreRepeatTarget=.34f; break;
            case Experimental: genreLeapTarget=.58f; genreRhythmTarget=.55f; genreMotifTarget=.28f; genreSurpriseTarget=.78f; genreRepeatTarget=.22f; break;
            case Lofi:       genreLeapTarget=.20f; genreRhythmTarget=.54f; genreMotifTarget=.58f; genreSurpriseTarget=.20f; genreRepeatTarget=.64f; break;
            default: break;
        }

        genreLeapTarget = juce::jlimit(.0f,1.0f,.68f*genreLeapTarget + .32f*hybridLeap);
        genreRhythmTarget = juce::jlimit(.0f,1.0f,.68f*genreRhythmTarget + .32f*hybridRhythm);
        genreMotifTarget = juce::jlimit(.0f,1.0f,.68f*genreMotifTarget + .32f*hybridMotif);
        genreSurpriseTarget = juce::jlimit(.0f,1.0f,.68f*genreSurpriseTarget + .32f*hybridSurprise);
        genreRepeatTarget = juce::jlimit(.0f,1.0f,.68f*genreRepeatTarget + .32f*hybridRepeat);
        const float genreDensityTarget = juce::jlimit(0.0f,1.0f,(0.40f+0.60f*dnaDensity)*judgeProf.densityMul);
        const float genreSpaceTarget = juce::jlimit(0.0f,1.0f,dnaSpace);
        const float genreRegisterTarget = juce::jlimit(0.0f,1.0f,dnaRegister);
        quality += 0.10f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.density-genreDensityTarget)));
        quality += 0.06f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.space-genreSpaceTarget)));
        quality += 0.04f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.registerScore-genreRegisterTarget)));
        quality += 0.045f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.leap-genreLeapTarget)));
        quality += 0.045f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.rhythmIdentity-genreRhythmTarget)));
        quality += 0.040f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.motifIdentity-genreMotifTarget)));
        quality += 0.035f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.surprise-genreSurpriseTarget)));
        quality += 0.035f * (1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.repetition-genreRepeatTarget)));
        quality += 0.08f*(1.0f-juce::jlimit(0.0f,1.0f,std::abs(f.density-0.80f*judgeProf.densityMul)/0.50f));
        quality -= 0.28f*f.stepPenalty;

        // 0.38 Layer judge: the old judge looked at the melody only.  Now the whole
        // loop is scored: complete chords, a bass anchor, a repeating rhythmic
        // hook and a playable note density.
        {
            const int barsN = juce::jmax(1, flat.bars);
            int chordOk = 0, bassOk = 0, melNotes = 0;
            std::vector<std::vector<int>> sig((size_t) barsN);
            std::vector<std::vector<int>> pcs((size_t) barsN);
            std::vector<bool> hasAnchor((size_t) barsN, false);
            for (const auto& n : flat.notes)
            {
                const int b = juce::jlimit(0, barsN - 1, n.step / 16);
                if (n.channel == 1) pcs[(size_t) b].push_back(((n.note % 12) + 12) % 12);
                if (n.channel == 2 && n.step % 16 == 0) hasAnchor[(size_t) b] = true;
                if (n.channel == 3) { sig[(size_t) b].push_back(n.step % 16); ++melNotes; }
            }
            for (int b = 0; b < barsN; ++b)
            {
                auto& v = pcs[(size_t) b];
                std::sort(v.begin(), v.end());
                v.erase(std::unique(v.begin(), v.end()), v.end());
                if (v.size() >= 3) ++chordOk;
                if (hasAnchor[(size_t) b]) ++bassOk;
                std::sort(sig[(size_t) b].begin(), sig[(size_t) b].end());
            }
            const float chordComplete = (chordsEnabled && ! judgeProf.soloLine) ? (float) chordOk / (float) barsN : 1.0f;
            const float bassAnchor = (bassEnabled && ! judgeProf.bassOff) ? (float) bassOk / (float) barsN : 1.0f;
            float hookRepeat = 0.55f;
            if (barsN >= 2)
            {
                int repeats = 0;
                for (int b = 1; b < barsN; ++b)
                {
                    if (sig[(size_t) b].empty()) continue;
                    for (int a = 0; a < b; ++a)
                        if (sig[(size_t) a] == sig[(size_t) b]) { ++repeats; break; }
                }
                hookRepeat = (float) repeats / (float) (barsN - 1);
            }
            const float perBar = (float) melNotes / (float) barsN;
            const float minPerBar = sparseTypeAllowed ? 2.0f : 1.05f * (float) judgeProf.minNotes;
            const float sparsePenalty = juce::jlimit(0.0f, 1.0f, (minPerBar - perBar) / minPerBar);
            quality += 0.10f * chordComplete + 0.05f * bassAnchor;
            quality += 0.10f * (1.0f - juce::jlimit(0.0f, 1.0f, std::abs(hookRepeat - 0.55f) / 0.55f));
            quality -= 0.20f * sparsePenalty;
        }


        // 0.43 Musical Quality Judge 1.0:
        // Score the musical relationship of the whole loop instead of treating
        // melody features as mostly independent statistics.  This remains a
        // soft layer on top of the existing judge: it rewards coherence without
        // forcing every candidate into the same melodic shape.
        {
            const int barsN = juce::jmax(1, flat.bars);
            std::vector<std::vector<int>> chordPcs((size_t) barsN);
            std::vector<std::vector<const NoteEvent*>> melodyBars((size_t) barsN);

            for (const auto& n : flat.notes)
            {
                const int b = juce::jlimit(0, barsN - 1, n.step / 16);
                if (n.channel == 1)
                {
                    const int pc = (n.note % 12 + 12) % 12;
                    if (std::find(chordPcs[(size_t) b].begin(),
                                  chordPcs[(size_t) b].end(), pc) == chordPcs[(size_t) b].end())
                        chordPcs[(size_t) b].push_back(pc);
                }
                else if (n.channel == 3)
                {
                    melodyBars[(size_t) b].push_back(&n);
                }
            }

            // Harmony fit: notes should usually land on a chord tone, while
            // leaving room for passing/approach tones. Long notes are weighted
            // slightly more because they define the perceived harmony.
            float harmonySum = 0.0f;
            float harmonyWeight = 0.0f;
            for (int b = 0; b < barsN; ++b)
            {
                const auto& cp = chordPcs[(size_t) b];
                if (cp.empty()) continue;
                for (const auto* n : melodyBars[(size_t) b])
                {
                    const int pc = (n->note % 12 + 12) % 12;
                    const bool chordTone = std::find(cp.begin(), cp.end(), pc) != cp.end();
                    const float weight = 0.75f + 0.25f * juce::jlimit(0.0f, 1.0f,
                        (float) juce::jmax(1, n->length) / 8.0f);
                    harmonySum += (chordTone ? 1.0f : 0.35f) * weight;
                    harmonyWeight += weight;
                }
            }
            const float harmonyFit = harmonyWeight > 0.0f
                ? harmonySum / harmonyWeight : 0.55f;

            // Leap recovery: a large jump feels more intentional when the next
            // movement answers it in the opposite direction and is smaller.
            int largeLeaps = 0;
            int recoveredLeaps = 0;
            for (const auto& bar : melodyBars)
            {
                for (size_t i = 1; i + 1 < bar.size(); ++i)
                {
                    const int a = bar[i]->note - bar[i - 1]->note;
                    const int b = bar[i + 1]->note - bar[i]->note;
                    if (std::abs(a) >= 7)
                    {
                        ++largeLeaps;
                        if ((a > 0 && b < 0) || (a < 0 && b > 0))
                            if (std::abs(b) <= 5)
                                ++recoveredLeaps;
                    }
                }
            }
            const float leapRecovery = largeLeaps > 0
                ? (float) recoveredLeaps / (float) largeLeaps : 0.65f;

            // Cadence: the final melodic event should feel like a landing.
            // Chord-tone endings are preferred, but a nearby scale tone is
            // still acceptable so the judge does not over-constrain phrasing.
            float cadence = 0.55f;
            const auto& lastBar = melodyBars.back();
            if (!lastBar.empty())
            {
                const auto* last = lastBar.back();
                const int lastPc = (last->note % 12 + 12) % 12;
                const bool chordTone = !chordPcs.back().empty()
                    && std::find(chordPcs.back().begin(), chordPcs.back().end(), lastPc)
                       != chordPcs.back().end();

                float approach = 0.0f;
                if (lastBar.size() >= 2)
                {
                    const int d = last->note - lastBar[lastBar.size() - 2]->note;
                    approach = (std::abs(d) <= 2) ? 0.20f : 0.0f;
                }
                cadence = chordTone ? 0.80f + approach : 0.35f + approach;
                cadence = juce::jlimit(0.0f, 1.0f, cadence);
            }

            // Phrase balance: reward variation between bars, but penalise a
            // completely empty/overloaded bar. This complements, rather than
            // replaces, the existing density and phrase-memory scores.
            float balance = 0.70f;
            if (barsN >= 2)
            {
                std::vector<float> counts;
                counts.reserve((size_t) barsN);
                for (const auto& mb : melodyBars)
                    counts.push_back((float) mb.size());

                const float mean = std::accumulate(counts.begin(), counts.end(), 0.0f)
                    / (float) barsN;
                if (mean > 0.0f)
                {
                    float variance = 0.0f;
                    for (const auto c : counts)
                    {
                        const float d = c - mean;
                        variance += d * d;
                    }
                    variance /= (float) barsN;
                    const float cv = std::sqrt(variance) / juce::jmax(1.0f, mean);
                    balance = 1.0f - juce::jlimit(0.0f, 1.0f, std::abs(cv - 0.45f) / 0.75f);

                    int emptyBars = 0;
                    for (const auto c : counts) if (c <= 0.0f) ++emptyBars;
                    if (emptyBars > 0 && !sparseTypeAllowed)
                        balance -= 0.20f * juce::jlimit(0, 2, emptyBars);
                    balance = juce::jlimit(0.0f, 1.0f, balance);
                }
            }

            // The combined score is deliberately capped in influence. Existing
            // DNA, genre, phrase and diversity systems remain the main search
            // drivers; this layer only helps the Judge reject technically valid
            // but musically disconnected candidates.
            const float musicalCoherence =
                0.40f * harmonyFit
                + 0.22f * leapRecovery
                + 0.23f * cadence
                + 0.15f * balance;
            quality += 0.16f * musicalCoherence;
        }

        // 0.40 Taste ML: the features of the whole loop are collected here; the
        // model scores every candidate after the pool statistics are known (below).
        tasteFeatures.push_back(taste::extractFeatures(flat.notes, flat.bars));

        candidates.push_back({std::move(flat),quality,identity});
    }

    // Standardise against this search pool (kept for training the ratings of the
    // loops that come out of it), then let the model re-rank the pool.
    {
        const size_t cn = candidates.size();
        for (size_t i = 0; i < (size_t) taste::kDim; ++i)
        {
            double sum = 0.0, sum2 = 0.0;
            for (size_t c = 0; c < cn; ++c) { const double v = tasteFeatures[c][i]; sum += v; sum2 += v * v; }
            const double mean = cn ? sum / (double) cn : 0.0;
            const double var = cn ? std::max(0.0, sum2 / (double) cn - mean * mean) : 0.0;
            tasteMean[i] = (float) mean;
            tasteStd[i] = std::max(0.03f, (float) std::sqrt(var));
        }
        const float conf = tasteModel.confidence();
        if (tasteEnabled && conf > 0.01f && cn > 8)
        {
            double qs = 0.0, qs2 = 0.0;
            for (const auto& c : candidates) { qs += c.quality; qs2 += (double) c.quality * c.quality; }
            const double qm = qs / (double) cn;
            const float qstd = (float) std::sqrt(std::max(1.0e-6, qs2 / (double) cn - qm * qm));
            const float gain = 0.7f * qstd * conf;     // at full confidence: +-0.7 sigma of the judge's own spread
            for (size_t c = 0; c < cn; ++c)
            {
                taste::Vec z;
                for (size_t i = 0; i < (size_t) taste::kDim; ++i)
                    z[i] = juce::jlimit(-3.0f, 3.0f, (tasteFeatures[c][i] - tasteMean[i]) / tasteStd[i]);
                const float p = tasteModel.predict(z, soundTarget, genre);
                candidates[c].quality += gain * (2.0f * p - 1.0f);
            }
        }
    }

    std::vector<Candidate> selected;
    selected.reserve(8);
    std::vector<bool> used(candidates.size(),false);

    // Greedy diversity-aware selection. The best candidate wins first; after
    // that, similarity to already selected loops becomes a real penalty.
    for(int slot=0;slot<8;++slot)
    {
        int best=-1;
        float bestScore=-1000.0f;
        for(size_t i=0;i<candidates.size();++i)
        {
            if(used[i]) continue;
            float score=candidates[i].quality;
            float maxSim=0.0f;
            for(const auto& s:selected) maxSim=juce::jmax(maxSim,similarity(candidates[i].section,s.section));
            score-=0.78f*maxSim;
            // Do not let the top eight collapse into one archetype. Spread
            // candidate identities across the final bank while preserving quality.
            if(slot>0)
            {
                const int profile=(int)(candidates[i].identity % 8u);
                int profileCount=0;
                for(const auto& s:selected) if(((int)(s.identity % 8u))==profile) ++profileCount;
                score -= 0.07f*(float)profileCount;
            }
            if(slot==0) score=candidates[i].quality;
            if(score>bestScore){bestScore=score;best=(int)i;}
        }
        if(best<0) break;
        used[(size_t)best]=true;
        selected.push_back(std::move(candidates[(size_t)best]));
    }

    std::vector<Section> result;
    result.reserve(8);
    for(size_t i=0;i<selected.size();++i)
    {
        auto flat=std::move(selected[i].section);
        flat.name="VARIATION "+juce::String((int)i+1);

        auto applyLock = [&](int channel, bool locked)
        {
            if(!locked) return;
            flat.notes.erase(std::remove_if(flat.notes.begin(),flat.notes.end(),
                [channel](const NoteEvent& n){return n.channel==channel;}),flat.notes.end());
            for(const auto& n:previousSelected.notes)
                if(n.channel==channel && n.step<flat.bars*16) flat.notes.push_back(n);
        };
        applyLock(1,lockChordsLayer);
        applyLock(2,lockBassLayer);
        applyLock(3,lockMelodyLayer);
        applyLock(4,lockArpLayer);
        result.push_back(std::move(flat));
    }

    // Defensive fallback: the bank should never become empty.
    if(result.empty())
    {
        juce::Random fallback((juce::int64)generationSeed);
        SongData song;
        buildBaseSong(song,fallback,0);
        if(!song.sections.empty()) result.push_back(song.sections.front());
    }

    {
        const juce::ScopedLock sl(variationsLock);
        variations=std::move(result);
    }
    likeCounts.fill(0);
    dislikeCounts.fill(0);
}

void MidiForgeAudioProcessor::magicRandomize()
{
    // MAGIC 2.0: use generators::MagicDNA for coherent DNA generation
    generators::MagicDNA dna;
    
    const uint32_t timeSeed = (uint32_t) juce::Time::currentTimeMillis();
    uint32_t seed = generators::combineSeeds(generationSeed, timeSeed, 0x51A7D00Du);
    juce::Random r((juce::int64) seed);
    
    // Generate coherent DNA using module function
    dna.randomize(r);
    
    // Store DNA values
    dnaMelody   = dna.melody;
    dnaRhythm   = dna.rhythm;
    dnaHarmony  = dna.harmony;
    dnaMotif    = dna.motif;
    dnaRegister = dna.register_;
    dnaGroove   = dna.groove;
    dnaEnergy   = dna.energy;
    dnaSurprise = dna.surprise;
    magicDnaSeed = dna.seed;
    
    auto rf = [&](float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };
    auto pick = [&](int maxExclusive) { return r.nextInt(maxExclusive); };

    // Keep layer locks meaningful: a locked layer keeps its character controls.
    if (!lockChordsLayer)
    {
        rootPc = pick(12);
        scale = pick(7);
        progression = pick(7);
        chordDensity = juce::jlimit(.35f,1.0f,.50f + dna.harmony*.48f);
        chordExtensions = r.nextFloat() > (.48f - dna.harmony*.22f);
        inversions = r.nextFloat() > .28f;
        voicingWidth = juce::jlimit(.20f,.85f,.25f + dna.harmony*.55f);
    }

    if (!lockBassLayer)
    {
        bassDensity = juce::jlimit(.25f,.95f,.28f + dna.rhythm*.52f);
    }

    if (!lockMelodyLayer)
    {
        melodyDensity = juce::jlimit(.20f,.88f,.22f + dna.melody*.62f);
        melodyLength = juce::jlimit(.12f,.82f,.18f + dna.melody*.48f);
        pauseChance = juce::jlimit(.04f,.42f,.32f - dna.rhythm*.20f);
        leapChance = juce::jlimit(.04f,.48f,.06f + dna.register_*.34f);
        ghostChance = juce::jlimit(.01f,.24f,.03f + dna.groove*.12f);
        motifStrength = juce::jlimit(.45f,.98f,dna.motif);
        variationAmount = juce::jlimit(.18f,.85f,.22f + dna.surprise*.55f);
    }

    if (!lockArpLayer)
    {
        arpDensity = juce::jlimit(.03f,.58f,.05f + dna.rhythm*.42f);
        static constexpr int arpChoices[] = {1,2,4,8};
        arpRate = arpChoices[pick(4)];
    }

    // Global musical identity. These affect all layers coherently.
    genre = pick(16);
    mood = pick(9);
    melodyType = pick(8);
    rhythm = pick(4);
    static constexpr int barChoices[] = {1,2,4,8,16};
    bars = barChoices[pick(5)];
    { static constexpr int octaveChoices[] = {3, 4, 4, 5}; octave = octaveChoices[pick(4)]; }
    era = pick(6);

    swing = juce::jlimit(.0f,.40f, dna.groove*.34f);
    humanize = juce::jlimit(.05f,.30f,.07f + dna.groove*.16f);
    complexity = juce::jlimit(.20f,.92f,.25f + dna.surprise*.48f + dna.melody*.15f);
    fillAmount = juce::jlimit(.04f,.38f,.06f + dna.energy*.25f);
    energy = dna.energy;
    
    // Rhythm DNA and genre still get a chance to create distinct identities.
    if (dna.rhythm > .72f && r.nextFloat() > .35f) rhythm = Syncopated;
    if (dna.rhythm < .28f && r.nextFloat() > .30f) rhythm = Straight;
    hookMode = dna.motif > .46f;
    chordsEnabled = true;
    bassEnabled = r.nextFloat() > .06f;
    melodyEnabled = true;
    arpEnabled = dna.rhythm > .52f || r.nextFloat() > .72f;
    leadStyleSoundCloud = false;

    // Seed controls the candidate search; DNA seed remains stable for
    // REROLL, so REROLL explores the same musical universe.
    seed = static_cast<int>(magicDnaSeed);
    regenerate();
}

void MidiForgeAudioProcessor::rerollSameDNA()
{
    // Preserve all DNA axes and controls; only change the generation identity.
    seed = static_cast<int>(hash32(magicDnaSeed ^ generationNonce ^ 0x6d2b79f5u));
    regenerateVariations();
    chooseVariation(0);
}

void MidiForgeAudioProcessor::mutateSelected(float amount)
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    std::vector<VisibleNote> notes = getVisibleNotes();
    if (notes.empty()) { rerollSameDNA(); return; }

    // Use module DNA mutation instead of local hash-based approach
    generators::MagicDNA dna;
    dna.melody = dnaMelody;
    dna.rhythm = dnaRhythm;
    dna.harmony = dnaHarmony;
    dna.motif = dnaMotif;
    dna.register_ = dnaRegister;
    dna.groove = dnaGroove;
    dna.energy = dnaEnergy;
    dna.surprise = dnaSurprise;
    dna.seed = magicDnaSeed;
    
    juce::Random rng((juce::int64)magicDnaSeed);
    dna.mutate(amount, rng);
    
    const uint32_t base = generators::combineSeeds(magicDnaSeed, (uint32_t)(amount*1000.0f), 0xA17E5EEDu);
    
    for(size_t i=0;i<notes.size();++i)
    {
        auto& n=notes[i];
        const uint32_t h=generators::hash32(base ^ (uint32_t)(i*0x9e3779b9u));
        const float roll=(float)(h%1000u)/1000.0f;

        // Each layer has its own mutation grammar. Locked layers are untouched.
        const bool locked=(n.channel==1&&lockChordsLayer)||(n.channel==2&&lockBassLayer)||
                           (n.channel==3&&lockMelodyLayer)||(n.channel==4&&lockArpLayer);
        if(locked) continue;
        if(n.channel==5)
        {
            // Drums keep their groove: MUTATE / EVOLVE only touches their velocity (0.44).
            if((h%100u)>62u) n.velocity=juce::jlimit(30,120,n.velocity+(int)((h>>22)%13u)-6);
            continue;
        }

        const float chance=0.10f+0.48f*amount;
        if(roll<chance)
        {
            if(n.channel==3)
            {
                // Melody: scale-safe pitch mutation, occasional direction flip.
                const int step=((h>>8)&1u)?2:-2;
                { int ml=62, mh=86; registerLane(2,ml,mh); n.note=juce::jlimit(ml,mh,snapToScale(n.note+step)); }
            }
            else if(n.channel==2)
            {
                // Bass: mostly octave/scale-degree movement, never chromatic.
                const int move=((h>>9)&3u)==0 ? 12 : (((h>>9)&1u)?2:-2);
                { int bl=28, bh=52; registerLane(1,bl,bh); n.note=foldIntoLane(snapToScale(n.note+move),bl,bh); }
            }
            else if(n.channel==1)
            {
                // Chords: alter voicing rather than changing the harmony identity.
                if((h&3u)==0) n.note=juce::jlimit(24,108,n.note+12);
                else if((h&3u)==1) n.note=juce::jlimit(24,108,n.note-12);
                else n.velocity=juce::jlimit(40,105,n.velocity+(int)((h>>12)%13u)-6);
            }
            else if(n.channel==4)
            {
                // Arp: move along the active scale, preserving its role.
                n.note=juce::jlimit(36,108,snapToScale(n.note+(((h>>10)&1u)?2:-2)));
            }
        }

        // Rhythm mutation: shift only by 1/8-note cells, so we don't reintroduce
        // the accidental off-grid 1/16 positions fixed in Rhythm Engine 2.0.
        if((h%100u) < (uint32_t)(18.0f+35.0f*amount))
        {
            const int delta=((h>>18)&1u)?2:-2;
            n.step=juce::jmax(0,n.step+delta);
        }
        if((h%100u)>62u)
            n.length=juce::jlimit(1,16,n.length+(((h>>20)&1u)?1:-1));
        if((h%100u)>78u)
            n.velocity=juce::jlimit(38,118,n.velocity+(int)((h>>22)%11u)-5);
    }
    removeDuplicateNotes(notes);
    cleanMelodyLine(notes);
    replaceVisibleNotes(notes);
}
void MidiForgeAudioProcessor::evolveSelected()
{
    // Gentle evolution: preserve the current idea and mutate it rather than
    // throwing the loop away. This is intentionally a small mutation pass.
    mutateSelected(0.22f);
}

void MidiForgeAudioProcessor::regenerate()
{
buildVariationBank();
chooseVariation (0);
}
void MidiForgeAudioProcessor::regenerateVariations()
{
int keep = 0;
{
const juce::ScopedLock sl (variationsLock);
keep = selectedVariation;
}
buildVariationBank();
chooseVariation (keep);
}
void MidiForgeAudioProcessor::chooseVariation(int index)
{
std::vector<NoteEvent> selectedNotes;
int selectedBars = bars;
{
const juce::ScopedLock sl(variationsLock);
if (variations.empty())
return;
selectedVariation=juce::jlimit(0,(int)variations.size()-1,index);
selectedNotes = variations[(size_t)selectedVariation].notes;
selectedBars = variations[(size_t)selectedVariation].bars;
}
{
const juce::ScopedLock sl(activeNotesLock);
activeNotes = std::move(selectedNotes);
activeBars = selectedBars;
}
lastGlobalStep.store (-1);
}
MidiForgeAudioProcessor::Section MidiForgeAudioProcessor::mergedSelectedSong() const
{
if(variations.empty())return {};
return variations[(size_t)selectedVariation];
}
void MidiForgeAudioProcessor::emitNote(const NoteEvent& e,juce::MidiBuffer& midi,
int sampleOffset,int velocityBias)
{
if(e.step<0)return;
if(e.channel==5){ const int drow=drumRowForNote(e.note); if(drow>=0 && (drumMuteMask&(1<<drow))!=0) return; }
int velocity=juce::jlimit(1,127,e.velocity+velocityBias);
const int midiCh=(e.channel==5)?10:e.channel;      // drums = GM channel 10
midi.addEvent(juce::MidiMessage::noteOn(midiCh,e.note,(juce::uint8)velocity),sampleOffset);
const double stepSamples = sampleRate * 60.0 / juce::jmax (20.0, currentBpm.load()) / 4.0;
const juce::int64 offGlobal = samplePosition + sampleOffset
+ (juce::int64) juce::jmax (1.0, e.length * stepSamples);
pendingOffs.push_back ({ offGlobal, midiCh, e.note });
}
bool MidiForgeAudioProcessor::exportMidi(const juce::File& targetFile) const
{
const juce::ScopedLock sl(variationsLock);
if (variations.empty())
return false;
const auto& song = variations[(size_t)juce::jlimit(0, (int)variations.size() - 1, selectedVariation)];
constexpr int ppq = 960;
constexpr int ticksPerStep = ppq / 4;
juce::MidiFile file;
file.setTicksPerQuarterNote(ppq);
juce::MidiMessageSequence conductor;
const int microsecondsPerQuarterNote = juce::roundToInt (60000000.0 / juce::jmax (20.0, currentBpm.load()));
conductor.addEvent (juce::MidiMessage::tempoMetaEvent (microsecondsPerQuarterNote), 0.0);
conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
const int totalSteps = juce::jmax(1, song.bars * 16);
const double endTick = (double) totalSteps * ticksPerStep;
const auto songArt = articulationFor (song.notes);
conductor.addEvent(juce::MidiMessage::endOfTrack(), endTick + ppq);
file.addTrack(conductor);
for (int channel = 1; channel <= 5; ++channel)
{
const int midiCh = (channel == 5) ? 10 : channel;
if (channel == 5 && std::none_of (song.notes.begin(), song.notes.end(), [] (const NoteEvent& q) { return q.channel == 5; })) continue;
if (channel == 5)
{
    for (int row = 0; row < kDrumRows; ++row)
    {
        if ((drumMuteMask & (1 << row)) != 0) continue;
        juce::MidiMessageSequence dtrack;
        dtrack.addEvent (juce::MidiMessage::textMetaEvent (3, drumRowName (row)), 0.0);
        bool any = false;
        for (const auto& n : song.notes)
        {
            if (n.channel != 5 || drumRowForNote (n.note) != row) continue;
            any = true;
            const double onTick = (double) n.step * ticksPerStep;
            const double offTick = onTick + (double) juce::jmax (1, n.length) * ticksPerStep;
            const int pitch = drumOutPitch (row, n.note);
            dtrack.addEvent (juce::MidiMessage::noteOn (10, pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), onTick);
            dtrack.addEvent (juce::MidiMessage::noteOff (10, pitch), offTick);
        }
        if (any) { dtrack.updateMatchedPairs(); dtrack.addEvent (juce::MidiMessage::endOfTrack(), endTick + ppq); file.addTrack (dtrack); }
    }
    continue;
}
juce::MidiMessageSequence track;
for (size_t ei = 0; ei < song.notes.size(); ++ei)
{
const auto& e = song.notes[ei];
if (e.channel != channel)
continue;
const double onTick = (double) e.step * ticksPerStep;
double offTick = onTick + (double) juce::jmax(1, e.length) * ticksPerStep;
addArticulation (track, songArt[ei], midiCh, onTick, offTick, (double) ticksPerStep);
const int velocity = juce::jlimit(1, 127, e.velocity);
track.addEvent(juce::MidiMessage::noteOn(midiCh, e.note, (juce::uint8) velocity), onTick);
track.addEvent(juce::MidiMessage::noteOff(midiCh, e.note), offTick);
}
track.updateMatchedPairs();
track.addEvent(juce::MidiMessage::endOfTrack(), endTick + ppq);
file.addTrack(track);
}
juce::File output = targetFile.withFileExtension(".mid");
auto stream = output.createOutputStream();
if (stream == nullptr)
return false;
return file.writeTo (*stream, 1);
}
void MidiForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi)
{
audio.clear();
juce::MidiBuffer out;
for(const auto m:midi)out.addEvent(m.getMessage(),m.samplePosition);
if(auto* ph=getPlayHead()){
if(auto pos=ph->getPosition()){
currentBpm.store (pos->getBpm().orFallback (120.0));
const double ppq=pos->getPpqPosition().orFallback(0.0);
const int globalStep=(int)std::floor(ppq*4.0);
if (globalStep != lastGlobalStep.load())
{
lastGlobalStep.store (globalStep);
int local = 0;
std::array<NoteEvent, 256> dueNotes {};
int dueCount = 0;
{
    const juce::ScopedLock sl (activeNotesLock);
    if (!activeNotes.empty())
    {
        const int period = juce::jmax (16, activeBars * 16);
        local = globalStep % period;
        if (local < 0) local += period;
        for (const auto& e : activeNotes)
            if (e.step == local && dueCount < (int) dueNotes.size())
                dueNotes[(size_t) dueCount++] = e;
    }
}
if (dueCount > 0)
{
    uiCurrentStep.store (local);
    int offset = 0;
    if ((local % 2) == 1)
        offset = (int) (swing * sampleRate * 60.0
                        / juce::jmax (20.0, currentBpm.load()) / 8.0);
    const int velBias = (int) ((realtimeRng.nextFloat() * 2.0f - 1.0f)
                               * 14.0f * humanize);
    for (int n = 0; n < dueCount; ++n)
        emitNote (dueNotes[(size_t) n], out, offset, velBias);
}
}
}
}
const juce::int64 blockEnd = samplePosition + audio.getNumSamples();
for (size_t i = 0; i < pendingOffs.size(); )
{
auto& p = pendingOffs[i];
if (p.globalSample < blockEnd)
{
const juce::int64 local = juce::jlimit<juce::int64> (0, juce::jmax (0, audio.getNumSamples() - 1),
p.globalSample - samplePosition);
out.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), (int) local);
p = pendingOffs.back();
pendingOffs.pop_back();
}
else
{
++i;
}
}
samplePosition += audio.getNumSamples();
midi.swapWith(out);
}
void MidiForgeAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
juce::MemoryOutputStream o(dest,false);
o.writeInt(rootPc);o.writeInt(genre);o.writeInt(scale);o.writeInt(progression);
o.writeInt(rhythm);o.writeInt(bars);o.writeInt(seed);o.writeInt(octave);o.writeInt(sectionMode);
o.writeFloat(chordDensity);o.writeFloat(bassDensity);o.writeFloat(melodyDensity);o.writeFloat(arpDensity);
o.writeFloat(swing);o.writeFloat(humanize);o.writeFloat(complexity);
o.writeFloat(melodyLength);o.writeFloat(pauseChance);o.writeFloat(leapChance);o.writeFloat(ghostChance);
o.writeInt(arpRate);o.writeFloat(voicingWidth);o.writeBool(chordExtensions);o.writeBool(inversions);
o.writeFloat(motifStrength);o.writeFloat(variationAmount);o.writeFloat(fillAmount);o.writeFloat(energy);
o.writeBool(chordsEnabled);o.writeBool(bassEnabled);o.writeBool(melodyEnabled);o.writeBool(arpEnabled);o.writeBool(hookMode);
o.writeInt(selectedVariation);
o.writeInt(mood);o.writeInt(melodyType);o.writeInt(era);
o.writeInt(soundTarget);
o.writeInt(articulation);o.writeInt(autoNextOnDislike?1:0);
o.writeInt(chordStyle);o.writeInt(drumsEnabled?1:0);
o.writeInt(drumMuteMask);o.writeInt(drumPitchMode);
}
void MidiForgeAudioProcessor::setStateInformation(const void* data,int size)
{
if(!data||size<=0)return;
juce::MemoryInputStream i(data,(size_t)size,false);
rootPc=i.readInt();genre=i.readInt();scale=i.readInt();progression=i.readInt();
rhythm=i.readInt();bars=i.readInt();seed=i.readInt();octave=i.readInt();sectionMode=i.readInt();
chordDensity=i.readFloat();bassDensity=i.readFloat();melodyDensity=i.readFloat();arpDensity=i.readFloat();
swing=i.readFloat();humanize=i.readFloat();complexity=i.readFloat();
melodyLength=i.readFloat();pauseChance=i.readFloat();leapChance=i.readFloat();ghostChance=i.readFloat();
arpRate=i.readInt();voicingWidth=i.readFloat();chordExtensions=i.readBool();inversions=i.readBool();
motifStrength=i.readFloat();variationAmount=i.readFloat();fillAmount=i.readFloat();energy=i.readFloat();
chordsEnabled=i.readBool();bassEnabled=i.readBool();melodyEnabled=i.readBool();arpEnabled=i.readBool();hookMode=i.readBool();
int savedSelection=i.readInt();
if (i.getNumBytesRemaining() >= 12) { mood=i.readInt(); melodyType=i.readInt(); era=i.readInt(); }
if (i.getNumBytesRemaining() >= 4) soundTarget=juce::jlimit(0,7,i.readInt());
if (i.getNumBytesRemaining() >= 8) { articulation=juce::jlimit(0,2,i.readInt()); autoNextOnDislike=i.readInt()!=0; }
if (i.getNumBytesRemaining() >= 8) { chordStyle=juce::jlimit(0,2,i.readInt()); drumsEnabled=i.readInt()!=0; }
if (i.getNumBytesRemaining() >= 8) { drumMuteMask=i.readInt() & 0xFF; drumPitchMode=juce::jlimit(0,1,i.readInt()); }
regenerate();
chooseVariation (savedSelection);
}
// --- MIDI export --------------------------------------------------------
std::vector<MidiForgeAudioProcessor::ArtInfo> MidiForgeAudioProcessor::articulationFor (const std::vector<NoteEvent>& notes) const
{
    std::vector<ArtInfo> art (notes.size());
    const auto prof = soundProfileFor (soundTarget);
    if (articulation <= 0 || (prof.slideChance <= 0.0f && prof.vibChance <= 0.0f)) return art;
    std::vector<size_t> idx;
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].channel == 3) idx.push_back (i);
    std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return notes[a].step < notes[b].step; });
    for (size_t k = 0; k < idx.size(); ++k)
    {
        const auto& cur = notes[idx[k]];
        const uint32_t hsh = hash32 (generationSeed ^ (uint32_t) (cur.step * 131 + cur.note * 17 + 5));
        if (k + 1 < idx.size())
        {
            const auto& nx = notes[idx[k + 1]];
            const int gap = nx.step - cur.step;
            const int iv = std::abs (nx.note - cur.note);
            // legato slide: the note runs on into the next one (touching notes, different pitch, no big leap)
            if (gap > 0 && gap - cur.length <= 1 && iv >= 1 && iv <= 7
                && (float) (hsh % 1000u) / 1000.0f < prof.slideChance)
            {
                art[idx[k]].slide = true;
                art[idx[k]].slideToStep = nx.step;
            }
        }
        if (articulation >= 2 && cur.length >= 4
            && (float) (hash32 (hsh ^ 0x51ed270bu) % 1000u) / 1000.0f < prof.vibChance)
            art[idx[k]].vib = true;
    }
    return art;
}

void MidiForgeAudioProcessor::addArticulation (juce::MidiMessageSequence& track, const ArtInfo& a, int channel,
                                               double onTick, double& offTick, double ticksPerStep) const
{
    if (a.slide && a.slideToStep >= 0)
        offTick = juce::jmax (offTick, (double) (a.slideToStep + 1) * ticksPerStep);   // overlap the next note by one step
    if (a.vib)
    {
        // delayed vibrato on the mod wheel (CC1): 0 -> 45 -> 85 -> 0
        const double dur = offTick - onTick;
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 0), onTick);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 45), onTick + 0.35 * dur);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 85), onTick + 0.60 * dur);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 0), offTick);
    }
}

juce::MidiFile MidiForgeAudioProcessor::buildMidiFile (int channelFilter, int drumRow) const
{
juce::MidiFile midiFile;
constexpr int ticksPerQuarter = 960;
midiFile.setTicksPerQuarterNote (ticksPerQuarter);
const int ticksPerStep = ticksPerQuarter / 4;
Section pattern;
{
const juce::ScopedLock sl (variationsLock);
pattern = mergedSelectedSong();
}
static const char* trackNames[6] = { "", "Chords", "Bass", "Melody", "Arp", "Drums" };
const auto art = articulationFor (pattern.notes);
for (int channel = 1; channel <= 5; ++channel)
{
if (channelFilter != 0 && channel != channelFilter) continue;
const int midiCh = (channel == 5) ? 10 : channel;      // drums = General MIDI channel 10
if (channel == 5 && std::none_of (pattern.notes.begin(), pattern.notes.end(), [] (const NoteEvent& q) { return q.channel == 5; })) continue;
if (channel == 5)
{
    // one track per drum instrument (kick / snare / hat ...), so each can go to its own sampler
    for (int row = 0; row < kDrumRows; ++row)
    {
        if (drumRow >= 0 && row != drumRow) continue;
        if (drumRow < 0 && (drumMuteMask & (1 << row)) != 0) continue;
        juce::MidiMessageSequence dtrack;
        dtrack.addEvent (juce::MidiMessage::textMetaEvent (3, drumRowName (row)), 0.0);
        bool any = false;
        for (const auto& n : pattern.notes)
        {
            if (n.channel != 5 || drumRowForNote (n.note) != row) continue;
            any = true;
            const double onTick = n.step * (double) ticksPerStep;
            const double offTick = onTick + juce::jmax (1, n.length) * (double) ticksPerStep;
            const int pitch = drumOutPitch (row, n.note);
            dtrack.addEvent (juce::MidiMessage::noteOn (10, pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), onTick);
            dtrack.addEvent (juce::MidiMessage::noteOff (10, pitch), offTick);
        }
        if (any) { dtrack.updateMatchedPairs(); midiFile.addTrack (dtrack); }
    }
    continue;
}
juce::MidiMessageSequence track;
track.addEvent (juce::MidiMessage::textMetaEvent (3, trackNames[channel]), 0.0);
for (size_t ni = 0; ni < pattern.notes.size(); ++ni)
{
const auto& n = pattern.notes[ni];
if (n.channel != channel) continue;
const double onTick  = n.step * (double) ticksPerStep;
double offTick = onTick + juce::jmax (1, n.length) * (double) ticksPerStep;
addArticulation (track, art[ni], midiCh, onTick, offTick, (double) ticksPerStep);
auto on = juce::MidiMessage::noteOn (midiCh, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity));
auto off = juce::MidiMessage::noteOff (midiCh, n.note);
track.addEvent (on, onTick);
track.addEvent (off, offTick);
}
track.updateMatchedPairs();
midiFile.addTrack (track);
}
return midiFile;
}
bool MidiForgeAudioProcessor::exportMidiFileTo (const juce::File& file) const
{
auto midiFile = buildMidiFile (0);
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
bool MidiForgeAudioProcessor::exportMidiFileToChannel (const juce::File& file, int channel) const
{
auto midiFile = buildMidiFile (channel);
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFile() const
{
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileTo (file);
return file;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForChannel (int channel) const
{
static const char* names[6] = { "All", "Chords", "Bass", "Melody", "Arp", "Drums" };
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (names[juce::jlimit (0, 5, channel)])
+ "_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileToChannel (file, channel);
return file;
}
std::vector<MidiForgeAudioProcessor::VisibleNote> MidiForgeAudioProcessor::getVisibleNotes() const
{
const juce::ScopedLock sl (activeNotesLock);
std::vector<VisibleNote> out;
out.reserve (activeNotes.size());
for (const auto& n : activeNotes)
out.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
return out;
}
int MidiForgeAudioProcessor::getVisibleBars() const
{
const juce::ScopedLock sl (activeNotesLock);
return activeBars;
}

void MidiForgeAudioProcessor::syncEditedNotesToSelectedVariation (const std::vector<VisibleNote>& notes)
{
    const juce::ScopedLock sl (variationsLock);
    if (selectedVariation < 0 || selectedVariation >= static_cast<int> (variations.size()))
        return;

    auto& section = variations[static_cast<size_t> (selectedVariation)];
    section.notes.clear();
    section.notes.reserve (notes.size());
    for (const auto& n : notes)
        section.notes.push_back ({ n.step, n.length, n.note, n.velocity, n.channel, false });
}

bool MidiForgeAudioProcessor::addVisibleNote (int step, int note, int length, int velocity, int channel)
{
    VisibleNote created { juce::jmax (0, step), juce::jmax (1, length),
                          juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity),
                          juce::jlimit (1, 5, channel) };
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        activeNotes.push_back ({ created.step, created.length, created.note, created.velocity, created.channel, false });
        snapshot.reserve (activeNotes.size());
        for (const auto& n : activeNotes)
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
    }
    syncEditedNotesToSelectedVariation (snapshot);
    return true;
}

bool MidiForgeAudioProcessor::editVisibleNote (int index, int step, int note, int length, int velocity)
{
    std::vector<VisibleNote> snapshot;
    bool changed = false;
    {
        const juce::ScopedLock sl (activeNotesLock);
        if (index < 0 || index >= static_cast<int> (activeNotes.size()))
            return false;
        auto& n = activeNotes[static_cast<size_t> (index)];
        n.step = juce::jmax (0, step);
        n.note = juce::jlimit (0, 127, note);
        n.length = juce::jmax (1, length);
        n.velocity = juce::jlimit (1, 127, velocity);
        snapshot.reserve (activeNotes.size());
        for (const auto& item : activeNotes)
            snapshot.push_back ({ item.step, item.length, item.note, item.velocity, item.channel });
        changed = true;
    }
    if (changed)
        syncEditedNotesToSelectedVariation (snapshot);
    return changed;
}

bool MidiForgeAudioProcessor::deleteVisibleNote (int index)
{
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        if (index < 0 || index >= static_cast<int> (activeNotes.size()))
            return false;
        activeNotes.erase (activeNotes.begin() + index);
        snapshot.reserve (activeNotes.size());
        for (const auto& n : activeNotes)
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
    }
    syncEditedNotesToSelectedVariation (snapshot);
    return true;
}

void MidiForgeAudioProcessor::replaceVisibleNotes (const std::vector<VisibleNote>& notes)
{
    std::vector<VisibleNote> cleaned;
    cleaned.reserve (notes.size());
    {
        const juce::ScopedLock sl (activeNotesLock);
        activeNotes.clear();
        for (const auto& n : notes)
        {
            const auto step = juce::jlimit (0, juce::jmax (0, activeBars * 16 - 1), n.step);
            const auto length = juce::jmax (1, n.length);
            const auto note = juce::jlimit (0, 127, n.note);
            const auto velocity = juce::jlimit (1, 127, n.velocity);
            const auto channel = juce::jlimit (1, 5, n.channel);
            activeNotes.push_back ({ step, length, note, velocity, channel, false });
            cleaned.push_back ({ step, length, note, velocity, channel });
        }
    }
    syncEditedNotesToSelectedVariation (cleaned);
}

void MidiForgeAudioProcessor::quantizeVisibleNotes (int gridSteps)
{
    const int grid = juce::jmax (1, gridSteps);
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        snapshot.reserve (activeNotes.size());
        for (auto& n : activeNotes)
        {
            n.step = juce::jmax (0, (n.step + grid / 2) / grid * grid);
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
        }
    }
    syncEditedNotesToSelectedVariation (snapshot);
}

juce::AudioProcessorEditor* MidiForgeAudioProcessor::createEditor()
{
#ifdef MIDIFORGE_HEADLESS
    return nullptr;
#else
    return new MidiForgeAudioProcessorEditor(*this);
#endif
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MidiForgeAudioProcessor();}
