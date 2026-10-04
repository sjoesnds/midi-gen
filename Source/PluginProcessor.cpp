#include "PluginProcessor.h"
#include "RhythmGrammar.h"
#include "ComposerGrammar.h"
#include "MelodicProsody.h"
#include "MotifSemantics.h"
#include "LoopClosure.h"
#include "VariationIntelligence.h"
#ifndef MIDIFORGE_HEADLESS
#include "PluginEditor.h"
#endif
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <cstdlib>
#include <limits>

namespace
{
    // Shared deterministic hash used by generation and mutation paths.
    // Kept at file scope so helper methods can use the same seed logic as addMelody().
    static uint32_t hash32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    // Project-state envelope. Legacy states had no header and started directly
    // with rootPc, so a non-matching first word is treated as the legacy format.
    static constexpr int kStateMagic = 0x4D464752; // "MFGR"
    static constexpr int kStateVersion = 2;
}
namespace
{
    // 0.39 Sound Profiles: the melody is written for a *type of sound*, not only
    // for a piano.  A piano tolerates sparse, staccato, velocity-driven notes;
    // a lead needs legato, a pluck needs short consistent hits, a pad needs long
    // stepwise notes, a bell needs high register and ringing notes.
    struct SoundProfile
    {
        int   laneShift;      // semitones added to the melody register lane
        int   laneCap;        // absolute upper limit of the melody lane
        float legato;         // fraction of the gap to the next note that is sustained (<0: use Melody Length slider)
        int   minLen, maxLen; // note length clamp in steps
        int   velCenter;      // velocity compression centre
        float velSpread;      // 1 = unchanged, lower = flatter dynamics
        int   minNotes, minHits; // playable density floor (notes per bar / pattern size)
        float densityMul;     // scales melody density and the judge density targets
        int   maxLeap;        // max interval to the previous note in semitones (0 = unlimited)
        int   chordLen;       // chord hit length in steps (16 = sustained)
        bool  chordTwoHits;   // second chord hit on beat 3 (stab styles)
        float slideChance;    // 0.42: chance of a legato slide into the next note (Articulation)
        float vibChance;      // 0.42: chance of vibrato on a long note (Articulation = Slides + Vibrato)
        bool  bassOff;        // the melody IS the bass voice (808): no separate bass layer
        bool  soloLine;       // 0.43.2: the loop is ONE line (808): no chord / bass / arp layers, real 808 generator
    };
    static SoundProfile soundProfileFor (int id)
    {
        switch (id)
        {
            // 0.71.1 range expansion: regular melodic voices get a much wider
            // playable lane. The lane is intentionally broad; phrase/register
            // grammar still decides whether a given melody actually uses it.
            //                    shift cap  legato  min max  vc   vspr   nn nh  dens   leap cLen 2hit slide vib  bassOff solo
            case 1:  return {   0, 100,  0.00f,  1,  2,  88, 0.45f,  5, 5, 1.10f, 12,   6, true,  0.00f, 0.0f, false, false }; // Pluck
            case 2:  return {   2, 108,  0.94f,  2, 16,  96, 0.40f,  4, 5, 0.95f,  9,  16, false, 0.30f, 0.5f, false, false }; // Synth Lead
            case 3:  return {   8, 112,  0.60f,  3,  6,  82, 0.60f,  3, 4, 0.72f, 12,  16, false, 0.00f, 0.0f, false, false }; // Bell / Mallet
            case 4:  return {  -4,  94,  1.00f,  4, 16,  76, 0.30f,  2, 3, 0.50f,  5,  16, false, 0.00f, 0.0f, false, false }; // Pad / Strings
            case 5:  return {  -2, 102,  0.55f,  2,  6,  98, 0.70f,  4, 5, 0.90f,  7,   5, true,  0.00f, 0.0f, false, false }; // Brass
            case 6:  return { -30,  60,  0.85f,  2, 16, 100, 0.30f,  2, 3, 0.55f,  7,  16, false, 0.55f, 0.0f, true, true }; // 808 / Sub Lead
            case 7:  return {  -8,  96,  0.35f,  1,  6,  86, 0.70f,  4, 5, 1.00f,  9,   8, true,  0.20f, 0.0f, false, false }; // Guitar
            default: return {   0, 104, -1.00f,  1, 16,  80, 1.00f,  4, 5, 1.00f, 12,  16, false, 0.00f, 0.0f, false, false }; // Piano (neutral)
        }
    }
}
static juce::File& settingsDirectoryOverride()
{
    static juce::File dir;
    return dir;
}

void MidiForgeAudioProcessor::setSettingsDirectoryOverride (const juce::File& dir)
{
    settingsDirectoryOverride() = dir;
}

MidiForgeAudioProcessor::MidiForgeAudioProcessor()
: AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    {
        const juce::File overrideDir = settingsDirectoryOverride();
        const juce::File base = overrideDir != juce::File()
            ? overrideDir
            : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MidiForge");
        preferencesFile = base.getChildFile ("taste.json");
    }
    feedbackFile = preferencesFile.getSiblingFile ("feedback.csv");
    loadPreferences();
    realtimeSwing.store (swing);
    realtimeHumanize.store (humanize);
    realtimeHumanizeEnabled.store (humanizeEnabled);
    realtimeDrumMuteMask.store (drumMuteMask);
    regenerateBlocking (0);
}

MidiForgeAudioProcessor::~MidiForgeAudioProcessor()
{
    waitForGeneration();
}
bool MidiForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void MidiForgeAudioProcessor::prepareToPlay(double sr, int)
{
sampleRate = sr; lastGlobalStep.store (-1);
samplePosition = 0;
pendingEvents.clear();
pendingEvents.reserve (4096);
}
void MidiForgeAudioProcessor::setRoot(int v){rootPc=juce::jlimit(0,11,v);regenerate();}
void MidiForgeAudioProcessor::setGenre(int v){genre=juce::jlimit(0,15,v);regenerate();}
void MidiForgeAudioProcessor::setScale(int v){scale=juce::jlimit(0,11,v);regenerate();}
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
void MidiForgeAudioProcessor::setSectionMode(int v){ sectionMode = juce::jlimit((int)Loop, (int)SongExtended, v); regenerate(); }
void MidiForgeAudioProcessor::setChordDensity(float v, bool regenerateNow){chordDensity=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setBassDensity(float v, bool regenerateNow){bassDensity=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setMelodyDensity(float v, bool regenerateNow){melodyDensity=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setArpDensity(float v, bool regenerateNow){arpDensity=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setSwing(float v){swing=juce::jlimit(0.f,.75f,v);realtimeSwing.store(swing);}
void MidiForgeAudioProcessor::setHumanize(float v){humanize=juce::jlimit(0.f,1.f,v);realtimeHumanize.store(humanize);}
void MidiForgeAudioProcessor::setHumanizeEnabled(bool on){if(humanizeEnabled==on)return;humanizeEnabled=on;realtimeHumanizeEnabled.store(on);regenerate();}
void MidiForgeAudioProcessor::setComplexity(float v, bool regenerateNow){complexity=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setMelodyLength(float v, bool regenerateNow){melodyLength=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setPauseChance(float v, bool regenerateNow){pauseChance=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setLeapChance(float v, bool regenerateNow){leapChance=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setGhostChance(float v, bool regenerateNow){ghostChance=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setArpRate(int v){arpRate=juce::jlimit(1,8,v);regenerate();}
void MidiForgeAudioProcessor::setVoicingWidth(float v){voicingWidth=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setChordExtensions(bool v){chordExtensions=v;regenerate();}
void MidiForgeAudioProcessor::setInversions(bool v){inversions=v;regenerate();}
void MidiForgeAudioProcessor::setMotifStrength(float v, bool regenerateNow){motifStrength=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setVariationAmount(float v, bool regenerateNow){variationAmount=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerateVariations();}
void MidiForgeAudioProcessor::setFillAmount(float v, bool regenerateNow){fillAmount=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setEnergy(float v, bool regenerateNow){energy=juce::jlimit(0.f,1.f,v);if(regenerateNow)regenerate();}
void MidiForgeAudioProcessor::setChordsEnabled(bool v){chordsEnabled=v;}
void MidiForgeAudioProcessor::setBassEnabled(bool v){bassEnabled=v;}
void MidiForgeAudioProcessor::setMelodyEnabled(bool v){melodyEnabled=v;}
void MidiForgeAudioProcessor::setArpEnabled(bool v){arpEnabled=v;}
void MidiForgeAudioProcessor::setHookMode(bool v){hookMode=v;regenerate();}
std::vector<int> MidiForgeAudioProcessor::scaleSemitones() const
{
switch(scale)
{
case Major:         return {0,2,4,5,7,9,11};
case Minor:         return {0,2,3,5,7,8,10};
case Dorian:        return {0,2,3,5,7,9,10};
case Phrygian:      return {0,1,3,5,7,8,10};
case Lydian:        return {0,2,4,6,7,9,11};
case Mixolydian:    return {0,2,4,5,7,9,10};
case Locrian:       return {0,1,3,5,6,8,10};
case HarmonicMinor: return {0,2,3,5,7,8,11};
case MelodicMinor:  return {0,2,3,5,7,9,11};
case HarmonicMajor: return {0,2,4,5,7,8,11};
case Pentatonic:    return {0,2,4,7,9};
case Blues:         return {0,3,5,6,7,10};
default:            return {0,2,4,5,7,9,11};
}
}
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
if(progression==Pop)return{0,4,5,3};
if(progression==Dark)return{0,5,2,6};
if(progression==Emotional)return{5,3,0,4};
if(progression==CinematicProg)return{0,3,4,5};
if(progression==JazzLike)return{1,4,0,3};
if(progression==Looping)return{0,5,3,4};
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
            // 0.71.1: broaden the practical melody lane before the
            // profile cap. Wide/register-heavy profiles can now explore more
            // than the old two-octave window without forcing every phrase wide.
            lo = 48 + shift + prof.laneShift;
            hi = juce::jmin (104 + shift + prof.laneShift, prof.laneCap);
            lo = juce::jmin (lo, hi - 18);
            break;
        }
    }
}

void MidiForgeAudioProcessor::melodyCoreLane (int& lo, int& hi) const
{
    const auto profile = soundProfileFor (soundTarget);

    if (profile.soloLine)
    {
        lo = 28;
        hi = 50;
        return;
    }

    int baseLo = 48, baseHi = 96;
    registerLane (2, baseLo, baseHi);

    // 0.82.3: the Melody Core was hard-pinned to roughly C4..D5 (62..86),
    // so later safety stages inherited a narrow source phrase and could not
    // recover register variety. The lane is now role-aware.
    int targetSpan = 30;
    switch (melodyType)
    {
        case HookMelody:        targetSpan = 30; break;
        case VocalLikeMelody:  targetSpan = 34; break;
        case RiffMelody:       targetSpan = 40; break;
        case OstinatoMelody:   targetSpan = 24; break;
        case ArpMelody:        targetSpan = 32; break;
        case CounterMelody:    targetSpan = 40; break;
        case SparseLeadMelody: targetSpan = 28; break;
        case PhraseMelody:     targetSpan = 36; break;
    }

    targetSpan += juce::roundToInt (juce::jlimit (0.0f, 1.0f, complexity) * 6.0f);
    if (genre == Experimental || genre == Cinematic)
        targetSpan += 3;
    targetSpan = juce::jlimit (22, 44, targetSpan);

    const int centre = (baseLo + baseHi) / 2;
    const int half = targetSpan / 2;
    lo = centre - half;
    hi = lo + targetSpan;

    lo = juce::jmax (lo, baseLo);
    hi = juce::jmin (hi, baseHi);
    hi = juce::jmin (hi, profile.laneCap);
    lo = juce::jmax (0, lo);
    hi = juce::jmin (127, juce::jmax (lo + 1, hi));
}

void MidiForgeAudioProcessor::melodyRegisterContract (int& lo, int& hi, int& maxLeap) const
{
    const auto profile = soundProfileFor (soundTarget);

    if (profile.soloLine)
    {
        lo = 28;
        hi = 50;
        maxLeap = 7;
        return;
    }

    melodyCoreLane (lo, hi);

    constexpr int maxPracticalSpan = 44;
    if (hi - lo > maxPracticalSpan)
        hi = lo + maxPracticalSpan;

    lo = juce::jlimit (0, 127, lo);
    hi = juce::jlimit (lo + 1, 127, hi);
    hi = juce::jmin (hi, profile.laneCap);
    maxLeap = juce::jlimit (4, 9, profile.maxLeap > 0 ? profile.maxLeap : 9);
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
int best=midi,bestDist=999;
for(int o=1;o<=9;++o)for(int p:scaleSemitones()){
int n=12*o+rootPc+p,d=std::abs(n-midi);
if(d<bestDist){bestDist=d;best=n;}
}
return juce::jlimit(0,127,best);
}


float MidiForgeAudioProcessor::melodyPleasantnessScore (const Section& section) const
{
    std::vector<const NoteEvent*> melody;
    melody.reserve (section.notes.size());
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    if (melody.size() < 2)
        return melody.empty() ? 0.0f : 0.55f;

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    const auto scale = scaleSemitones();
    const int barsN = juce::jmax (1, section.bars);

    auto pitchClass = [] (int n)
    {
        return (n % 12 + 12) % 12;
    };

    auto inScale = [&] (int pitch)
    {
        const int rel = (pitchClass (pitch) - rootPc + 12) % 12;
        return std::find (scale.begin(), scale.end(), rel) != scale.end();
    };

    float scaleFit = 0.0f;
    float stepFit = 0.0f;
    float leapRecovery = 0.60f;
    float repeatFit = 1.0f;
    float anchorFit = 0.55f;
    float registerFit = 0.65f;
    float cadenceFit = 0.55f;

    int intervals = 0;
    int stepwise = 0;
    int largeLeaps = 0;
    int recovered = 0;
    int badRepeatRuns = 0;

    for (size_t i = 0; i < melody.size(); ++i)
    {
        if (inScale (melody[i]->note))
            scaleFit += 1.0f;

        if (i == 0)
            continue;

        ++intervals;
        const int d = melody[i]->note - melody[i - 1]->note;
        const int ad = std::abs (d);

        if (ad <= 4) ++stepwise;
        if (ad >= 8)
        {
            ++largeLeaps;
            if (i + 1 < melody.size())
            {
                const int next = melody[i + 1]->note - melody[i]->note;
                if ((d > 0 && next < 0) || (d < 0 && next > 0))
                    if (std::abs (next) <= 5)
                        ++recovered;
            }
        }
    }

    if (intervals > 0)
    {
        stepFit = (float) stepwise / (float) intervals;
        if (largeLeaps > 0)
            leapRecovery = (float) recovered / (float) largeLeaps;

        int sameRun = 1;
        for (size_t i = 1; i < melody.size(); ++i)
        {
            if (melody[i]->note == melody[i - 1]->note)
                ++sameRun;
            else
            {
                if (sameRun >= 3) ++badRepeatRuns;
                sameRun = 1;
            }
        }
        if (sameRun >= 3) ++badRepeatRuns;

        repeatFit = 1.0f - juce::jlimit (0.0f, 1.0f,
            (float) badRepeatRuns / juce::jmax (1.0f, (float) melody.size() / 5.0f));
    }

    // Downbeats and the midpoint of a bar should usually agree with the active
    // harmony. This is deliberately a preference, not a hard arpeggio rule.
    int anchors = 0;
    int chordAnchors = 0;
    std::vector<int> finalBarChord;
    std::vector<int> finalBarScaleCandidates;

    for (const auto& n : section.notes)
    {
        if (n.channel != 1)
            continue;

        const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
        if (bar == barsN - 1)
            finalBarChord.push_back (pitchClass (n.note));
    }

    for (size_t i = 0; i < melody.size(); ++i)
    {
        const int local = melody[i]->step % 16;
        const bool anchor = local == 0
                         || (local == 8 && melody[i]->length >= 2);
        if (! anchor)
            continue;

        ++anchors;
        const int bar = juce::jlimit (0, barsN - 1, melody[i]->step / 16);
        std::vector<int> chordPcs;
        for (const auto& n : section.notes)
            if (n.channel == 1 && n.step / 16 == bar)
                chordPcs.push_back (pitchClass (n.note));

        if (! chordPcs.empty()
            && std::find (chordPcs.begin(), chordPcs.end(), pitchClass (melody[i]->note)) != chordPcs.end())
            ++chordAnchors;
    }

    if (anchors > 0)
        anchorFit = (float) chordAnchors / (float) anchors;

    // Keep the melodic voice in a comfortable lead register. This is a soft
    // score because some sound profiles intentionally sit lower/higher.
    float meanPitch = 0.0f;
    for (const auto* n : melody)
        meanPitch += (float) n->note;
    meanPitch /= (float) melody.size();
    registerFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (meanPitch - 72.0f) / 20.0f);

    if (! finalBarChord.empty())
    {
        const int lastPitch = pitchClass (melody.back()->note);
        const bool chordTone = std::find (
            finalBarChord.begin(), finalBarChord.end(), lastPitch) != finalBarChord.end();

        int tonic = rootPc;
        int third = rootPc;
        const auto sc = scaleSemitones();
        if (sc.size() >= 3)
            third = (rootPc + sc[2]) % 12;

        cadenceFit = chordTone ? 0.88f : 0.36f;
        if (lastPitch == tonic || lastPitch == third)
            cadenceFit += 0.08f;
        cadenceFit = juce::jlimit (0.0f, 1.0f, cadenceFit);
    }

    // A good generated line is mostly stepwise, permits occasional expressive
    // leaps, lands on harmony at structural points, and returns to a stable
    // register. The scale check is intentionally the strongest invariant.
    return juce::jlimit (0.0f, 1.0f,
        0.30f * (scaleFit / (float) melody.size())
        + 0.18f * stepFit
        + 0.14f * leapRecovery
        + 0.12f * repeatFit
        + 0.12f * anchorFit
        + 0.08f * registerFit
        + 0.06f * cadenceFit);
}

void MidiForgeAudioProcessor::applyMelodyPleasantness (Section& section, uint32_t identity) const
{
    if (section.notes.empty() || ! melodyEnabled || soundProfileFor (soundTarget).soloLine)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.size() < 2)
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    const auto scale = scaleSemitones();
    int laneLo = 40, laneHi = 96, contractLeap = 9;
    melodyRegisterContract (laneLo, laneHi, contractLeap);

    const bool conservative = (hash32 (identity ^ 0xB41A5AFEu) % 100u) < 72u;
    const int maxLeap = conservative ? juce::jmin (7, contractLeap) : contractLeap;

    auto pitchClass = [] (int n)
    {
        return (n % 12 + 12) % 12;
    };

    auto inScale = [&] (int pitch)
    {
        const int rel = (pitchClass (pitch) - rootPc + 12) % 12;
        return std::find (scale.begin(), scale.end(), rel) != scale.end();
    };

    auto nearestScale = [&] (int target, int lo, int hi)
    {
        lo = juce::jlimit (0, 127, lo);
        hi = juce::jlimit (lo, 127, hi);
        int best = juce::jlimit (lo, hi, target);
        int bestDistance = 1000;
        for (int p = lo; p <= hi; ++p)
        {
            if (! inScale (p))
                continue;
            const int d = std::abs (p - target);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = p;
            }
        }
        return best;
    };

    auto chordPcsForBar = [&] (int bar)
    {
        std::vector<int> pcs;
        for (const auto& n : section.notes)
            if (n.channel == 1 && n.step / 16 == bar)
                pcs.push_back (pitchClass (n.note));

        std::sort (pcs.begin(), pcs.end());
        pcs.erase (std::unique (pcs.begin(), pcs.end()), pcs.end());
        return pcs;
    };

    // 1) Hard scale safety and moderate register.
    for (const auto index : melody)
    {
        auto& n = section.notes[index];
        n.note = nearestScale (n.note, laneLo, laneHi);
    }

    // 2) Break pathological repeated-note runs while preserving the local
    // rhythmic idea. Only the third+ identical note is altered.
    int sameRun = 1;
    for (size_t k = 1; k < melody.size(); ++k)
    {
        auto& prev = section.notes[melody[k - 1]];
        auto& cur = section.notes[melody[k]];

        if (cur.note == prev.note)
            ++sameRun;
        else
            sameRun = 1;

        if (sameRun >= 3)
        {
            const auto chordPcs = chordPcsForBar (juce::jlimit (
                0, juce::jmax (0, section.bars - 1), cur.step / 16));
            int target = cur.note + (((hash32 (identity ^ (uint32_t) k * 0x9e3779b9u) & 1u) != 0u) ? 2 : -2);

            if (! chordPcs.empty())
            {
                int best = target;
                int bestDistance = 1000;
                for (const int pc : chordPcs)
                {
                    for (int oct = 3; oct <= 8; ++oct)
                    {
                        const int candidate = oct * 12 + pc;
                        if (candidate < laneLo || candidate > laneHi)
                            continue;
                        const int d = std::abs (candidate - target);
                        if (d < bestDistance && std::abs (candidate - prev.note) <= maxLeap)
                        {
                            bestDistance = d;
                            best = candidate;
                        }
                    }
                }
                target = best;
            }

            cur.note = nearestScale (target,
                juce::jmax (laneLo, prev.note - maxLeap),
                juce::jmin (laneHi, prev.note + maxLeap));
            sameRun = 1;
        }
    }

    // 3) Large leaps become occasional gestures, not the default vocabulary.
    for (size_t k = 1; k < melody.size(); ++k)
    {
        auto& prev = section.notes[melody[k - 1]];
        auto& cur = section.notes[melody[k]];
        const int delta = cur.note - prev.note;
        if (std::abs (delta) <= maxLeap)
            continue;

        const int bar = juce::jlimit (0, juce::jmax (0, section.bars - 1), cur.step / 16);
        const auto chordPcs = chordPcsForBar (bar);
        const bool strongBeat = (cur.step % 16) == 0 || (cur.step % 16) == 8;

        int target = prev.note + juce::jlimit (-maxLeap, maxLeap, delta);
        if (strongBeat && ! chordPcs.empty())
        {
            int best = target;
            int bestDistance = 1000;
            for (const int pc : chordPcs)
            {
                for (int oct = 3; oct <= 8; ++oct)
                {
                    const int candidate = oct * 12 + pc;
                    if (candidate < laneLo || candidate > laneHi)
                        continue;
                    const int distance = std::abs (candidate - cur.note);
                    if (distance < bestDistance
                        && std::abs (candidate - prev.note) <= maxLeap)
                    {
                        bestDistance = distance;
                        best = candidate;
                    }
                }
            }
            target = best;
        }

        cur.note = nearestScale (target,
            juce::jmax (laneLo, prev.note - maxLeap),
            juce::jmin (laneHi, prev.note + maxLeap));
    }

    // 4) Do not rewrite good structural pitches here.
    // Harmonic Intelligence already handled chord gravity upstream. This stage
    // is intentionally safety-only so it does not turn a finished contour into
    // another chord-tone pass before judging.

    // 5) Final invariant: scale-safe and leap-safe once more.
    int previous = -1;
    for (const auto index : melody)
    {
        auto& n = section.notes[index];
        n.note = nearestScale (n.note, laneLo, laneHi);
        if (previous >= 0)
        {
            n.note = nearestScale (n.note,
                juce::jmax (laneLo, previous - maxLeap),
                juce::jmin (laneHi, previous + maxLeap));
        }
        previous = n.note;
    }

    juce::ignoreUnused (profile);
    cleanMelodyLine (section.notes);
}

void MidiForgeAudioProcessor::applyMelodyFoundation (Section& section, uint32_t identity) const
{
    // 0.77 Melody Foundation:
    // The earlier engines are allowed to explore; this layer is the final
    // musical safety contract before a melody is accepted.
    if (section.notes.empty() || ! melodyEnabled || soundProfileFor (soundTarget).soloLine)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.empty())
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    int laneLo = 40, laneHi = 96, contractLeap = 9;
    melodyRegisterContract (laneLo, laneHi, contractLeap);
    const bool simple = (hash32 (identity ^ 0xA11CE55u) % 100u) < 58u;
    const int maxLeap = simple ? juce::jmin (7, contractLeap) : contractLeap;
    const auto scale = scaleSemitones();

    auto pitchClass = [] (int n)
    {
        return (n % 12 + 12) % 12;
    };

    auto inScale = [&] (int pitch)
    {
        const int pc = pitchClass (pitch);
        const int rel = (pc - rootPc + 12) % 12;
        return std::find (scale.begin(), scale.end(), rel) != scale.end();
    };

    auto nearestScalePitch = [&] (int target, int lo, int hi)
    {
        lo = juce::jlimit (0, 127, lo);
        hi = juce::jlimit (lo, 127, hi);

        int best = juce::jlimit (lo, hi, target);
        int bestDistance = 1000;
        for (int p = lo; p <= hi; ++p)
        {
            if (! inScale (p))
                continue;

            const int distance = std::abs (p - target);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = p;
            }
        }
        return best;
    };

    // Register placement. The composer plans the line inside registerLane(2) (about 62-86), but this lane is tonic-relative
    // (about 44-68 in a typical key). Clamping every note into it piled 35-45% of the notes onto the single highest scale
    // tone under the ceiling (measured: the top pitch was also the most-used pitch in ~75% of loops) and caused long
    // same-pitch runs. Move the whole melody by whole octaves into the lane first, so the contour survives and only a
    // genuinely too-wide line is clamped.
    if (! melody.empty())
    {
        int bestShift = 0;
        float bestCost = 1.0e9f;
        for (int k = -4; k <= 4; ++k)
        {
            float cost = 0.0f;
            for (const auto index : melody)
            {
                const int p = section.notes[index].note + 12 * k;
                if (p < laneLo) cost += (float) (laneLo - p);
                if (p > laneHi) cost += (float) (p - laneHi);
            }
            cost += 0.01f * (float) std::abs (12 * k);   // prefer the smaller move on a tie
            if (cost < bestCost) { bestCost = cost; bestShift = 12 * k; }
        }
        if (bestShift != 0)
            for (const auto index : melody)
                section.notes[index].note = juce::jlimit (0, 127, section.notes[index].note + bestShift);
    }

    int previous = -1;
    for (const auto index : melody)
    {
        auto& n = section.notes[index];
        int note = nearestScalePitch (n.note, laneLo, laneHi);

        if (previous >= 0)
        {
            const int lo = juce::jmax (laneLo, previous - maxLeap);
            const int hi = juce::jmin (laneHi, previous + maxLeap);
            note = nearestScalePitch (note, lo, hi);
        }

        n.note = juce::jlimit (laneLo, laneHi, note);
        previous = n.note;
    }

    // Harmony has already been handled by Harmonic Intelligence.
    // Foundation is deliberately safety-first: it must not rewrite a good
    // contour merely because a beat is not currently a chord tone.

    // Final invariant pass after anchor edits.
    previous = -1;
    for (const auto index : melody)
    {
        auto& n = section.notes[index];
        n.note = nearestScalePitch (n.note, laneLo, laneHi);

        if (previous >= 0)
        {
            const int lo = juce::jmax (laneLo, previous - maxLeap);
            const int hi = juce::jmin (laneHi, previous + maxLeap);
            n.note = nearestScalePitch (n.note, lo, hi);
        }

        previous = n.note;
    }

    cleanMelodyLine (section.notes);
}

void MidiForgeAudioProcessor::enforceFinalMelodyContract (Section& section, uint32_t identity) const
{
    if (section.notes.empty() || ! melodyEnabled || soundProfileFor (soundTarget).soloLine)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.empty())
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    int laneLo = 40, laneHi = 96, contractLeap = 9;
    melodyRegisterContract (laneLo, laneHi, contractLeap);
    const auto scale = scaleSemitones();

    auto pitchClass = [] (int n)
    {
        return (n % 12 + 12) % 12;
    };

    auto inScale = [&] (int pitch)
    {
        const int rel = (pitchClass (pitch) - rootPc + 12) % 12;
        return std::find (scale.begin(), scale.end(), rel) != scale.end();
    };

    auto nearestScale = [&] (int target, int lo, int hi)
    {
        lo = juce::jlimit (0, 127, lo);
        hi = juce::jlimit (lo, 127, hi);

        int best = juce::jlimit (lo, hi, target);
        int bestDistance = 1000;
        for (int p = lo; p <= hi; ++p)
        {
            if (! inScale (p))
                continue;

            const int d = std::abs (p - target);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = p;
            }
        }
        return best;
    };

    const bool preserveIntentionalRepetition = (melodyType == OstinatoMelody);
    const bool preserveStepwiseArp = (melodyType == ArpMelody);
    const int walkThreshold = ((hash32 (identity ^ 0x71A7C0DEu) % 100u) < 58u) ? 5 : 4;

    float walkBreakChance = 0.76f;
    switch (melodyType)
    {
        case HookMelody:        walkBreakChance = 0.68f; break;
        case VocalLikeMelody:  walkBreakChance = 0.72f; break;
        case RiffMelody:       walkBreakChance = 0.88f; break;
        case CounterMelody:    walkBreakChance = 0.90f; break;
        case SparseLeadMelody: walkBreakChance = 0.80f; break;
        case PhraseMelody:     walkBreakChance = 0.84f; break;
        default: break;
    }

    int previous = -1;
    int sameRun = 0;
    int walkDirection = 0;
    int walkLength = 0;
    int previousBar = -1;

    for (size_t i = 0; i < melody.size(); ++i)
    {
        auto& n = section.notes[melody[i]];
        const int currentBar = n.step / 16;

        if (currentBar != previousBar)
        {
            walkDirection = 0;
            walkLength = 0;
            previousBar = currentBar;
        }

        n.note = nearestScale (n.note, laneLo, laneHi);

        if (previous >= 0)
        {
            const int lo = juce::jmax (laneLo, previous - contractLeap);
            const int hi = juce::jmin (laneHi, previous + contractLeap);
            n.note = nearestScale (n.note, lo, hi);
        }

        sameRun = (previous >= 0 && n.note == previous) ? sameRun + 1 : 1;

        if (! preserveIntentionalRepetition && sameRun >= 3)
        {
            int best = n.note;
            int bestDistance = 1000;

            for (int p = juce::jmax (laneLo, previous - contractLeap);
                 p <= juce::jmin (laneHi, previous + contractLeap); ++p)
            {
                if (! inScale (p) || p == previous)
                    continue;

                const int d = std::abs (p - n.note);
                const uint32_t tie = hash32 (identity ^ (uint32_t) (i * 0x9E3779B9u) ^ (uint32_t) p);

                if (d < bestDistance || (d == bestDistance && (tie & 1u) == 0u))
                {
                    bestDistance = d;
                    best = p;
                }
            }

            if (best != previous)
            {
                n.note = best;
                sameRun = 1;
            }
        }

        // 0.82.5: prevent long scalar walks without outlawing stepwise melodies.
        // A short run of stepwise motion is useful; an entire bar of one-direction
        // scale climbing/descending is one of the main signatures of "AI melody".
        // Break only after a few same-direction small intervals, then let the line
        // continue naturally from the changed note.
        if (previous >= 0 && ! preserveIntentionalRepetition && ! preserveStepwiseArp)
        {
            const int delta = n.note - previous;
            const int direction = (std::abs (delta) <= 3 && delta != 0) ? (delta > 0 ? 1 : -1) : 0;

            if (direction != 0 && direction == walkDirection)
                ++walkLength;
            else
            {
                walkDirection = direction;
                walkLength = direction != 0 ? 1 : 0;
            }

            if (walkLength >= walkThreshold)
            {
                const uint32_t roll = hash32 (
                    identity ^ (uint32_t) (i * 0x85EBCA6Bu) ^ 0xA17F4D31u) % 1000u;

                if ((float) roll < walkBreakChance * 1000.0f)
                {
                    int best = n.note;
                    int bestScore = -100000;

                    for (int p = juce::jmax (laneLo, previous - contractLeap);
                         p <= juce::jmin (laneHi, previous + contractLeap); ++p)
                    {
                        if (! inScale (p) || p == previous)
                            continue;

                        const int candidateDelta = p - previous;
                        const int ad = std::abs (candidateDelta);
                        if (ad < 3 || ad > contractLeap)
                            continue;

                        const int candidateDirection = candidateDelta > 0 ? 1 : -1;
                        int score = 0;

                        // Prefer turning around the walk. Staying on the same
                        // direction is allowed only when the alternative is poor.
                        score += (candidateDirection != walkDirection) ? 120 : 0;
                        score += juce::jmin (ad, 7) * 8;
                        score -= std::abs (p - n.note) * 4;

                        const uint32_t tie = hash32 (
                            identity ^ (uint32_t) (i * 0x9E3779B9u) ^ (uint32_t) p);
                        score += (int) (tie & 15u);

                        if (score > bestScore)
                        {
                            bestScore = score;
                            best = p;
                        }
                    }

                    if (best != n.note)
                    {
                        n.note = best;
                        walkDirection = 0;
                        walkLength = 0;
                    }
                }
            }
        }

        previous = n.note;
    }
}

bool MidiForgeAudioProcessor::rhythmHit(int x) const
{
x=((x%16)+16)%16;
if(rhythm==Straight)return true;
if(rhythm==Syncopated)return(x%4==0)||(x%4==3)||(x==6)||(x==14);
if(rhythm==Broken)return(x%8==0)||x==3||x==6||x==10||x==13;
return((x*7)%16)<7;
}

void MidiForgeAudioProcessor::applyRhythmGrammar (Section& section, uint32_t identity) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine)
        return;

    std::vector<midiforge::RhythmGrammar::Note> melody;
    std::vector<size_t> indices;
    melody.reserve (section.notes.size());
    indices.reserve (section.notes.size());

    for (size_t i = 0; i < section.notes.size(); ++i)
    {
        const auto& n = section.notes[i];
        if (n.channel != 3)
            continue;
        melody.push_back ({ n.step, n.length, n.velocity });
        indices.push_back (i);
    }

    if (melody.size() < 2)
        return;

    midiforge::RhythmGrammar::apply (
        melody,
        section.bars,
        juce::jlimit (40.0, 240.0, currentBpm.load()),
        rhythm,
        complexity,
        energy,
        identity ^ 0x52A11F7Du,
        melodyType,
        genre);

    for (size_t i = 0; i < melody.size() && i < indices.size(); ++i)
    {
        auto& dst = section.notes[indices[i]];
        dst.step = juce::jlimit (0, juce::jmax (0, section.bars * 16 - 1), melody[i].step);
        dst.length = juce::jlimit (1, juce::jmax (1, 16 - (dst.step % 16)), melody[i].length);
        dst.velocity = juce::jlimit (35, 122, melody[i].velocity);
    }

    removeDuplicateNotes (section.notes);
    cleanMelodyLine (section.notes);
}


void MidiForgeAudioProcessor::applyMelodyExpression (Section& section, uint32_t identity) const
{
    // 0.67 Expressive Melody Engine:
    // Rhythm Grammar decides WHEN the notes speak. This layer decides HOW the
    // phrase speaks: contour, tension peak, answer/return gestures, sustain and
    // velocity dynamics. It deliberately runs after rhythm grammar and never
    // touches the 808/sub-lead path.
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    std::array<std::vector<size_t>, 4> phraseBars;

    std::stable_sort (section.notes.begin(), section.notes.end(),
        [] (const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step) return a.step < b.step;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.note < b.note;
        });

    // Rebuild the phrase index after the stable sort.
    for (auto& v : phraseBars) v.clear();
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
        {
            const int localBar = (section.notes[i].step / 16) & 3;
            if (localBar >= 0 && localBar < 4)
                phraseBars[(size_t) localBar].push_back (i);
        }

    auto lanePitch = [&] (int pitch) -> int
    {
        int lo = 62, hi = 86;
        melodyCoreLane (lo, hi);
        return juce::jlimit (lo, hi, snapToScale (pitch));
    };

    auto nearestChordTone = [&] (int bar, int pitch) -> int
    {
        int best = pitch;
        int bestDistance = 999;
        for (const auto& n : section.notes)
        {
            if (n.channel != 1 || n.step / 16 != bar)
                continue;

            for (int oct = -2; oct <= 2; ++oct)
            {
                const int candidate = n.note + 12 * oct;
                if (candidate < 40 || candidate > 104)
                    continue;

                const int distance = std::abs (candidate - pitch);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }
        }
        return best;
    };

    static const int gestures[8][5] =
    {
        { -1,  2,  5,  2, -1 }, // arch
        {  1,  3,  1, -2,  0 }, // question -> answer
        { -2,  0,  2,  5,  1 }, // late rise
        {  0,  4,  0, -3,  1 }, // leap -> answer
        {  0,  0,  3,  1, -1 }, // held idea -> lift
        {  2, -1,  3, -2,  0 }, // call -> response
        {  0,  3,  4,  1, -2 }, // long -> burst -> release
        {  1, -1, -3, -1,  2 }  // dark fall -> rebound
    };

    const int completePhrases = section.bars / 4;
    for (int phrase = 0; phrase < completePhrases; ++phrase)
    {
        std::array<std::vector<size_t>, 4> bars4;
        for (int role = 0; role < 4; ++role)
        {
            const int absoluteBar = phrase * 4 + role;
            for (size_t i = 0; i < section.notes.size(); ++i)
                if (section.notes[i].channel == 3 && section.notes[i].step / 16 == absoluteBar)
                    bars4[(size_t) role].push_back (i);

            std::stable_sort (bars4[(size_t) role].begin(), bars4[(size_t) role].end(),
                [&] (size_t a, size_t b) { return section.notes[a].step < section.notes[b].step; });
        }

        if (bars4[0].size() < 2)
            continue;

        const uint32_t phraseSeed = hash32 (identity
                                           ^ (uint32_t) (phrase + 1) * 0x9e3779b9u
                                           ^ 0x6D2B79F5u);
        const int gestureId = (int) (phraseSeed % 8u);
        const float gestureAmount = 0.72f
            + 0.22f * ((float) ((phraseSeed >> 8) & 255u) / 255.0f);

        const int anchorPitch = section.notes[bars4[0].front()].note;
        const int peakLift = 4 + (int) ((phraseSeed >> 16) % 4u);

        for (int role = 0; role < 4; ++role)
        {
            auto& current = bars4[(size_t) role];
            if (current.empty())
                continue;

            const int absoluteBar = phrase * 4 + role;
            const float memoryBlend =
                role == 0 ? 0.10f :
                role == 1 ? 0.40f :
                role == 2 ? 0.52f : 0.46f;

            for (size_t i = 0; i < current.size(); ++i)
            {
                auto& n = section.notes[current[i]];

                const size_t refIndex = bars4[0].size() <= 1
                    ? 0
                    : (size_t) juce::jlimit (
                        0,
                        (int) bars4[0].size() - 1,
                        juce::roundToInt ((double) i
                                          * (double) (bars4[0].size() - 1)
                                          / (double) juce::jmax<size_t> (1, current.size() - 1)));

                const auto& ref = section.notes[bars4[0][refIndex]];
                const int referenceContour = ref.note - anchorPitch;
                int target = n.note;

                if (role == 1)
                {
                    // A': recognizable contour, but re-voiced around this bar.
                    target = n.note * 0.58f + (n.note + referenceContour) * 0.42f;
                }
                else if (role == 2)
                {
                    // B: invert part of the opening contour and widen the phrase.
                    target = n.note * 0.48f + (n.note - (int) std::round (0.76f * referenceContour)) * 0.52f;

                    const int midpoint = (int) (current.size() / 2);
                    if (i == (size_t) midpoint)
                        target += peakLift;
                    else if (i + 1 == current.size() && current.size() >= 3)
                        target -= 2;
                }
                else if (role == 3)
                {
                    // A'': return toward the opening contour without cloning it.
                    target = n.note * (1.0f - memoryBlend)
                           + (n.note + (int) std::round (0.86f * referenceContour)) * memoryBlend;

                    if (i + 1 == current.size())
                        target -= 2 + (int) ((phraseSeed >> 24) & 1u);
                }

                const float localT = current.size() <= 1
                    ? 0.5f
                    : (float) i / (float) (current.size() - 1);
                const int gestureSlot = juce::jlimit (0, 4, (int) std::floor (localT * 4.999f));
                float gestureWeight = gestureAmount;

                if (role == 0)
                    gestureWeight *= 0.26f;
                else if (role == 1)
                    gestureWeight *= 0.54f;
                else if (role == 2)
                    gestureWeight *= 0.88f;
                else
                    gestureWeight *= 0.46f;

                if (i == 0 || i + 1 == current.size())
                    gestureWeight *= 0.45f;

                target += juce::roundToInt (
                    (float) gestures[gestureId][gestureSlot] * gestureWeight);

                // Strong beats remain harmonically grounded, but the line is not
                // forced onto chord tones on every event.
                const bool strong = (n.step % 4) == 0;
                if (strong || n.length >= 4)
                {
                    const int chordTarget = nearestChordTone (absoluteBar, target);
                    const float chordBlend = role == 2 ? 0.12f : 0.22f;
                    target = juce::roundToInt (
                        (1.0f - chordBlend) * (float) target
                        + chordBlend * (float) chordTarget);
                }

                // Give the B peak a genuine register change rather than only a
                // single-note random jump. The surrounding note remains useful.
                if (role == 2 && i == current.size() / 2)
                    target += juce::roundToInt (0.45f * (float) peakLift);

                n.note = lanePitch (target);

                // Phrase dynamics: anchors are clear, the B peak speaks, and A''
                // relaxes into the loop seam. This is deliberately independent
                // of Humanize mode so a plain piano already exposes the expression.
                float targetVelocity = 74.0f;
                if (n.step % 4 == 0) targetVelocity += 8.0f;
                if (role == 1) targetVelocity += 2.0f;
                if (role == 2) targetVelocity += 7.0f;
                if (role == 3) targetVelocity -= 3.0f;
                if (i == current.size() / 2) targetVelocity += (role == 2 ? 8.0f : 2.0f);
                if (i + 1 == current.size()) targetVelocity -= 2.0f;

                const float shapedVelocity = 0.68f * (float) n.velocity
                                           + 0.32f * targetVelocity;
                n.velocity = juce::jlimit (42, 118, juce::roundToInt (shapedVelocity));

                const int nextStep =
                    (i + 1 < current.size())
                        ? section.notes[current[i + 1]].step
                        : (absoluteBar + 1) * 16;
                const int gap = juce::jmax (1, nextStep - n.step);

                // Meaningful sustain: anchors/peaks hold, connective notes release.
                int desiredLength = n.length;
                if (n.step % 4 == 0 || (role == 2 && i == current.size() / 2))
                    desiredLength = juce::jmax (desiredLength, juce::jmin (gap, 3 + (int) ((phraseSeed >> 5) & 1u)));
                if (role == 2 && i > 0 && (i + 1) < current.size() && (i & 1u) == 0)
                    desiredLength = juce::jmin (desiredLength, juce::jmax (1, gap - 1));
                if (role == 3 && i + 1 == current.size())
                    desiredLength = juce::jmin (gap, juce::jmax (2, desiredLength + 1));

                n.length = juce::jlimit (1, gap, desiredLength);
            }
        }

        // One small contour correction prevents the phrase from degenerating into
        // a long string of identical stepwise moves after scale snapping.
        std::vector<size_t> all;
        for (const auto& v : bars4)
            all.insert (all.end(), v.begin(), v.end());
        std::stable_sort (all.begin(), all.end(),
            [&] (size_t a, size_t b) { return section.notes[a].step < section.notes[b].step; });

        for (size_t i = 2; i < all.size(); ++i)
        {
            auto& a = section.notes[all[i - 2]];
            auto& b = section.notes[all[i - 1]];
            auto& d = section.notes[all[i]];
            const int firstInterval = b.note - a.note;
            const int secondInterval = d.note - b.note;

            if (std::abs (firstInterval) <= 2
                && std::abs (secondInterval) <= 2
                && firstInterval != 0
                && secondInterval != 0
                && ((firstInterval > 0) == (secondInterval > 0)))
            {
                const int correction = firstInterval > 0 ? -4 : 4;
                d.note = lanePitch (d.note + correction);
            }
        }
    }
}

float MidiForgeAudioProcessor::melodyExpressionScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine)
        return 1.0f;

    std::vector<const NoteEvent*> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    if (melody.size() < 4)
        return 0.45f;

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

    int intervals = 0;
    int turnCount = 0;
    int usefulLeaps = 0;
    std::array<int, 16> intervalBuckets {};
    float velocityMin = 127.0f, velocityMax = 0.0f;

    for (size_t i = 0; i < melody.size(); ++i)
    {
        velocityMin = juce::jmin (velocityMin, (float) melody[i]->velocity);
        velocityMax = juce::jmax (velocityMax, (float) melody[i]->velocity);

        if (i > 0)
        {
            const int d = melody[i]->note - melody[i - 1]->note;
            const int ad = juce::jmin (15, std::abs (d));
            ++intervals;
            ++intervalBuckets[(size_t) ad];
            if (ad >= 4 && ad <= 12) ++usefulLeaps;
            if (i > 1)
            {
                const int prev = melody[i - 1]->note - melody[i - 2]->note;
                if (prev != 0 && d != 0 && ((prev > 0) != (d > 0)))
                    ++turnCount;
            }
        }
    }

    int distinctIntervals = 0;
    for (int v : intervalBuckets)
        if (v > 0) ++distinctIntervals;

    float intervalVariety = juce::jlimit (
        0.0f, 1.0f, (float) distinctIntervals / 5.0f);
    const float leapFit = juce::jlimit (
        0.0f, 1.0f, (float) usefulLeaps / (float) juce::jmax (1, intervals) * 3.2f);
    const float turnFit = juce::jlimit (
        0.0f, 1.0f, (float) turnCount / (float) juce::jmax (1, intervals - 1) * 1.5f);
    const float velocityShape = juce::jlimit (
        0.0f, 1.0f, (velocityMax - velocityMin) / 26.0f);

    float phraseShape = 0.5f;
    if (section.bars >= 4)
    {
        std::array<float, 4> barAvg {};
        std::array<int, 4> counts {};
        for (auto* n : melody)
        {
            const int bar = juce::jlimit (0, 3, n->step / 16);
            barAvg[(size_t) bar] += (float) n->note;
            ++counts[(size_t) bar];
        }

        if (counts[0] > 0 && counts[1] > 0 && counts[2] > 0 && counts[3] > 0)
        {
            for (int b = 0; b < 4; ++b)
                barAvg[(size_t) b] /= (float) counts[(size_t) b];

            const float peak = barAvg[2] - barAvg[0];
            const float release = barAvg[2] - barAvg[3];
            const float peakFit = juce::jlimit (0.0f, 1.0f, (peak + 1.0f) / 7.0f);
            const float releaseFit = juce::jlimit (0.0f, 1.0f, (release + 1.0f) / 7.0f);

            std::vector<int> a, ap, b, ar;
            for (auto* n : melody)
            {
                const int localBar = n->step / 16;
                if (localBar == 0) a.push_back (n->note);
                else if (localBar == 1) ap.push_back (n->note);
                else if (localBar == 2) b.push_back (n->note);
                else if (localBar == 3) ar.push_back (n->note);
            }

            float repeatWithChange = 0.45f;
            if (! a.empty() && ! ap.empty() && ! ar.empty())
            {
                const auto contourSimilarity = [] (const std::vector<int>& x,
                                                   const std::vector<int>& y)
                {
                    if (x.size() < 2 || y.size() < 2)
                        return 0.0f;
                    const size_t pairs = juce::jmin (x.size(), y.size());
                    int sameDirection = 0;
                    for (size_t i = 1; i < pairs; ++i)
                    {
                        const int dx = x[i] - x[i - 1];
                        const int dy = y[i] - y[i - 1];
                        if ((dx == 0 && dy == 0)
                            || (dx > 0 && dy > 0)
                            || (dx < 0 && dy < 0))
                            ++sameDirection;
                    }
                    return (float) sameDirection / (float) juce::jmax<size_t> (1, pairs - 1);
                };

                const float apSim = contourSimilarity (a, ap);
                const float arSim = contourSimilarity (a, ar);
                const float deltaFromCopy = 1.0f - juce::jlimit (
                    0.0f, 1.0f, std::abs (apSim - arSim) * 0.35f);
                repeatWithChange = juce::jlimit (
                    0.0f, 1.0f, 0.45f + 0.45f * 0.5f * (apSim + arSim) + 0.10f * deltaFromCopy);
            }

            phraseShape = 0.30f * peakFit
                        + 0.24f * releaseFit
                        + 0.22f * repeatWithChange
                        + 0.24f * juce::jlimit (0.0f, 1.0f, (float) usefulLeaps / (float) juce::jmax (1, intervals) * 2.7f);
        }
    }

    return juce::jlimit (
        0.0f, 1.0f,
        0.23f * intervalVariety
        + 0.20f * leapFit
        + 0.16f * turnFit
        + 0.18f * phraseShape
        + 0.13f * velocityShape
        + 0.10f * juce::jlimit (0.0f, 1.0f, (float) intervals / 12.0f));
}


void MidiForgeAudioProcessor::applyMelodicProsody (Section& section, uint32_t identity) const
{
    // 0.71 Melodic Prosody:
    // Every melody note gets a musical function before Harmony resolves its
    // exact pitch. This pass does not invent a second melody; it gives existing
    // notes intentional jobs: anchor, pickup, approach, connective motion,
    // accent, peak and release.
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.size() < 2)
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    const auto composerPlan = midiforge::ComposerGrammar::makePlan (
        section.bars, energy, complexity, melodyType, mood, genre,
        hash32 (identity ^ 0xC0719F0u));

    std::vector<int> prosodyScalePitches;
    prosodyScalePitches.reserve (56);
    const auto prosodyScale = scaleSemitones();
    for (int oct = 1; oct <= 8; ++oct)
        for (int p : prosodyScale)
        {
            const int n = 12 * oct + rootPc + p;
            if (n >= 24 && n <= 108)
                prosodyScalePitches.push_back (n);
        }
    std::sort (prosodyScalePitches.begin(), prosodyScalePitches.end());
    prosodyScalePitches.erase (
        std::unique (prosodyScalePitches.begin(), prosodyScalePitches.end()),
        prosodyScalePitches.end());

    auto shiftScale = [&] (int midi, int steps)
    {
        if (steps == 0)
            return snapToScale (midi);

        const auto& scalePitches = prosodyScalePitches;
        if (scalePitches.empty())
            return juce::jlimit (24, 108, midi);

        const int base = snapToScale (midi);
        auto it = std::lower_bound (scalePitches.begin(), scalePitches.end(), base);
        int idx = (int) std::distance (scalePitches.begin(), it);
        if (idx >= (int) scalePitches.size())
            idx = (int) scalePitches.size() - 1;
        else if (*it != base && idx > 0
                 && std::abs (scalePitches[(size_t) idx - 1] - base) <= std::abs (*it - base))
            --idx;

        idx = juce::jlimit (0, (int) scalePitches.size() - 1, idx + steps);
        return scalePitches[(size_t) idx];
    };

    for (size_t i = 0; i < melody.size(); ++i)
    {
        auto& n = section.notes[melody[i]];
        const int phrase = juce::jlimit (0, (int) composerPlan.phrases.size() - 1, n.step / 64);
        const auto composerState = composerPlan.stateFor (phrase);

        const int nextStep = (i + 1 < melody.size()) ? section.notes[melody[i + 1]].step : n.step;
        const int nextLocalStep = (i + 1 < melody.size()) ? (nextStep % 16) : -1;
        const int localStep = n.step % 16;
        const int nextInterval = (i + 1 < melody.size())
            ? section.notes[melody[i + 1]].note - n.note
            : 0;
        const bool finalOfPhrase = (i + 1 == melody.size())
            || (nextStep / 64 != n.step / 64);

        const auto intent = midiforge::MelodicProsody::classify (
            (int) i,
            (int) melody.size(),
            localStep,
            nextLocalStep,
            std::abs (nextInterval),
            finalOfPhrase,
            composerState.role,
            composerState.tension,
            hash32 (identity ^ 0x71A11CEu));

        switch (intent.role)
        {
            case midiforge::MelodicProsody::Anchor:
                n.velocity = juce::jlimit (35, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmin (6, n.length + 1);
                break;

            case midiforge::MelodicProsody::Accent:
                n.velocity = juce::jlimit (35, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                break;

            case midiforge::MelodicProsody::Pickup:
                n.velocity = juce::jlimit (35, 118,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmax (1, n.length - 1);
                break;

            case midiforge::MelodicProsody::Approach:
                if (i + 1 < melody.size() && std::abs (nextInterval) >= 4)
                    n.note = shiftScale (n.note,
                        nextInterval > 0 ? intent.scaleMotion : -intent.scaleMotion);
                n.length = juce::jmax (1, n.length);
                break;

            case midiforge::MelodicProsody::Connect:
                if (i + 1 < melody.size() && std::abs (nextInterval) >= 8)
                    n.note = shiftScale (n.note,
                        nextInterval > 0 ? 1 : -1);
                break;

            case midiforge::MelodicProsody::Peak:
                n.note = shiftScale (n.note, juce::jlimit (1, 2,
                    1 + (composerState.registerLift > 4.0f ? 1 : 0)));
                n.velocity = juce::jlimit (40, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmax (1, n.length - 1);
                break;

            case midiforge::MelodicProsody::Release:
                if (i > 0)
                {
                    const int previous = section.notes[melody[i - 1]].note;
                    if (std::abs (n.note - previous) >= 2)
                        n.note = shiftScale (n.note, n.note > previous ? -1 : 1);
                    else if (n.note > previous)
                        n.note = shiftScale (n.note, -1);
                }
                n.velocity = juce::jlimit (35, 118,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmin (6, n.length + 1);
                break;
        }

        if (intent.sustainBias > 0.04f && (intent.role == midiforge::MelodicProsody::Anchor
                                         || intent.role == midiforge::MelodicProsody::Release))
            n.length = juce::jmin (6, n.length + 1);
    }

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}

float MidiForgeAudioProcessor::melodicProsodyScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 4)
        return 0.52f;

    std::vector<const NoteEvent*> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    if (melody.size() < 3)
        return 0.45f;

    const auto composerPlan = midiforge::ComposerGrammar::makePlan (
        section.bars, energy, complexity, melodyType, mood, genre,
        hash32 (generationSeed ^ 0xC0719F0u));

    int approachGood = 0, approachCount = 0;
    int releaseGood = 0, releaseCount = 0;
    int peakGood = 0, peakCount = 0;
    int anchorGood = 0, anchorCount = 0;
    int accentGood = 0, accentCount = 0;

    for (size_t i = 0; i < melody.size(); ++i)
    {
        const auto* cur = melody[i];
        const int phrase = juce::jlimit (0, (int) composerPlan.phrases.size() - 1, cur->step / 64);
        const auto state = composerPlan.stateFor (phrase);
        const int nextStep = i + 1 < melody.size() ? melody[i + 1]->step : cur->step;
        const int nextInterval = i + 1 < melody.size()
            ? melody[i + 1]->note - cur->note : 0;
        const bool finalOfPhrase = i + 1 == melody.size()
            || nextStep / 64 != cur->step / 64;

        const auto intent = midiforge::MelodicProsody::classify (
            (int) i, (int) melody.size(), cur->step % 16,
            i + 1 < melody.size() ? nextStep % 16 : -1,
            std::abs (nextInterval),
            finalOfPhrase,
            state.role,
            state.tension,
            hash32 (generationSeed ^ 0x71A11CEu));

        const int local = cur->step % 16;
        switch (intent.role)
        {
            case midiforge::MelodicProsody::Approach:
                ++approachCount;
                if (i + 1 < melody.size())
                {
                    const int after = melody[i + 1]->note - cur->note;
                    if (std::abs (after) <= 7)
                        ++approachGood;
                }
                break;

            case midiforge::MelodicProsody::Release:
                ++releaseCount;
                if ((i == 0 || melody[i - 1]->note >= cur->note)
                    && cur->length >= 2)
                    ++releaseGood;
                break;

            case midiforge::MelodicProsody::Peak:
            {
                ++peakCount;
                int phraseMax = cur->note;
                const int phraseStart = (cur->step / 64) * 64;
                for (const auto* n : melody)
                    if (n->step >= phraseStart && n->step < phraseStart + 64)
                        phraseMax = std::max (phraseMax, n->note);
                if (cur->note >= phraseMax - 1)
                    ++peakGood;
                break;
            }

            case midiforge::MelodicProsody::Anchor:
                ++anchorCount;
                if (cur->velocity >= 76)
                    ++anchorGood;
                break;

            case midiforge::MelodicProsody::Accent:
                ++accentCount;
                if (cur->velocity >= 72)
                    ++accentGood;
                break;

            default:
                break;
        }

        juce::ignoreUnused (local);
    }

    const float approachFit = approachCount > 0
        ? (float) approachGood / (float) approachCount : 0.58f;
    const float releaseFit = releaseCount > 0
        ? (float) releaseGood / (float) releaseCount : 0.58f;
    const float peakFit = peakCount > 0
        ? (float) peakGood / (float) peakCount : 0.56f;
    const float anchorFit = anchorCount > 0
        ? (float) anchorGood / (float) anchorCount : 0.56f;
    const float accentFit = accentCount > 0
        ? (float) accentGood / (float) accentCount : 0.56f;

    return juce::jlimit (0.0f, 1.0f,
        0.30f * approachFit
        + 0.22f * releaseFit
        + 0.22f * peakFit
        + 0.14f * anchorFit
        + 0.12f * accentFit);
}

void MidiForgeAudioProcessor::applyHarmonicIntelligence (Section& section, uint32_t identity) const
{
    // 0.68 Harmonic Intelligence 2.0:
    // Harmony provides destinations and voice-leading gravity, but it does not
    // flatten the melody into an arpeggio. Strong/long notes prefer the active
    // chord, weak notes may remain as passing/color tones, and late-bar notes
    // can anticipate the next chord.
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    const auto prog = progressionDegrees();
    if (prog.empty())
        return;

    auto pitchClass = [] (int n) { return (n % 12 + 12) % 12; };

    auto chordPcsForBar = [&] (int bar)
    {
        std::array<int, 12> pcs {};
        int count = 0;

        for (const auto& n : section.notes)
        {
            if (n.channel != 1 || n.step / 16 != bar)
                continue;

            const int pc = pitchClass (n.note);
            if (pcs[(size_t) pc] == 0)
            {
                pcs[(size_t) pc] = 1;
                ++count;
            }
        }

        if (count == 0)
        {
            const int degree = prog[(size_t) (bar % (int) prog.size())];
            for (int offset : { 0, 2, 4 })
            {
                const int pc = pitchClass (degreeToPitch (degree + offset, octave));
                if (pcs[(size_t) pc] == 0)
                {
                    pcs[(size_t) pc] = 1;
                    ++count;
                }
            }
        }

        return pcs;
    };

    auto nearestChordPitch = [&] (int bar, int pitch, bool preferStable) -> int
    {
        const auto pcs = chordPcsForBar (bar);
        const int degree = prog[(size_t) (bar % (int) prog.size())];
        const int rootPcForBar = pitchClass (degreeToPitch (degree, octave));
        const int thirdPcForBar = pitchClass (degreeToPitch (degree + 2, octave));
        const int fifthPcForBar = pitchClass (degreeToPitch (degree + 4, octave));

        int best = pitch;
        float bestScore = 1.0e9f;

        for (int oct = -3; oct <= 3; ++oct)
        {
            const int baseOct = ((pitch / 12) * 12) + (oct * 12);
            for (int pc = 0; pc < 12; ++pc)
            {
                if (pcs[(size_t) pc] == 0)
                    continue;

                const int candidate = baseOct + pc;
                if (candidate < 34 || candidate > 108)
                    continue;

                float score = (float) std::abs (candidate - pitch);

                // On a bar anchor, roots are useful. Everywhere else, thirds and
                // fifths are slightly preferred so the melody does not become
                // a root-note machine.
                if (preferStable)
                {
                    if (pc == rootPcForBar) score -= 0.85f;
                    if (pc == thirdPcForBar) score -= 1.15f;
                    if (pc == fifthPcForBar) score -= 0.45f;
                }
                else
                {
                    if (pc == thirdPcForBar) score -= 1.55f;
                    if (pc == fifthPcForBar) score -= 0.80f;
                    if (pc == rootPcForBar) score += 0.80f;
                }

                if (score < bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
        }

        return best;
    };

    auto isChordTone = [&] (int bar, int pitch)
    {
        const auto pcs = chordPcsForBar (bar);
        return pcs[(size_t) pitchClass (pitch)] != 0;
    };

    std::vector<size_t> melody;
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            return section.notes[a].step < section.notes[b].step;
        });

    if (melody.size() < 2)
        return;

    for (size_t k = 0; k < melody.size(); ++k)
    {
        auto& n = section.notes[melody[k]];
        const int bar = juce::jlimit (0, juce::jmax (0, section.bars - 1), n.step / 16);
        const int local = n.step % 16;
        const bool strong = (local % 4) == 0;
        const bool longNote = n.length >= 3;
        const bool late = local >= 12;
        const bool firstOfBar = (k == 0 || section.notes[melody[k - 1]].step / 16 != bar);

        float targetWeight = 0.0f;
        if (strong)
            targetWeight = firstOfBar ? 0.68f : 0.46f;
        else if (longNote)
            targetWeight = 0.34f;
        else
            targetWeight = 0.10f;

        // Keep some harmonic tension in the B bar and on deliberately weak events.
        if ((bar & 3) == 2 && !firstOfBar && !longNote)
            targetWeight *= 0.72f;
        if (complexity > 0.70f && !strong)
            targetWeight *= 0.72f;

        int target = nearestChordPitch (bar, n.note, firstOfBar || strong);

        // Late notes are allowed to point into the next harmony. This is the
        // main anticipation mechanism: the note can remain slightly tense while
        // its destination is clearly implied before the bar changes.
        if (late && bar + 1 < section.bars)
        {
            const int nextTarget = nearestChordPitch (bar + 1, n.note, false);
            const float anticipation = ((bar & 3) == 3) ? 0.78f : 0.56f;
            const int blend = juce::roundToInt (
                (1.0f - anticipation) * (float) target
                + anticipation * (float) nextTarget);
            target = juce::jlimit (34, 108, blend);
            targetWeight = juce::jmax (targetWeight, 0.24f);
        }

        // At a phrase seam, prioritize a smooth destination instead of a random
        // octave jump between bars.
        if (firstOfBar && k > 0)
        {
            const int previous = section.notes[melody[k - 1]].note;
            const int voiceTarget = nearestChordPitch (bar, previous, true);
            target = juce::roundToInt (
                0.48f * (float) target
                + 0.52f * (float) voiceTarget);
            targetWeight = juce::jmax (targetWeight, 0.42f);
        }

        // Only rewrite the note when the destination is musically needed. This
        // preserves color/passing tones instead of erasing the expressive layer.
        if (!isChordTone (bar, n.note) || strong || longNote || late)
        {
            const int blended = juce::roundToInt (
                (1.0f - targetWeight) * (float) n.note
                + targetWeight * (float) target);
            n.note = foldIntoLane (snapToScale (blended), 34, 108);
        }

        // A non-chord tone followed quickly by a chord tone is a useful passing
        // gesture. Make the following destination a little more intentional.
        if (k + 1 < melody.size())
        {
            auto& next = section.notes[melody[k + 1]];
            const int nextBar = next.step / 16;
            if (nextBar == bar && next.step - n.step <= 3 && !isChordTone (bar, n.note)
                && !isChordTone (bar, next.note))
            {
                const int resolution = nearestChordPitch (bar, next.note, false);
                next.note = foldIntoLane (
                    snapToScale (juce::roundToInt (
                        0.35f * (float) next.note + 0.65f * (float) resolution)),
                    34, 108);
            }
        }

        // Slight dynamic separation: harmonic anchors speak more clearly than
        // passing tones, while anticipation stays audible but does not dominate.
        if (strong)
            n.velocity = juce::jlimit (42, 118, n.velocity + 3);
        else if (late && bar + 1 < section.bars)
            n.velocity = juce::jlimit (42, 118, n.velocity + 1);
        else if (!isChordTone (bar, n.note))
            n.velocity = juce::jlimit (42, 118, n.velocity - 1);
    }

    removeDuplicateNotes (section.notes);
    cleanMelodyLine (section.notes);
}

float MidiForgeAudioProcessor::harmonicIntelligenceScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine)
        return 1.0f;

    const auto prog = progressionDegrees();
    if (prog.empty())
        return 0.55f;

    auto pc = [] (int n) { return (n % 12 + 12) % 12; };
    auto isChordToneAt = [&] (int bar, int pitch)
    {
        bool found = false;
        for (const auto& n : section.notes)
        {
            if (n.channel == 1 && n.step / 16 == bar && pc (n.note) == pc (pitch))
            {
                found = true;
                break;
            }
        }
        if (found) return true;

        const int degree = prog[(size_t) (bar % (int) prog.size())];
        for (int offset : { 0, 2, 4 })
            if (pc (degreeToPitch (degree + offset, octave)) == pc (pitch))
                return true;
        return false;
    };

    std::vector<const NoteEvent*> mel;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            mel.push_back (&n);
    std::stable_sort (mel.begin(), mel.end(),
        [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

    if (mel.size() < 2)
        return 0.45f;

    int anchors = 0, anchorHits = 0, anticipations = 0, anticipationHits = 0;
    int resolutions = 0, resolved = 0, transitions = 0, smooth = 0;
    int chordToneCount = 0;

    for (size_t i = 0; i < mel.size(); ++i)
    {
        const auto* n = mel[i];
        const int bar = n->step / 16;
        const int local = n->step % 16;
        const bool anchor = (local % 4) == 0 || n->length >= 3;
        const bool late = local >= 12 && bar + 1 < section.bars;

        if (anchor)
        {
            ++anchors;
            if (isChordToneAt (bar, n->note)) ++anchorHits;
        }
        if (isChordToneAt (bar, n->note))
            ++chordToneCount;

        if (late)
        {
            ++anticipations;
            bool pointsForward = false;
            if (i + 1 < mel.size())
            {
                const int nextBar = mel[i + 1]->step / 16;
                if (nextBar > bar || mel[i + 1]->step - n->step >= 1)
                    pointsForward = isChordToneAt (bar + 1, mel[i + 1]->note);
            }
            if (pointsForward) ++anticipationHits;
        }

        if (i + 1 < mel.size())
        {
            const auto* next = mel[i + 1];
            if (next->step - n->step <= 4 && !isChordToneAt (bar, n->note))
            {
                ++resolutions;
                if (isChordToneAt (next->step / 16, next->note))
                    ++resolved;
            }

            if (next->step / 16 != bar)
            {
                ++transitions;
                if (std::abs (next->note - n->note) <= 7)
                    ++smooth;
            }
        }
    }

    const float anchorFit = anchors > 0 ? (float) anchorHits / (float) anchors : 0.55f;
    const float anticipationFit = anticipations > 0
        ? (float) anticipationHits / (float) anticipations : 0.55f;
    const float resolutionFit = resolutions > 0
        ? (float) resolved / (float) resolutions : 0.55f;
    const float voiceFit = transitions > 0
        ? (float) smooth / (float) transitions : 0.60f;

    const float chordRatio = (float) chordToneCount / (float) mel.size();
    // Reward a useful harmonic backbone, but penalize an arpeggio-like wall of
    // chord tones. Expressive melodies usually mix anchors with color tones.
    const float distributionFit = 1.0f
        - juce::jlimit (0.0f, 1.0f, std::abs (chordRatio - 0.58f) / 0.38f);

    std::array<int, 12> used {};
    int uniqueChordTones = 0;
    for (const auto* n : mel)
    {
        const int bar = n->step / 16;
        if (!isChordToneAt (bar, n->note))
            continue;
        const int key = pc (n->note);
        if (used[(size_t) key] == 0)
        {
            used[(size_t) key] = 1;
            ++uniqueChordTones;
        }
    }
    const float diversityFit = juce::jlimit (0.0f, 1.0f, (float) uniqueChordTones / 3.0f);

    return juce::jlimit (
        0.0f, 1.0f,
        0.22f * anchorFit
        + 0.17f * resolutionFit
        + 0.17f * anticipationFit
        + 0.18f * voiceFit
        + 0.16f * distributionFit
        + 0.10f * diversityFit);
}


void MidiForgeAudioProcessor::applyPhraseMemory4 (Section& section, uint32_t identity) const
{
    // 0.69 Phrase Memory 4.0:
    // Existing motif/development logic works inside a four-bar phrase. This
    // layer gives the generator a longer memory: phrase 1 establishes a macro
    // idea, later four-bar cells re-state, invert, fragment or return to it.
    // Rhythm is deliberately preserved; Harmony runs afterwards and resolves
    // the transformed contour against the destination chords.
    if (section.notes.empty() || section.bars < 8
        || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    auto collectBar = [&] (int bar)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < section.notes.size(); ++i)
            if (section.notes[i].channel == 3 && section.notes[i].step / 16 == bar)
                out.push_back (i);

        std::stable_sort (out.begin(), out.end(),
            [&] (size_t a, size_t b)
            {
                return section.notes[a].step < section.notes[b].step;
            });
        return out;
    };

    std::array<std::vector<size_t>, 4> memoryBars;
    for (int bar = 0; bar < 4; ++bar)
        memoryBars[(size_t) bar] = collectBar (bar);

    for (const auto& v : memoryBars)
        if (v.size() < 2)
            return;

    auto contourSimilarity = [&] (const std::vector<size_t>& reference,
                                   const std::vector<size_t>& current,
                                   bool inverted)
    {
        if (reference.size() < 2 || current.size() < 2)
            return 0.0f;

        const size_t pairs = juce::jmin (reference.size(), current.size());
        int matches = 0;
        for (size_t i = 1; i < pairs; ++i)
        {
            const int refDelta = section.notes[reference[i]].note
                               - section.notes[reference[i - 1]].note;
            const int curDelta = section.notes[current[i]].note
                               - section.notes[current[i - 1]].note;
            const bool same = inverted ? ((refDelta > 0 && curDelta < 0)
                                       || (refDelta < 0 && curDelta > 0)
                                       || (refDelta == 0 && curDelta == 0))
                                       : ((refDelta > 0 && curDelta > 0)
                                       || (refDelta < 0 && curDelta < 0)
                                       || (refDelta == 0 && curDelta == 0));
            if (same) ++matches;
        }
        return (float) matches / (float) juce::jmax<size_t> (1, pairs - 1);
    };

    auto rhythmSimilarity = [&] (const std::vector<size_t>& reference,
                                 const std::vector<size_t>& current)
    {
        if (reference.empty() || current.empty())
            return 0.0f;

        const size_t pairs = juce::jmin (reference.size(), current.size());
        int hits = 0;
        for (size_t i = 0; i < pairs; ++i)
            if (std::abs ((section.notes[reference[i]].step % 16)
                        - (section.notes[current[i]].step % 16)) <= 1)
                ++hits;

        const float hitFit = (float) hits / (float) juce::jmax (reference.size(), current.size());
        const float countFit = 1.0f - juce::jlimit (
            0.0f, 1.0f,
            (float) std::abs ((int) reference.size() - (int) current.size()) / 4.0f);
        return juce::jlimit (0.0f, 1.0f, 0.74f * hitFit + 0.26f * countFit);
    };

    auto mix32 = [] (uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };

    const int phraseCount = section.bars / 4;
    for (int phrase = 1; phrase < phraseCount; ++phrase)
    {
        std::array<std::vector<size_t>, 4> currentBars;
        for (int localBar = 0; localBar < 4; ++localBar)
            currentBars[(size_t) localBar] = collectBar (phrase * 4 + localBar);

        const uint32_t phraseSeed = mix32 (
            identity ^ (uint32_t) (phrase + 1) * 0x9e3779b9u ^ 0x4A3F19C7u);
        const auto composerPlan = midiforge::ComposerGrammar::makePlan (
        section.bars, energy, complexity, melodyType, mood, genre,
        hash32 (identity ^ 0xC0A70970u));
    const auto composerState = composerPlan.stateFor (phrase);

    int mode = 0; // 0 return/restatement, 1 development, 2 contrast, 3 build/fragment
    switch (composerState.role)
    {
        case midiforge::ComposerGrammar::Develop:  mode = 1; break;
        case midiforge::ComposerGrammar::Build:    mode = 3; break;
        case midiforge::ComposerGrammar::Contrast:
        case midiforge::ComposerGrammar::Peak:     mode = 2; break;
        case midiforge::ComposerGrammar::Release:
        case midiforge::ComposerGrammar::Return:   mode = 0; break;
        case midiforge::ComposerGrammar::Statement:
        default:                                    mode = 1; break;
    }
        const bool inverted = mode == 2;

        float overallSimilarity = 0.0f;
        int comparableBars = 0;
        for (int localBar = 0; localBar < 4; ++localBar)
        {
            if (memoryBars[(size_t) localBar].size() < 2
                || currentBars[(size_t) localBar].size() < 2)
                continue;

            const auto& ref = memoryBars[(size_t) localBar];
            const auto& cur = currentBars[(size_t) localBar];
            const float direct = contourSimilarity (ref, cur, false);
            const float inverse = contourSimilarity (ref, cur, true);
            const float rhythmFit = rhythmSimilarity (ref, cur);
            const float shapeFit = inverted ? inverse : juce::jmax (direct, inverse * 0.72f);
            overallSimilarity += 0.58f * shapeFit + 0.42f * rhythmFit;
            ++comparableBars;
        }

        if (comparableBars == 0)
            continue;

        overallSimilarity /= (float) comparableBars;

        // When the current phrase has already retained the idea, touch it lightly.
        // When it has drifted, pull it back toward the long-term phrase fingerprint.
        const float memoryStrength = juce::jlimit (
            0.18f, 0.74f,
            0.24f + 0.78f * juce::jmax (0.0f, 0.64f - overallSimilarity));

        for (int localBar = 0; localBar < 4; ++localBar)
        {
            auto& cur = currentBars[(size_t) localBar];
            const auto& ref = memoryBars[(size_t) localBar];
            if (cur.empty() || ref.empty())
                continue;

            const int currentAnchor = section.notes[cur.front()].note;
            const int referenceAnchor = section.notes[ref.front()].note;

            for (size_t i = 0; i < cur.size(); ++i)
            {
                auto& n = section.notes[cur[i]];

                const size_t ri = ref.size() <= 1
                    ? 0
                    : (size_t) juce::jlimit (
                        0,
                        (int) ref.size() - 1,
                        juce::roundToInt (
                            (double) i * (double) (ref.size() - 1)
                            / (double) juce::jmax<size_t> (1, cur.size() - 1)));

                const int referenceRelative =
                    section.notes[ref[ri]].note - referenceAnchor;
                const int currentRelative = n.note - currentAnchor;

                float targetRelative = (float) referenceRelative;
                if (mode == 2)
                {
                    targetRelative = -(float) referenceRelative;
                }
                else if (mode == 3)
                {
                    const float retain = i < cur.size() / 2 ? 0.82f : 0.36f;
                    targetRelative = retain * (float) referenceRelative
                                   + (1.0f - retain) * (float) currentRelative;
                }
                else if (mode == 0)
                {
                    targetRelative = 0.90f * (float) referenceRelative
                                   + 0.10f * (float) currentRelative;
                }

                // Small deterministic variation keeps memory alive without making
                // every later phrase a duplicate.
                const uint32_t h = mix32 (
                    phraseSeed ^ (uint32_t) (localBar * 97 + i * 31 + 7));
                int variation = 0;
                if ((h % 100u) < 28u)
                    variation = ((h & 1u) != 0u) ? 1 : -1;

                if (mode == 3 && i == cur.size() / 2)
                    variation += 2 + (int) ((h >> 8) & 1u);

                float strength = memoryStrength;
                if (i == 0)
                    strength *= 0.42f; // do not erase local harmonic anchors
                if (mode == 2)
                    strength = juce::jlimit (0.68f, 0.92f, strength + 0.18f);
                if (mode == 3 && i + 1 == cur.size())
                    strength *= 0.60f;

                // Contrast is not merely "different". It is an intentional
                // reversal of the reference contour with an asymmetric offset,
                // so the middle phrase reads as an answer rather than another
                // copy wearing a different register.
                int contrastOffset = 0;
                if (mode == 2 && cur.size() >= 3)
                {
                    const float t = (float) i / (float) juce::jmax<size_t> (1, cur.size() - 1);
                    contrastOffset = (t < 0.45f) ? 2 : (t > 0.70f ? -2 : 3);
                    if (((phraseSeed >> ((i % 4) * 5)) & 1u) != 0u)
                        contrastOffset = -contrastOffset;
                }

                const int desired = currentAnchor
                                  + juce::roundToInt (targetRelative)
                                  + variation
                                  + contrastOffset;

                n.note = juce::jlimit (34, 108,
                    snapToScale (juce::roundToInt (
                        (1.0f - strength) * (float) n.note
                        + strength * (float) desired)));

                // The contrasting phrase receives a small register lift around its
                // centre; the return phrase deliberately avoids keeping that lift.
                if (mode == 2 && i == cur.size() / 2)
                    n.note = juce::jlimit (34, 108,
                        snapToScale (n.note + 4));
                else if (mode == 0 && i == cur.size() / 2 && phrase > 1)
                    n.note = juce::jlimit (34, 108,
                        snapToScale (n.note - 1));
            }
        }
    }

    removeDuplicateNotes (section.notes);
    cleanMelodyLine (section.notes);
}

float MidiForgeAudioProcessor::phraseMemory4Score (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 8)
        return 0.52f;

    auto collectBar = [&] (int bar)
    {
        std::vector<const NoteEvent*> out;
        for (const auto& n : section.notes)
            if (n.channel == 3 && n.step / 16 == bar)
                out.push_back (&n);

        std::stable_sort (out.begin(), out.end(),
            [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });
        return out;
    };

    std::array<std::vector<const NoteEvent*>, 4> reference;
    for (int b = 0; b < 4; ++b)
        reference[(size_t) b] = collectBar (b);

    for (const auto& v : reference)
        if (v.size() < 2)
            return 0.45f;

    auto contourFit = [] (const std::vector<const NoteEvent*>& a,
                          const std::vector<const NoteEvent*>& b,
                          bool inverted)
    {
        if (a.size() < 2 || b.size() < 2)
            return 0.0f;

        const size_t n = juce::jmin (a.size(), b.size());
        int hits = 0;
        for (size_t i = 1; i < n; ++i)
        {
            const int da = a[i]->note - a[i - 1]->note;
            const int db = b[i]->note - b[i - 1]->note;
            const bool same = inverted ? ((da > 0 && db < 0)
                                       || (da < 0 && db > 0)
                                       || (da == 0 && db == 0))
                                       : ((da > 0 && db > 0)
                                       || (da < 0 && db < 0)
                                       || (da == 0 && db == 0));
            if (same) ++hits;
        }
        return (float) hits / (float) juce::jmax<size_t> (1, n - 1);
    };

    auto rhythmFit = [] (const std::vector<const NoteEvent*>& a,
                         const std::vector<const NoteEvent*>& b)
    {
        if (a.empty() || b.empty()) return 0.0f;
        const size_t n = juce::jmin (a.size(), b.size());
        int hits = 0;
        for (size_t i = 0; i < n; ++i)
            if (std::abs ((a[i]->step % 16) - (b[i]->step % 16)) <= 1)
                ++hits;
        return (float) hits / (float) juce::jmax (a.size(), b.size());
    };

    float scoreSum = 0.0f;
    int count = 0;
    for (int phrase = 1; phrase < section.bars / 4; ++phrase)
    {
        float phraseShape = 0.0f;
        float phraseRhythm = 0.0f;
        int barsCompared = 0;

        for (int localBar = 0; localBar < 4; ++localBar)
        {
            const auto cur = collectBar (phrase * 4 + localBar);
            if (cur.size() < 2)
                continue;

            const float direct = contourFit (reference[(size_t) localBar], cur, false);
            const float inverse = contourFit (reference[(size_t) localBar], cur, true);
            const auto composerPlan = midiforge::ComposerGrammar::makePlan (
                section.bars, energy, complexity, melodyType, mood, genre,
                hash32 (generationSeed ^ 0xC0A70970u));
            const auto composerState = composerPlan.stateFor (phrase);
            const bool expectedInverse =
                composerState.role == midiforge::ComposerGrammar::Contrast
                || composerState.role == midiforge::ComposerGrammar::Peak;

            phraseShape += expectedInverse ? juce::jmax (inverse, direct * 0.68f)
                                           : juce::jmax (direct, inverse * 0.72f);
            phraseRhythm += rhythmFit (reference[(size_t) localBar], cur);
            ++barsCompared;
        }

        if (barsCompared > 0)
        {
            phraseShape /= (float) barsCompared;
            phraseRhythm /= (float) barsCompared;

            float variation = 0.52f;
            if (phrase >= 1)
            {
                int literal = 0;
                for (int localBar = 0; localBar < 4; ++localBar)
                {
                    const auto cur = collectBar (phrase * 4 + localBar);
                    if (cur.size() == reference[(size_t) localBar].size()
                        && rhythmFit (reference[(size_t) localBar], cur) > 0.92f
                        && contourFit (reference[(size_t) localBar], cur, false) > 0.95f)
                        ++literal;
                }
                variation = 1.0f - juce::jlimit (0.0f, 1.0f, (float) literal / 4.0f);
            }

            scoreSum += 0.52f * phraseShape
                      + 0.28f * phraseRhythm
                      + 0.20f * variation;
            ++count;
        }
    }

    return count > 0
        ? juce::jlimit (0.0f, 1.0f, scoreSum / (float) count)
        : 0.50f;
}

float MidiForgeAudioProcessor::composerGrammarScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 4)
        return 0.52f;

    const auto plan = midiforge::ComposerGrammar::makePlan (
        section.bars, energy, complexity, melodyType, mood, genre,
        hash32 (generationSeed ^ 0xC0A70970u));

    struct PhraseObs
    {
        int count = 0;
        float meanPitch = 60.0f;
        float meanVelocity = 80.0f;
    };

    std::vector<PhraseObs> observed (plan.phrases.size());

    for (const auto& n : section.notes)
    {
        if (n.channel != 3)
            continue;

        const int phrase = juce::jlimit (0, (int) observed.size() - 1, n.step / 64);
        auto& o = observed[(size_t) phrase];
        const float w = (float) o.count;
        o.meanPitch = (o.meanPitch * w + (float) n.note) / (w + 1.0f);
        o.meanVelocity = (o.meanVelocity * w + (float) n.velocity) / (w + 1.0f);
        ++o.count;
    }

    const float basePitch = observed.front().meanPitch;
    const float baseVelocity = observed.front().meanVelocity;

    float total = 0.0f;
    int used = 0;

    for (size_t i = 0; i < observed.size(); ++i)
    {
        const auto& o = observed[i];
        if (o.count < 2)
            continue;

        const auto& target = plan.phrases[i];
        const float density = juce::jlimit (0.0f, 1.0f,
            (float) o.count / 24.0f);
        const float densityFit = 1.0f
            - juce::jlimit (0.0f, 1.0f,
                std::abs (density - target.density) / 0.46f);

        const float registerLift = o.meanPitch - basePitch;
        const float registerFit = 1.0f
            - juce::jlimit (0.0f, 1.0f,
                std::abs (registerLift - target.registerLift) / 8.0f);

        const float velocityLift = (o.meanVelocity - baseVelocity) / 78.0f;
        const float velocityFit = 1.0f
            - juce::jlimit (0.0f, 1.0f,
                std::abs (velocityLift - target.velocityLift) / 0.16f);

        const float observedTension = juce::jlimit (0.0f, 1.0f,
            0.46f * density
            + 0.34f * juce::jlimit (0.0f, 1.0f, 0.50f + registerLift / 12.0f)
            + 0.20f * juce::jlimit (0.0f, 1.0f, 0.50f + velocityLift));

        const float tensionFit = 1.0f
            - juce::jlimit (0.0f, 1.0f,
                std::abs (observedTension - target.tension) / 0.62f);

        total += 0.32f * densityFit
               + 0.30f * registerFit
               + 0.18f * velocityFit
               + 0.20f * tensionFit;
        ++used;

        if (i > 0 && observed[i - 1].count >= 2)
        {
            const float delta = o.meanPitch - observed[i - 1].meanPitch;
            const float expectedDelta =
                target.registerLift - plan.phrases[i - 1].registerLift;
            const bool expectedUp = expectedDelta > 0.6f;
            const bool expectedDown = expectedDelta < -0.6f;
            const bool observedUp = delta > 0.7f;
            const bool observedDown = delta < -0.7f;
            total += ((!expectedUp && !expectedDown)
                      || (expectedUp && observedUp)
                      || (expectedDown && observedDown))
                ? 0.12f
                : 0.035f;
        }
    }

    if (! observed.empty() && observed.back().count >= 2)
    {
        const float returnDistance = std::abs (observed.back().meanPitch - basePitch);
        const float returnFit = 1.0f
            - juce::jlimit (0.0f, 1.0f, returnDistance / 12.0f);

        const int lastBar = juce::jmax (0, section.bars - 1);
        const auto prog = progressionDegrees();
        const int activeDegree = prog.empty()
            ? 0
            : prog[(size_t) (lastBar % (int) prog.size())];
        const int targets[3] =
        {
            degreeToPitch (activeDegree, octave),
            degreeToPitch (activeDegree + 2, octave),
            degreeToPitch (activeDegree + 4, octave)
        };

        float nearest = 1000.0f;
        for (const int p : targets)
            nearest = juce::jmin (nearest, std::abs (observed.back().meanPitch - (float) p));

        const float cadenceFit = 1.0f
            - juce::jlimit (0.0f, 1.0f, nearest / 9.0f);
        total += 0.42f * returnFit + 0.18f * cadenceFit;
    }

    return used > 0
        ? juce::jlimit (0.0f, 1.0f, total / (float) used)
        : 0.50f;
}

float MidiForgeAudioProcessor::rhythmGrammarScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine)
        return 1.0f;

    std::vector<midiforge::RhythmGrammar::Note> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back ({ n.step, n.length, n.velocity });

    return midiforge::RhythmGrammar::score (
        melody,
        section.bars,
        juce::jlimit (40.0, 240.0, currentBpm.load()),
        rhythm,
        complexity,
        energy);
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
    logFeedback (vi, "like");
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
    logFeedback (vi, "dislike");
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

bool MidiForgeAudioProcessor::noteKeptVariation()
{
    // Implicit drag/export learning was removed. Explicit LIKE / DISLIKE remains
    // the only way user feedback trains Taste ML.
    return false;
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

void MidiForgeAudioProcessor::logFeedback (int vi, const char* verdict) const
{
    static constexpr const char* archetypes[8] = { "HOOK", "GROOVE", "HARMONY", "MOTIF", "MINIMAL", "WEIRD", "EMOTIONAL", "WILDCARD" };
    static constexpr const char* transforms[9] = { "ORIGINAL", "TIGHT", "SPARSE", "DARK", "BIGGER", "WEIRD", "TIGHT+WEIRD", "SPARSE+DARK", "SIMILAR" };
    static constexpr const char* genres[16] = { "Universal", "Trap", "House", "Techno", "BoomBap", "Ambient", "Cinematic", "RnB",
                                                "Pop", "Drill", "DnB", "Jersey", "Afro", "Hyperpop", "Experimental", "Lofi" };
    static constexpr const char* scales[12] = { "Major", "Minor", "Dorian", "Phrygian", "HarmonicMinor", "MelodicMinor", "Pentatonic", "Lydian", "Mixolydian", "Locrian", "HarmonicMajor", "Blues" };
    static constexpr const char* sounds[8] = { "Piano", "Pluck", "SynthLead", "Bell", "Pad", "Brass", "808", "Guitar" };
    static constexpr const char* moods[9] = { "Neutral", "Dark", "Melancholic", "Euphoric", "Aggressive", "Dreamy", "Nostalgic", "Mysterious", "Energetic" };
    static constexpr const char* melodyTypes[8] = { "Hook", "VocalLike", "Riff", "Ostinato", "Arp", "Counter", "SparseLead", "Phrase" };
    auto nameOf = [] (const char* const* table, int n, int i) { return juce::String (i >= 0 && i < n ? table[i] : "?"); };

    int slot = 0, archetype = -1, transform = -1, melodyNotes = 0, totalNotes = 0, loopBars = bars;
    {
        const juce::ScopedLock sl (variationsLock);
        if (variations.empty()) return;
        slot = juce::jlimit (0, (int) variations.size() - 1, vi < 0 ? selectedVariation : vi);
        const auto& v = variations[(size_t) slot];
        archetype = v.sourceArchetype;
        transform = v.transformMode;
        loopBars = v.bars;
        totalNotes = (int) v.notes.size();
        for (const auto& n : v.notes) if (n.channel == 3) ++melodyNotes;
    }

    juce::StringArray row;
    row.add (juce::Time::getCurrentTime().toISO8601 (false));
    row.add (kMidiForgeEngineVersion);
    row.add (verdict);
    row.add (juce::String (slot + 1));
    row.add (nameOf (transforms, 9, transform));
    row.add (nameOf (archetypes, 8, archetype));
    row.add (nameOf (genres, 16, genre));
    row.add (nameOf (moods, 9, mood));
    row.add (nameOf (melodyTypes, 8, melodyType));
    row.add (nameOf (sounds, 8, soundTarget));
    row.add (juce::String (era));
    row.add (nameOf (scales, 12, scale));
    row.add (juce::String (progression));
    row.add (juce::String (loopBars));
    row.add (juce::String (currentBpm.load(), 1));
    row.add (juce::String (complexity, 2));
    row.add (juce::String (energy, 2));
    row.add (juce::String (melodyDensity, 2));
    row.add (juce::String (melodyNotes));
    row.add (juce::String (totalNotes));
    row.add (juce::String ((juce::int64) generationSeed));
    row.add (juce::String ((juce::int64) magicDnaSeed));

    const juce::ScopedLock fl (feedbackLock);
    if (feedbackFile == juce::File()) return;
    feedbackFile.getParentDirectory().createDirectory();
    const bool needHeader = ! feedbackFile.existsAsFile() || feedbackFile.getSize() == 0;
    juce::FileOutputStream out (feedbackFile);
    if (! out.openedOk()) return;
    out.setPosition (feedbackFile.getSize());
    if (needHeader)
        out << "time_utc,engine,verdict,slot,transform,archetype,genre,mood,melody_type,sound,era,scale,progression,bars,bpm,complexity,energy,melody_density,melody_notes,total_notes,generation_seed,dna_seed\n";
    out << row.joinIntoString (",") << "\n";
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
    o->setProperty("tasteMLVersion", 1);
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
        const int tasteVersion = (int) o->getProperty ("tasteMLVersion");
        if (tasteVersion >= 1)
            tasteModel.fromVar (o->getProperty ("tasteML"));
        else
            tasteModel.reset();
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

    // 0.48 Harmony 2.0: use the previous chord's individual voices, not only
    // its average pitch. This lets each voice move to the nearest practical note
    // while still respecting the register lane and voicing width.
    std::vector<int> prevVoices;
    if(barOffset>0)
    {
        int firstStep=1000000;
        for(const auto& ev : s.notes)
            if(ev.channel==1 && ev.step>=(barOffset-1)*16 && ev.step<barOffset*16)
                firstStep=std::min(firstStep,ev.step);
        for(const auto& ev : s.notes)
            if(ev.channel==1 && ev.step==firstStep)
                prevVoices.push_back(ev.note);
        std::sort(prevVoices.begin(),prevVoices.end());
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
            if(prevVoices.empty())
                cost += 0.6f*std::abs(mean-laneCentre);
            else
            {
                // Per-voice movement dominates the decision. A common pitch class
                // is slightly rewarded because real players naturally retain voices.
                float motion=0.0f;
                for(int p : v)
                {
                    int nearest=999;
                    bool commonTone=false;
                    for(int q : prevVoices)
                    {
                        nearest=std::min(nearest,std::abs(p-q));
                        if((((p%12)+12)%12)==(((q%12)+12)%12))
                            commonTone=true;
                    }
                    motion += (float) nearest;
                    if(commonTone) motion -= 0.75f;
                }
                cost += 0.58f * motion / (float)n;
            }
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
        if(!anchor && complexity>0.5f&&r.nextFloat()<0.22f)
            note += r.nextBool() ? 7 : -5;

        // 0.48 Harmonic anticipation: the final offbeat can briefly point to
        // the next chord. Keep the motion inside the active scale.
        if(!anchor && x>=11 && x<=14 && complexity>0.38f && r.nextFloat()<0.24f)
        {
            const auto prog=progressionDegrees();
            if(!prog.empty())
            {
                const int nextDegree=prog[(size_t)((barOffset+1)%(int)prog.size())];
                const int nextRoot=foldIntoLane(degreeToPitch(nextDegree,2),lo,hi);
                if(std::abs(nextRoot-root)>0)
                    note=nextRoot;
            }
        }
        note=foldIntoLane(snapToScale(note),lo,hi);
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

    // 0.70 Composer Grammar: one macro plan coordinates the existing engines.
    const auto composerPlan = midiforge::ComposerGrammar::makePlan (
        bars, energy, complexity, melodyType, mood, genre,
        hash32 (loopSeed ^ 0xC0A70970u));
    const int composerPhrase = barOffset / 4;
    const auto composerState = composerPlan.stateFor (composerPhrase);

    e = juce::jlimit (0.0f, 1.0f,
        0.68f * e + 0.32f * composerState.tension);

    // These are soft macro targets; genre/mood DNA remains the primary language.
    dnaSpace = juce::jlimit (0.0f, 1.0f,
        dnaSpace + (composerState.space - 0.50f) * 0.16f);
    dnaDensity = juce::jlimit (0.0f, 1.0f,
        dnaDensity + (composerState.density - 0.50f) * 0.14f);
    dnaMotif = juce::jlimit (0.0f, 1.0f,
        0.82f * dnaMotif + 0.18f * composerState.motifStrength);

    int melLo = 62, melHi = 86;
    melodyCoreLane (melLo, melHi);
    const auto prof = soundProfileFor(soundTarget);
    const bool sparseAllowed = (melodyType == SparseLeadMelody || genre == Ambient);
    // 0.81 Simple Melody Class: roughly half the search space is intentionally
    // composed with a compact 2-4 note vocabulary. This is different from merely
    // lowering density: the pitch contour, rhythm size and leap language also
    // become simpler, so the final bank can contain genuinely memorable melodies.
    // Native archetype: build the melody for the same archetype that this
    // MAGIC candidate will later be judged/selected under. variationSalt is c+1
    // during candidate search, while 0 remains a safe default for normal calls.
    const int nativeArchetype = ((juce::jmax (0, variationSalt - 1)) % 8 + 8) % 8;

    // Simple is now an archetype-sensitive composition choice instead of one
    // global coin flip. MINIMAL strongly prefers compact cells; WEIRD/WILDCARD
    // leave more room for angular phrases; the other archetypes stay balanced.
    float simpleProbability = 0.58f;
    switch (nativeArchetype)
    {
        case 0: simpleProbability = 0.56f; break; // HOOK
        case 1: simpleProbability = 0.48f; break; // GROOVE
        case 2: simpleProbability = 0.50f; break; // HARMONY
        case 3: simpleProbability = 0.64f; break; // MOTIF
        case 4: simpleProbability = 0.90f; break; // MINIMAL
        case 5: simpleProbability = 0.34f; break; // WEIRD
        case 6: simpleProbability = 0.52f; break; // EMOTIONAL
        default: simpleProbability = 0.44f; break; // WILDCARD
    }
    const bool simpleCandidate =
        (hash32 (identitySeed ^ 0xA11CE55u) % 1000u)
        < (uint32_t) juce::roundToInt (simpleProbability * 1000.0f);

    // 0.61 Tempo Feel Engine: BPM changes the *time feel* of the same musical
    // language instead of simply deleting notes at faster tempos. The old engine
    // progressively reduced density above 120 BPM and then suppressed 1/16-note
    // positions again above ~160 BPM, which made fast DAW tempos sound like the
    // generator had a practical ceiling around 150-160 BPM.
    const double hostBpm = juce::jlimit (40.0, 240.0, currentBpm.load());
    const float slowTempo = juce::jlimit (0.0f, 1.0f, (120.0f - (float) hostBpm) / 60.0f);
    const float fastTempo = juce::jlimit (0.0f, 1.0f, ((float) hostBpm - 120.0f) / 60.0f);
    const float veryFastTempo = juce::jlimit (0.0f, 1.0f, ((float) hostBpm - 170.0f) / 50.0f);

    // Keep bar-level musical density almost stable across BPM. At high tempos
    // the engine changes subdivision usage and sustain instead of throwing away
    // the melody's identity.
    const float tempoDensityMul = juce::jlimit (0.92f, 1.10f,
        1.0f + 0.08f * slowTempo + 0.05f * fastTempo + 0.03f * veryFastTempo);
    const float tempoSpaceBonus = juce::jlimit (0.0f, 0.10f,
        0.045f * fastTempo + 0.02f * veryFastTempo);
    // Faster BPMs need shorter note occupancy so the next rhythmic event remains
    // perceptually readable. This is deliberately a reduction, not a legato boost.
    const float tempoLegatoBoost = juce::jlimit (-0.20f, 0.08f,
        0.04f * slowTempo - 0.12f * fastTempo - 0.08f * veryFastTempo);
    const float tempoSixteenthBoost = juce::jlimit (0.0f, 0.42f,
        0.05f * fastTempo + 0.28f * veryFastTempo);
    const float tempoOffbeatBoost = juce::jlimit (0.0f, 0.24f,
        0.04f * fastTempo + 0.12f * veryFastTempo);
    const int tempoLengthCap = juce::jlimit (2, 8,
        juce::roundToInt (6.0f - 2.0f * fastTempo - 1.5f * veryFastTempo));

    const int minNotes = simpleCandidate
        ? 2
        : juce::jmax (2,
            juce::roundToInt ((float) (sparseAllowed ? juce::jmin (2, prof.minNotes) : prof.minNotes)
                              * (1.0f + 0.12f * fastTempo + 0.08f * veryFastTempo)));
    const int minHits = simpleCandidate
        ? 2
        : juce::jmax (2,
            juce::roundToInt ((float) (sparseAllowed ? juce::jmin (2, prof.minHits) : prof.minHits)
                              * (1.0f + 0.10f * fastTempo + 0.08f * veryFastTempo)));

    // 0.58.3 Context-Aware Generation: melody now reads the musical space that
    // already exists in this bar before choosing its own rhythm and register.
    // Chords, bass, drums and arp become soft constraints rather than separate
    // generators competing for the same grid.
    struct MelodyContext
    {
        std::array<float, 16> chordCover {};
        std::array<float, 16> bassCover {};
        std::array<float, 16> drumCover {};
        std::array<float, 16> arpCover {};
        float backdropDensity = 0.0f;
        float chordDensity = 0.0f;
        float bassDensity = 0.0f;
        float drumDensity = 0.0f;
        float arpDensity = 0.0f;
        float chordPitchCenter = -1.0f;
    } context;

    {
        float chordPitchSum = 0.0f;
        float chordPitchWeight = 0.0f;
        std::array<bool, 16> chordHit {};
        std::array<bool, 16> bassHit {};
        std::array<bool, 16> drumHit {};
        std::array<bool, 16> arpHit {};

        const int barStart = barOffset * 16;
        const int barEnd = barStart + 16;

        for (const auto& ev : s.notes)
        {
            if (ev.step < barStart || ev.step >= barEnd)
                continue;

            const int localStart = juce::jlimit (0, 15, ev.step - barStart);
            const int localEnd = juce::jlimit (localStart + 1, 16,
                                               localStart + juce::jmax (1, ev.length));

            float* coverage = nullptr;
            std::array<bool, 16>* onsetMask = nullptr;

            if (ev.channel == 1)
            {
                coverage = context.chordCover.data();
                onsetMask = &chordHit;
                chordPitchSum += (float) ev.note * (float) juce::jmax (1, ev.length);
                chordPitchWeight += (float) juce::jmax (1, ev.length);
            }
            else if (ev.channel == 2)
            {
                coverage = context.bassCover.data();
                onsetMask = &bassHit;
            }
            else if (ev.channel == 4)
            {
                coverage = context.arpCover.data();
                onsetMask = &arpHit;
            }
            else if (ev.channel == 5)
            {
                coverage = context.drumCover.data();
                onsetMask = &drumHit;
            }

            if (coverage != nullptr && onsetMask != nullptr)
            {
                (*onsetMask)[(size_t) localStart] = true;
                for (int step = localStart; step < localEnd; ++step)
                    coverage[(size_t) step] = 1.0f;
            }
        }

        int chordSteps = 0, bassSteps = 0, drumSteps = 0, arpSteps = 0;
        for (int step = 0; step < 16; ++step)
        {
            chordSteps += chordHit[(size_t) step] ? 1 : 0;
            bassSteps += bassHit[(size_t) step] ? 1 : 0;
            drumSteps += drumHit[(size_t) step] ? 1 : 0;
            arpSteps += arpHit[(size_t) step] ? 1 : 0;
            const float busy = juce::jlimit (0.0f, 1.0f,
                0.34f * context.chordCover[(size_t) step]
              + 0.30f * context.bassCover[(size_t) step]
              + 0.20f * context.drumCover[(size_t) step]
              + 0.16f * context.arpCover[(size_t) step]);
            context.backdropDensity += busy;
        }

        context.chordDensity = (float) chordSteps / 16.0f;
        context.bassDensity = (float) bassSteps / 16.0f;
        context.drumDensity = (float) drumSteps / 16.0f;
        context.arpDensity = (float) arpSteps / 16.0f;
        context.backdropDensity /= 16.0f;
        if (chordPitchWeight > 0.0f)
            context.chordPitchCenter = chordPitchSum / chordPitchWeight;
    }

    const float contextSyncBias =
        (melodyType == HookMelody || melodyType == VocalLikeMelody) ? 0.10f
        : (melodyType == SparseLeadMelody ? -0.08f : 0.0f);
    const float contextGapBias =
        (melodyType == SparseLeadMelody || genre == Ambient) ? 0.16f : 0.06f;

    auto contextStepWeight = [&](int step) -> float
    {
        const float chord = context.chordCover[(size_t) step];
        const float bass = context.bassCover[(size_t) step];
        const float drums = context.drumCover[(size_t) step];
        const float arp = context.arpCover[(size_t) step];
        const float busy = juce::jlimit (0.0f, 1.0f,
            0.34f * chord + 0.30f * bass + 0.20f * drums + 0.16f * arp);

        // Fill genuine gaps first, but preserve intentional anchors on strong
        // beats and for hook/vocal-like lines.
        float weight = 1.0f + contextGapBias * (1.0f - busy * 2.0f);
        if (step % 4 == 0)
            weight += 0.06f + 0.06f * contextSyncBias;
        if (chord > 0.0f)
            weight += contextSyncBias;
        if (bass > 0.0f)
            weight += 0.035f * (1.0f + contextSyncBias);
        if (busy > 0.78f && step % 4 != 0)
            weight *= 0.62f;
        if (busy < 0.20f)
            weight *= 1.08f;

        return juce::jlimit (0.52f, 1.24f, weight);
    };

    // 0.26 Melody Engine 3.0: give every 4-bar phrase a compositional grammar.
    // A = statement, A' = variation, B = contrast/peak, A'' = return/cadence.
    // The grammar changes the destination of notes rather than merely adding
    // random pitch offsets, so a loop develops an audible arc.
    // 0.58.1 Melody Diversity 2.0: expand the melodic search space instead of
    // merely adding more random seeds. Each loop now receives an independent
    // contour language, interval language, tension profile and register behavior.
    // These axes deliberately sit outside the genre labels, so the same genre can
    // produce soft, tense, angular, chant-like or wide-register material.
    // 0.72 Creative Range: choose a coherent musical language before writing notes.
    // The plan expands the *creative* search space rather than simply adding pitch
    // randomness: contour, interval vocabulary, rhythm family, repetition behavior,
    // register journey, harmonic color and duration language are selected together.
    const auto creativeRange = midiforge::CreativeRange::makePlan (
        melodyType, mood, genre, e, complexity,
        hash32 (identitySeed ^ 0x72C0FFEEu));

    const int phraseStyle = creativeRange.contourFamily;
    const int intervalLanguage = creativeRange.intervalFamily;
    const int tensionProfile = (int)(hash32(identitySeed ^ 0x7f4a7c15u
                                             ^ (uint32_t) creativeRange.harmonyPersonality) % 8u);
    const int registerProfile = creativeRange.registerJourney;
    const int rhythmicLanguage = creativeRange.rhythmFamily;

    const float poolTension = juce::jlimit(0.05f, 0.88f,
        0.10f
        + 0.34f * ((float)tensionProfile / 7.0f)
        + 0.18f * moodTension
        + 0.12f * dnaSurprise
        + 0.08f * ((float)eraNovelty[juce::jlimit(0,5,era)])
        + 0.10f * dnaLeap
        + 0.08f * (composerState.tension - 0.50f));

    const float poolLeapChance = simpleCandidate
        ? juce::jmin (0.14f, juce::jlimit(0.04f, 0.82f,
            leapChance
            + 0.08f * poolTension
            + 0.02f * dnaLeap))
        : juce::jlimit(0.04f, 0.82f,
            leapChance
            + 0.16f * poolTension
            + 0.10f * ((intervalLanguage == 2 || intervalLanguage == 5 || intervalLanguage == 7) ? 1.0f : 0.0f)
            + 0.05f * dnaLeap);

    const bool wideIntervalLanguage =
        !simpleCandidate &&
        (intervalLanguage == 2 || intervalLanguage == 3 || intervalLanguage == 5
         || intervalLanguage == 6 || intervalLanguage == 7);

    const bool highRegisterLanguage =
        registerProfile == 2 || registerProfile == 5 || registerProfile == 7;

    const float phraseStrength = juce::jlimit(0.24f, 0.95f,
        (0.38f
        + 0.22f * dnaMotif
        + 0.18f * poolTension
        + 0.10f * (1.0f - dnaSurprise)
        + 0.07f * ((phraseStyle >= 6) ? 1.0f : 0.0f))
        * (simpleCandidate ? 0.58f : 1.0f));

    // 0.58.4 Phrase Tension Engine: tension is now an explicit four-bar target,
    // not only an incidental result of contour/leaps. The engine creates a
    // controlled A -> A' -> B -> A'' pressure curve while allowing unresolved
    // loops to hand energy into the next cycle.
    const int phraseRole = composerState.legacyRole;
    const float roleTension = composerState.tension;
    const float roleTensionVariation =
        (((float) tensionProfile / 7.0f) - 0.5f) * 0.22f
        + moodTension * 0.16f
        + dnaSurprise * 0.12f;
    const float phraseTension = juce::jlimit (0.08f, 0.94f,
        roleTension + roleTensionVariation
        + 0.08f * (composerState.tension - 0.50f));

    const float tensionPulse = (phraseRole == 2)
        ? 0.12f * phraseTension
        : 0.0f;

    auto phraseTargetOffset = [&](int noteIndex, int noteCount) -> int
    {
        if (noteCount <= 0) return 0;
        const float pos = (float)noteIndex / (float)juce::jmax(1, noteCount - 1);

        // 0.72 expands the contour vocabulary with less symmetrical human shapes.
        static const int contours[18][5] =
        {
            { 0,  1,  2,  1,  0 }, // rise / settle
            { 0,  2,  1,  3,  0 }, // hook peak
            { 1,  0, -1,  1,  0 }, // fall / return
            { 0, -1,  1,  2,  0 }, // delayed rise
            { 0,  2,  3,  1, -1 }, // high point / release
            { 0, -2,  0,  2,  0 },  // dip / rebound
            { 0,  3, -1,  2,  0 }, // leap / answer
            { 1, -1,  2, -2,  1 }, // angular zigzag
            { 0,  0,  3,  0, -1 }, // plateau / spike
            { 0, -2, -3,  1,  2 }, // descending valley
            { 0,  1,  0, -2,  2 }, // question / answer
            { 0, -1, -3, -1,  1 }, // dark fall / rebound
            { 0,  3,  1,  4,  0 }, // climbing hook
            { 0, -2, -1,  1,  4 }, // slow rise to late peak
            { 0,  1,  4,  3,  1 }, // apex / hold
            { 0, -3, -2,  2,  1 }, // deep dip / recovery
            { 0,  4,  2, -1,  2 }, // leap / fall / answer
            { 0, -1,  2,  5,  3 }  // long climb / high return
        };
        int slot = juce::jlimit(0, 4, (int)std::floor(pos * 4.999f));
        int value = contours[phraseStyle][slot];

        // Secondary contour language adds shape that is not tied to the phrase
        // grammar itself. This is what stops every "B" bar from feeling like the
        // same cheerful rise-and-resolve template.
        switch (intervalLanguage)
        {
            case 1: value += ((noteIndex & 1) ? 1 : -1); break;                    // thirds
            case 2: value += ((noteIndex % 3 == 1) ? 2 : 0); break;               // fourth/fifth pushes
            case 3: value += ((noteIndex % 4 == 2) ? -3 : (noteIndex % 4 == 3 ? 2 : 0)); break;
            case 4: value += ((noteIndex % 4 == 0) ? 1 : (noteIndex % 4 == 2 ? -1 : 0)); break;
            case 5: value += ((noteIndex & 1) ? 4 : -2); break;                   // octave-oriented degree jump
            case 6: value += ((noteIndex % 5 == 2) ? 3 : (noteIndex % 5 == 4 ? -2 : 0)); break;
            case 7: value += (noteIndex > noteCount / 2 ? -2 : 2); break;         // falling response
            case 8: value += ((noteIndex % 3 == 0) ? 3 : -1); break;              // anchor + drift
            case 9: value += ((noteIndex + phraseStyle) % 4 == 0 ? -3 : 1); break;
            case 10: value += ((noteIndex % 3 == 1) ? 4 : (noteIndex % 3 == 2 ? -2 : 0)); break; // leap / recovery
            case 11: value += ((noteIndex & 1) ? 3 : -1); break;                         // pendulum
            default: break;
        }

        if (highRegisterLanguage && noteIndex == noteCount / 2)
            value += 2;

        return value;
    };
    const int archetype = (int) (hash32(generationSeed
                                        ^ (uint32_t) variationSalt * 0x27d4eb2du
                                        ^ (uint32_t) genre * 0x165667b1u
                                        ^ (uint32_t)(dnaRhythmBias + 17) * 0x9e3779b9u
                                        ^ (uint32_t) intervalLanguage * 0x6c8e9cf5u) % 24u);

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
        // 0.58.1 additional melodic rhythm languages: asymmetric starts, rests,
        // late answers and pickup-heavy cells. They are still grid-safe.
        {1, 4, 7, 10, 14, -1,-1,-1,-1,-1},
        {0, 3, 7, 9, 14, -1,-1,-1,-1,-1},
        {0, 1, 6, 10, 13, -1,-1,-1,-1,-1},
        {2, 5, 8, 12, 15, -1,-1,-1,-1,-1},
        {0, 2, 5, 11, 14, -1,-1,-1,-1,-1},
        {1, 5, 8, 12, 15, -1,-1,-1,-1,-1},
        {0, 3, 8, 10, 14, -1,-1,-1,-1,-1},
        {2, 6, 9, 12, 15, -1,-1,-1,-1,-1},
        {0, 4, 7, 8, 13, -1,-1,-1,-1,-1},
        {1, 4, 6, 11, 14, -1,-1,-1,-1,-1},
        {0, 5, 9, 10, 15, -1,-1,-1,-1,-1},
        {2, 3, 8, 12, 14, -1,-1,-1,-1,-1},

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
    {
        const int hits = hitCount (t);
        if (hits < minHits)
            continue;
        if (simpleCandidate && hits > 4)
            continue;
        eligible.push_back (t);
    }
    if (eligible.empty()) eligible.push_back(0);
    const int eligibleN = (int)eligible.size();
    // Genre DNA nudges the rhythmic family without hard-locking it.
    int rhythmType = eligible[(size_t)((((int)(hash32(identitySeed ^ 0x51ed270bu) % (uint32_t)eligibleN)
                                          + dnaRhythmBias
                                          + (int)(dnaSync * 3.0f)
                                          + rhythmicLanguage * 2
                                          + creativeRange.rhythmBias
                                          + juce::roundToInt (creativeRange.asymmetry * 5.0f)
                                          + juce::roundToInt (fastTempo * 5.0f)
                                          - juce::roundToInt (slowTempo * 2.0f)) % eligibleN + eligibleN) % eligibleN)];

    // 0.82 Native Archetypes: rhythm is selected for the archetype's behavior,
    // not only for genre/tempo. This keeps the eight MAGIC families audibly
    // distinct before later polish passes touch the MIDI.
    auto archetypeRhythmAffinity = [&] (int patternIndex) -> float
    {
        int hits = 0, offbeats = 0, oddSixteenths = 0, downbeats = 0, lateHits = 0;
        int previousStep = -1, gaps = 0, gapSum = 0;

        for (int k = 0; k < 10; ++k)
        {
            const int x = rhythms[patternIndex][k];
            if (x < 0) break;
            ++hits;
            if ((x % 4) != 0) ++offbeats;
            if ((x & 1) != 0) ++oddSixteenths;
            if ((x % 8) == 0) ++downbeats;
            if (x >= 9) ++lateHits;
            if (previousStep >= 0) { ++gaps; gapSum += x - previousStep; }
            previousStep = x;
        }

        const float hitScore = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs ((float) hits - 4.0f) / 4.0f);
        const float offbeatRatio = hits > 0 ? (float) offbeats / (float) hits : 0.0f;
        const float oddRatio = hits > 0 ? (float) oddSixteenths / (float) hits : 0.0f;
        const float downbeatRatio = hits > 0 ? (float) downbeats / (float) hits : 0.0f;
        const float lateRatio = hits > 0 ? (float) lateHits / (float) hits : 0.0f;
        const float gapVariety = gaps > 0
            ? juce::jlimit (0.0f, 1.0f, std::abs ((float) gapSum / (float) gaps - 4.0f) / 4.0f)
            : 0.0f;

        switch (nativeArchetype)
        {
            case 0: // HOOK: memorable, grounded opening and mid-bar answer.
                return 0.34f * hitScore + 0.30f * downbeatRatio
                     + 0.18f * (1.0f - oddRatio) + 0.18f * (1.0f - lateRatio * 0.5f);
            case 1: // GROOVE: offbeat pocket without becoming pure 16th noise.
                return 0.28f * hitScore + 0.34f * offbeatRatio
                     + 0.20f * oddRatio + 0.18f * (1.0f - downbeatRatio * 0.5f);
            case 2: // HARMONY: clear structural anchors around chord changes.
                return 0.30f * hitScore + 0.42f * downbeatRatio
                     + 0.16f * (1.0f - oddRatio) + 0.12f * (1.0f - lateRatio);
            case 3: // MOTIF: repeatable cell size and moderate symmetry.
                return 0.42f * hitScore + 0.24f * (1.0f - oddRatio)
                     + 0.18f * downbeatRatio + 0.16f * (1.0f - lateRatio);
            case 4: // MINIMAL: three or fewer strong events with breathing room.
                return 0.58f * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs ((float) hits - 3.0f) / 3.0f)
                     + 0.24f * (1.0f - oddRatio) + 0.18f * downbeatRatio;
            case 5: // WEIRD: asymmetric / late / off-grid language.
                return 0.24f * hitScore + 0.28f * oddRatio
                     + 0.22f * lateRatio + 0.18f * offbeatRatio + 0.08f * gapVariety;
            case 6: // EMOTIONAL: spacious, readable pulse with a destination.
                return 0.34f * hitScore + 0.30f * downbeatRatio
                     + 0.22f * lateRatio + 0.14f * (1.0f - oddRatio);
            default: // WILDCARD: keep the generic generator's stochastic choice.
                return 0.20f * hitScore + 0.26f * offbeatRatio
                     + 0.20f * oddRatio + 0.18f * lateRatio + 0.16f * gapVariety;
        }
    };

    if (eligibleN > 1 && nativeArchetype != 7)
    {
        float bestAffinity = -1000.0f;
        int bestPattern = rhythmType;
        for (int t : eligible)
        {
            const float hashJitter =
                0.07f * ((float) (hash32 (identitySeed ^ (uint32_t) (t * 97 + 11)) % 1000u) / 1000.0f);
            const float score = archetypeRhythmAffinity (t) + hashJitter;
            if (score > bestAffinity)
            {
                bestAffinity = score;
                bestPattern = t;
            }
        }
        rhythmType = bestPattern;
    }

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
    const bool allowsOffGrid = dnaSync > 0.55f || creativeRange.asymmetry > 0.66f
        || nativeArchetype == 1 || nativeArchetype == 5
        || rhythmType == 0 || rhythmType == 3 || rhythmType == 5
        || fastTempo > 0.58f;
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
    const float density = juce::jlimit(0.16f, 0.95f, prof.densityMul *
        (0.64f + 0.30f * melodyDensity + 0.12f * e
        + 0.16f * (dnaDensity - 0.50f) - 0.10f * (dnaSpace - 0.50f)
        - 0.60f * (pauseChance - 0.10f)
        - tempoSpaceBonus)
        * tempoDensityMul
        * (simpleCandidate ? 0.72f : 1.0f));
    std::vector<int> chosen;
    auto posRank = [&](int x) { return hash32(identitySeed ^ (uint32_t)(x * 97 + 31)) % 1000u; };
    for (int x : positions)
    {
        const bool sixteenth = (x & 1) != 0;
        float positionDensity = density;
        if (fastTempo > 0.05f && sixteenth)
            positionDensity *= juce::jlimit (1.0f, 1.0f + tempoSixteenthBoost, 1.0f + tempoSixteenthBoost);
        else if (slowTempo > 0.05f && sixteenth)
            positionDensity = juce::jlimit (0.20f, 0.98f, positionDensity + 0.10f * slowTempo);

        // Fast tempos deliberately become more articulate around offbeats instead
        // of becoming simply emptier. This makes 170-220 BPM retain a usable pulse.
        if (fastTempo > 0.05f && (x % 4) != 0)
            positionDensity *= 1.0f + tempoOffbeatBoost;

        // 0.58.3: avoid stacking the melody onto a fully occupied slice of the
        // backdrop, while still allowing deliberate chord/bass alignment.
        positionDensity *= contextStepWeight (x);

        // Composer Grammar can ask a phrase to breathe or build without taking
        // control away from Rhythm Grammar.
        positionDensity *= juce::jlimit (0.84f, 1.16f,
            1.0f + (composerState.density - 0.50f) * 0.34f);
        if (composerState.role == midiforge::ComposerGrammar::Peak
            || composerState.role == midiforge::ComposerGrammar::Contrast)
        {
            if ((x % 4) != 0)
                positionDensity *= 1.0f + 0.10f * composerState.tension;
        }
        else if (composerState.role == midiforge::ComposerGrammar::Return
                 || composerState.role == midiforge::ComposerGrammar::Release)
        {
            if ((x % 4) == 0)
                positionDensity *= 1.04f;
        }

        // 0.58.4: high-tension bars prefer delayed/offbeat entries and more air
        // immediately after a strong hit; the return bar moves back toward
        // grounded downbeats and longer breathing room.
        if (phraseRole == 2)
        {
            if ((x % 4) != 0) positionDensity *= 1.0f + 0.22f * phraseTension;
            if ((x % 4) == 0) positionDensity *= 0.92f - 0.10f * phraseTension;
            if (x <= 1) positionDensity *= 0.90f;
        }
        else if (phraseRole == 3 && (x % 4) == 0)
        {
            positionDensity *= 1.04f + 0.08f * (1.0f - phraseTension);
        }

        if ((float)posRank(x) / 1000.0f < positionDensity)
            chosen.push_back(x);
    }
    // Never fall below a playable number of notes: bring back the most
    // "important" removed positions (deterministic per loop identity).
    while ((int)chosen.size() < minNotes && chosen.size() < positions.size())
    {
        int bestPos = -1; uint32_t bestRank = 100000u;
        for (int x : positions)
        {
            if (std::find(chosen.begin(), chosen.end(), x) != chosen.end()) continue;
            const uint32_t rk = (uint32_t) juce::jlimit (
                0, 1000000,
                juce::roundToInt ((float) posRank (x) / contextStepWeight (x)));
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

    // MINIMAL is a first-class phrase language: short, memorable cells rather
    // than a damaged version of a complex melody.
    if (archetype == 4)
    {
        while (chosen.size() > 4)
            chosen.erase (chosen.begin() + chosen.size() / 2);
    }

    // A true simple candidate is deliberately capped at four onsets. Prefer the
    // downbeat, the middle of the cell and the tail, then fill the remaining slot
    // from the deterministic rhythmic ranking. This preserves phrase shape instead
    // of simply taking the first four generated notes.
    if (simpleCandidate && chosen.size() > 4)
    {
        std::vector<int> compact;
        compact.reserve (4);

        const auto keep = [&] (int position)
        {
            if (std::find (chosen.begin(), chosen.end(), position) == chosen.end())
                return;
            if (std::find (compact.begin(), compact.end(), position) == compact.end()
                && compact.size() < 4)
                compact.push_back (position);
        };

        keep (0);
        keep (8);
        keep (positions.empty() ? 15 : positions.back());

        while (compact.size() < 4)
        {
            int best = -1;
            uint32_t bestRank = std::numeric_limits<uint32_t>::max();
            for (const int position : chosen)
            {
                if (std::find (compact.begin(), compact.end(), position) != compact.end())
                    continue;

                const uint32_t rank = hash32 (
                    identitySeed ^ (uint32_t) (position * 113 + 0x51ED270Bu));
                if (rank < bestRank)
                {
                    bestRank = rank;
                    best = position;
                }
            }

            if (best < 0)
                break;

            compact.push_back (best);
        }

        chosen = std::move (compact);
        std::sort (chosen.begin(), chosen.end());
    }

    // Harmonic context is used as gravity, not as a command to resolve every bar.
    const auto prog = progressionDegrees();
    const int degree = prog[(size_t)(barOffset % (int)prog.size())];
    const int nextDegree = prog[(size_t)((barOffset + 1) % (int)prog.size())];
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
        { 2, 0, 5, 5, 3, -1 }, { 4, 6, 2, 1, 5, -1 },

        // 0.58.1: second-generation motif vocabulary. More negative/rebound,
        // wider interval and off-centre shapes are intentionally represented.
        { 0, 1, 4, 2, -1, 3 }, { 3, 1, 5, 0, 4, -1 },
        { 5, 4, 2, 6, 1, 3 }, { 0, 3, 1, -2, 2, -1 },
        { 2, -1, 3, 0, 5, 1 }, { 4, 0, -1, 4, 2, -1 },
        { 1, 5, 2, 0, 6, 2 }, { 6, 2, 4, 1, -1, 3 },
        { 0, 5, 2, -2, 1, 4 }, { 3, 6, 1, 4, 0, -2 },
        { 0, -2, 2, 5, 1, 4 }, { 5, 1, -2, 3, 0, 4 },
        { 2, 6, 3, -1, 4, 1 }, { 0, 4, 6, 1, 5, -2 },
        { 4, -1, 2, 6, 0, 3 }, { 1, 3, -2, 5, 2, 6 }
    };

    const int motifType = (int)(hash32(identitySeed ^ (uint32_t)(archetype * 0x51ed270bu)
                                           ^ (uint32_t)(dnaMotif * 1000.0f)) % (simpleCandidate ? 16u : 32u));
    const int motifShift = (int)((identitySeed >> 16) % (uint32_t)scaleCount);

    const int motifTransform = (int)(hash32(identitySeed ^ 0x6d2b79f5u) % 8u);
    const int motifRotation = (int)(hash32(identitySeed ^ 0x1b873593u) % 5u);

    auto motifDegree = [&](int index) -> int
    {
        int pos = (index + motifRotation) % 5;
        if (motifTransform == 1 || motifTransform == 4) pos = 4 - pos;
        int raw = motifs[motifType][pos];
        if (motifTransform == 2) raw = 4 - raw;
        if (motifTransform == 3 && (pos & 1)) raw += 2;
        if (motifTransform == 5 && pos == 2) raw += 3;
        if (motifTransform == 6 && (pos == 1 || pos == 4)) raw -= 2;
        if (motifTransform == 7 && (pos & 1)) raw = -raw;
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
    auto archetypeProgression = progressionDegrees ();
    const int archetypeDegree = !archetypeProgression.empty ()
        ? archetypeProgression[(size_t) (barOffset % (int) archetypeProgression.size())]
        : 0;

    auto planDegree = [&](size_t i) -> int
    {
        int d = motifDegree((int)i);

        // Native archetype pitch grammar. The later archetype pass still acts
        // as a safety/polish stage, but the line already speaks the intended
        // musical language before that pass.
        switch (nativeArchetype)
        {
            case 0: // HOOK: keep a recognizable motif spine.
                d = juce::roundToInt (0.74f * (float) d + 0.26f * (float) motifDegree ((int) i));
                break;
            case 1: // GROOVE: smaller scalar motion; rhythm carries more identity.
                if (i > 0 && (chosen[i] % 4) != 0)
                    d += ((i & 1u) != 0u) ? -1 : 1;
                break;
            case 2: // HARMONY: structural beats lean toward the active chord degree.
                if ((chosen[i] % 4) == 0)
                    d = juce::roundToInt (0.58f * (float) d + 0.42f * (float) archetypeDegree);
                break;
            case 3: // MOTIF: tighter identity than the generic contour pool.
                d = juce::roundToInt (0.60f * (float) d + 0.40f * (float) motifDegree ((int) i));
                if (i >= 2 && (i % 3) == 2)
                    d = juce::roundToInt (0.78f * (float) d + 0.22f * (float) motifDegree ((int) (i - 2)));
                break;
            case 4: // MINIMAL: anchor-heavy and compact.
                if ((chosen[i] % 4) == 0)
                    d = juce::roundToInt (0.80f * (float) d + 0.20f * (float) archetypeDegree);
                if (i > 0)
                    d = juce::roundToInt (0.82f * (float) d + 0.18f * (float) motifDegree ((int) i));
                break;
            case 5: // WEIRD: occasional deliberate degree jumps, but still musical.
                if (! simpleCandidate && ((int) i % 3) == 1)
                {
                    const int jump = (hash32 (identitySeed ^ (uint32_t) (i * 131 + 0x0E17u)) & 1u) ? 3 : -3;
                    d += jump;
                }
                break;
            case 6: // EMOTIONAL: explicit rise -> peak -> release destination.
            {
                const float pos = (float) i / (float) juce::jmax<size_t> (1, chosen.size() - 1);
                const float arc = 1.0f - std::abs (2.0f * pos - 1.0f);
                d += juce::roundToInt (arc * 1.7f);
                break;
            }
            default: // WILDCARD: leave the broad motif/creative systems in charge.
                break;
        }

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

        // The macro plan supplies broad register motion; the existing
        // contour grammar still controls the detailed note-to-note shape.
        if (composerState.registerLift != 0.0f)
        {
            const float registerShape = i < chosen.size() / 2 ? 0.42f : 0.68f;
            d += juce::roundToInt (composerState.registerLift * registerShape);
        }

        // Explicit tension trajectory: B widens the melodic destination around
        // the midpoint, A'' eases the register back down for the loop return.
        if (phraseRole == 2)
        {
            const int tensionOffset = (i < chosen.size() / 2)
                ? juce::roundToInt (phraseTension * 1.5f)
                : juce::roundToInt (phraseTension * 2.4f);
            d += ((i & 1u) != 0u ? -tensionOffset : tensionOffset);
        }
        else if (phraseRole == 3 && i >= chosen.size() / 2)
        {
            d -= juce::roundToInt ((1.0f - phraseTension) * 0.8f);
        }

        // Creative Register Journey: register is a phrase destination, not a
        // random octave flip. Each journey uses the same scale-degree language
        // but changes where the line wants to live over the bar.
        {
            const float pos = (float) i / (float) juce::jmax<size_t> (1, chosen.size() - 1);
            float journey = 0.0f;
            switch (creativeRange.registerJourney)
            {
                case 1: journey = 3.5f * pos; break;                              // rising
                case 2: journey = -3.5f * pos; break;                             // falling
                case 3: journey = 6.0f * (1.0f - std::abs (2.0f * pos - 1.0f)); break; // peak
                case 4: journey = -3.5f + 7.0f * pos; break;                    // low -> high
                case 5: journey = (i & 1u) ? 2.8f : -2.0f; break;                // call/answer register
                case 6: journey = ((i & 1u) ? -1.0f : 1.0f) * 3.0f; break;       // wide orbit
                case 7: journey = (pos > 0.45f && pos < 0.75f) ? 5.0f : 0.0f; break; // sudden peak
                default: break;                                                   // centered
            }
            journey += creativeRange.registerBias * (0.25f + 0.75f * pos) / 2.0f;
            d += juce::roundToInt (journey);
        }

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
        const int big = wideIntervalLanguage ? ((scaleCount >= 7) ? 7 : 6)
                                             : ((scaleCount >= 7) ? 5 : 4);
        std::vector<int> raw(chosen.size());
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            raw[i] = planDegree(i);
            // Octave displacement (formerly `note += 12`) is part of the plan.
            const uint32_t rh = hash32(identitySeed ^ (uint32_t)(i * 313 + 73));
            const bool octaveLift =
                !simpleCandidate &&
                ((registerProfile == 1 && (i & 1u))
                || (registerProfile == 2 && i == chosen.size() / 2)
                || (registerProfile == 3 && (rh % 100u) < 34u)
                || (registerProfile == 5 && (i % 3u == 1))
                || (registerProfile == 6 && (rh % 100u) < 20u));

            if ((octaveLift || ((rh >> 4) & 1u) != 0u)
                && (archetype >= 2 || highRegisterLanguage))
            {
                const int registerDirection = ((rh >> 9) & 1u) ? 1 : -1;
                raw[i] += registerDirection * scaleCount;
            }
        }
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            if (i == 0) { dPlan[i] = raw[i]; continue; }
            int delta = raw[i] - raw[i - 1];

            // Interval languages intentionally preserve different motion vocabularies.
            // The old generator collapsed almost everything into small scalar moves.
            const uint32_t ih = hash32(identitySeed ^ (uint32_t)(i * 197 + 13));
            const float roll = (float)(ih % 1000u) / 1000.0f;
            if (simpleCandidate && i > 0)
            {
                // Simple class: mostly step/third motion. Do not let a complex motif
                // leak a large interval back into an otherwise compact phrase.
                const int direction = delta < 0 ? -1 : 1;
                const int simpleLimit = (roll < 0.72f) ? 2 : 3;
                delta = direction * juce::jmin (std::abs (delta), simpleLimit);
            }
            if (wideIntervalLanguage && i > 0 && (roll < 0.42f * poolTension || intervalLanguage == 3))
            {
                const int direction = (ih & 1u) ? 1 : -1;
                if (intervalLanguage == 2)
                    delta = direction * (3 + (int)(ih % 3u));       // 3..5 scale degrees
                else if (intervalLanguage == 5)
                    delta = direction * ((ih & 2u) ? scaleCount : scaleCount + 2);
                else if (intervalLanguage == 7)
                    delta = direction * (4 + (int)(ih % 4u));       // angular leap
                else
                    delta = direction * (3 + (int)(ih % 4u));
            }

            if (std::abs(delta) >= big)
            {
                const bool deliberateLeap =
                    roll < poolLeapChance;
                if (!deliberateLeap && !wideIntervalLanguage)   // preserve wide motion only for the languages that asked for it
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
        const float laneCentre = 0.5f * (float)(melLo + melHi)
            + creativeRange.registerBias * 2.0f;
        for (int k = -4; k <= 4; ++k)
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
            const bool creativeLeapCell =
                creativeRange.leapBias > 0.58f
                && ((int) i + (int) (identitySeed & 7u)) % (creativeRange.leapBias > 0.76f ? 3 : 4) == 2;
            const bool wantsLeap = !simpleCandidate
                && (((archetype == 2 || archetype == 5 || archetype == 6 || wideIntervalLanguage || creativeLeapCell)
                && (((int)i + (int)(identitySeed & 7u)) % (wideIntervalLanguage ? 4 : 5) == (wideIntervalLanguage ? 2 : 2)))
                || ((float)(hash32(identitySeed ^ (uint32_t)(i * 131 + 7)) % 1000u) / 1000.0f
                    < juce::jlimit (0.04f, 0.92f, poolLeapChance * (0.58f + 0.36f * creativeRange.leapBias))));
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
        if (!simpleCandidate && profile == 1 && (i & 1u) == 0)
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
            float repetitionFactor = 1.0f;
            switch (creativeRange.repetitionStyle)
            {
                case 0: repetitionFactor = 1.22f; break; // motif-heavy
                case 1: repetitionFactor = (cycle == 2 ? 0.42f : 1.08f); break; // answer
                case 2: repetitionFactor = 0.74f; break; // evolving
                case 3: repetitionFactor = 0.48f; break; // loose
                case 4: repetitionFactor = (cycle == 1 ? 1.26f : (cycle == 2 ? 0.34f : 0.92f)); break; // A' focus
                case 5: repetitionFactor = 0.82f; break; // sequence
                case 6: repetitionFactor = (cycle == 2 ? 0.26f : 1.12f); break; // contrast
                case 7: repetitionFactor = 1.30f; break; // hook
                default: break;
            }
            const float memoryStrength = juce::jlimit(0.0f, 0.78f,
                motifStrength * (cycle == 2 ? 0.24f : (cycle == 1 ? 0.52f : 0.44f))
                * repetitionFactor
                * juce::jlimit (0.72f, 1.16f, 0.86f + 0.34f * creativeRange.repetition));
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

        // Harmonic gravity is now a spectrum instead of a hard safety rail.
        // Low-tension languages still resolve strongly; high-tension languages
        // may deliberately place a non-chord scale tone on beat 1/3 and resolve later.
        if (x == 0 || x == 8)
        {
            const uint32_t ah = hash32(identitySeed ^ (uint32_t)(x + 101));
            const float anchorRoll = (float)(ah % 1000u) / 1000.0f;
            const float harmonyPersonalityBias =
                (creativeRange.harmonyPersonality <= 1 ? -0.10f
                 : creativeRange.harmonyPersonality >= 6 ? 0.12f : 0.0f);
            const float tensionChance = juce::jlimit(0.04f, 0.84f,
                0.06f
                + 0.42f * poolTension
                + 0.20f * phraseTension
                + 0.08f * ((intervalLanguage == 3 || intervalLanguage == 7) ? 1.0f : 0.0f)
                + 0.10f * creativeRange.harmonyColor
                + harmonyPersonalityBias
                + tensionPulse);

            if (anchorRoll < tensionChance)
            {
                int bestTension = note;
                int bestDist = 1000;
                for (int td : { degree + 1, degree + 3, degree + 5, degree + 6 })
                {
                    const int base = pitchForDegree(td, octave);
                    for (int oct = -2; oct <= 2; ++oct)
                    {
                        const int cand = base + oct * 12;
                        if (cand < melLo || cand > melHi) continue;
                        if (chordTone(cand)) continue;
                        const int dist = std::abs(cand - note);
                        if (dist < bestDist) { bestDist = dist; bestTension = cand; }
                    }
                }
                note = bestTension;
            }
            else if (!chordTone(note))
            {
                const int root = pitchForDegree(degree, octave);
                const int third = pitchForDegree(degree + 2, octave);
                if ((ah % 100u) < (uint32_t)(50.0f + 40.0f * (1.0f - poolTension)))
                    note = (std::abs(root - previous) <= std::abs(third - previous)) ? root : third;
            }

            note = foldIntoLane(note, melLo, melHi);
            note = juce::jlimit(melLo, melHi, snapToScale(note));
        }

        // 0.58.3: when the chord voicing already occupies the lead's middle
        // register, bias the melody toward a clear upper voice instead of
        // repeatedly landing inside the chord stack.
        if (context.chordPitchCenter >= 0.0f)
        {
            const float leadCentre = 0.5f * (float) (melLo + melHi);
            if (std::abs (context.chordPitchCenter - leadCentre) < 9.0f)
            {
                int target = juce::roundToInt (context.chordPitchCenter + 9.0f);
                if (target > melHi)
                    target = juce::roundToInt (context.chordPitchCenter - 8.0f);
                target = juce::jlimit (melLo, melHi, snapToScale (target));
                note = juce::jlimit (melLo, melHi,
                    snapToScale (juce::roundToInt (0.72f * (float) note
                                                  + 0.28f * (float) target)));
            }
        }

        // 0.48 Harmonic anticipation: on the end of a four-bar cell, a late
        // note may lean toward the next chord instead of resolving only backward.
        // This creates audible forward motion while preserving the active scale.
        if (cycle == 3 && x >= 12 && x != 15 && complexity > 0.34f
            && (float)(hash32(identitySeed ^ (uint32_t)(x * 173 + 401)) % 1000u) / 1000.0f < 0.42f)
        {
            const int nextRoot = pitchForDegree(nextDegree, octave);
            const int nextThird = pitchForDegree(nextDegree + 2, octave);
            const int target = (std::abs(nextRoot - note) <= std::abs(nextThird - note)) ? nextRoot : nextThird;
            const int blended = juce::roundToInt(0.42f * (float) note + 0.58f * (float) target);
            note = juce::jlimit(melLo, melHi, snapToScale(blended));
        }

        // Final safety net (post-processing above can still create a wide interval):
        // unless it is a deliberate leap, move the note to the octave nearest the
        // previous note.  Pitch class - and so the harmony - is untouched.
        if ((barOffset > 0 || !generated.empty()) && std::abs(note - previous) >= 8
            && (float)(hash32(identitySeed ^ (uint32_t)(i * 197 + 13)) % 1000u) / 1000.0f >= poolLeapChance)
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
        const int effectiveMaxLeap = prof.maxLeap > 0
            ? juce::jmin(18, prof.maxLeap
                             + (wideIntervalLanguage ? 3 : 0)
                             + ((tensionProfile >= 6) ? 2 : 0)
                             + juce::roundToInt (4.0f * creativeRange.leapBias))
            : 0;
        if (effectiveMaxLeap > 0 && (!generated.empty() || barOffset > 0)
            && std::abs(note - previous) > effectiveMaxLeap)
        {
            int cand = note;
            while (cand - previous > effectiveMaxLeap && cand - 12 >= melLo) cand -= 12;
            while (previous - cand > effectiveMaxLeap && cand + 12 <= melHi) cand += 12;
            if (std::abs(cand - previous) > effectiveMaxLeap)
                cand = juce::jlimit(melLo, melHi,
                    snapToScale(previous + (note > previous ? effectiveMaxLeap : -effectiveMaxLeap)));
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

        // The macro plan also controls articulation direction:
        // peaks speak tighter while releases/returns get more air.
        if (composerState.sustainBias > 0.02f && (h % 100u) < 34u)
            len = juce::jmin (4, len + 1);
        else if (composerState.sustainBias < -0.02f && (h % 100u) < 28u)
            len = juce::jmax (1, len - 1);

        // Phrase tension affects articulation too: the peak uses shorter
        // fragments and the return bar allows more sustain.
        if (phraseRole == 2 && phraseTension > 0.58f && (h % 100u) < 42u)
            len = juce::jmax (1, len - 1);
        else if (phraseRole == 3 && phraseTension < 0.62f && (h % 100u) < 36u)
            len = juce::jmin (4, len + 1);

        // Sparse grooves leave more air; driving grooves connect pulses.
        if (rhythmType >= 4 && rhythmType <= 7 && (h % 100u) < (uint32_t)(25.0f * dnaGroove))
            len = juce::jmin(4, len + 1);
        if (rhythmType >= 14 && (h % 100u) < (uint32_t)(22.0f * dnaGroove))
            len = juce::jmax(1, len - 1);

        // Creative duration language: the same pitch contour can feel vocal,
        // percussive, declarative or flowing without adding random timing.
        switch (creativeRange.durationStyle)
        {
            case 0: break; // balanced
            case 1: if ((i & 1u) == 0) len = juce::jmin (4, len + 1); else len = juce::jmax (1, len - 1); break;
            case 2: if ((i % 3u) == 1u) len = juce::jmax (1, len - 1); else len = juce::jmin (4, len + 1); break;
            case 3: len = juce::jmin (4, len + 1); break; // flowing
            case 4: if (i == 0 || i + 1 == chosen.size()) len = juce::jmin (4, len + 1); break; // declarative
            case 5: if ((i % 4u) == 2u) len = juce::jmin (4, len + 1); else len = juce::jmax (1, len - 1); break;
            default: break;
        }

        if (creativeRange.durationContrast > 0.68f
            && (h % 100u) < 22u)
            len = (len <= 1 ? juce::jmin (4, len + 2) : juce::jmax (1, len - 1));

        len = juce::jmin(len, 16 - x);

        int velocity = 70 + (x % 4 == 0 ? 8 : 0);
        if (cycle == 2) velocity += 5;
        if (hook && (x == 0 || x == 8)) velocity += 4;
        velocity += juce::roundToInt (composerState.velocityLift * 78.0f);

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
        const float human = humanizeEnabled ? juce::jlimit(0.0f, 1.0f, humanize) : 0.0f;
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
            const float legatoAmt = juce::jlimit (0.0f, 1.0f,
                ((prof.legato < 0.0f) ? melodyLength : prof.legato) + tempoLegatoBoost);
            const int sustained = 1 + (int) std::round(legatoAmt * (float) (gap - 1));
            len = juce::jmax(len, juce::jmin(sustained, gap));
            len = juce::jmin(len, prof.maxLen);
            // Tempo Feel: at fast BPMs keep ordinary lead phrases readable by
            // capping occupancy. Long-register sound profiles (pads/strings)
            // can still exceed this cap through their explicit maxLen.
            if (! (soundTarget == 4 && prof.maxLen >= 12))
                len = juce::jmin (len, tempoLengthCap);
            len = juce::jmax(len, juce::jmin(prof.minLen, gap));
            len = juce::jlimit(1, juce::jmax(1, 16 - x), len);
        }
        // 0.82 Rhythm x Pitch Semantics:
        // The onset position now has a pitch function. Strong beats prefer stable
        // chord tones, weak short events prefer connective/color tones, and long
        // events prefer notes that can comfortably carry the harmony. This makes
        // the rhythm and pitch languages cooperate instead of being judged as two
        // unrelated streams.
        {
            const bool strongBeat = (x % 4) == 0;
            const bool backBeat = (x % 8) == 4;
            const int nextX = (i + 1 < chosen.size()) ? chosen[i + 1] : 16;
            const int gap = juce::jmax (1, nextX - x);
            const bool longEvent = len >= juce::jmax (3, juce::jmin (6, gap));
            const uint32_t semanticHash = hash32 (
                identitySeed ^ (uint32_t) (x * 193 + (int) i * 37 + 0x52A11CEu));
            const float semanticRoll =
                (float) (semanticHash % 1000u) / 1000.0f;

            auto nearestChordTone = [&] (int target) -> int
            {
                int best = target;
                int bestDistance = 1000;
                for (const int chordDegree : { degree, degree + 2, degree + 4 })
                {
                    const int base = pitchForDegree (chordDegree, octave);
                    for (int k = -2; k <= 2; ++k)
                    {
                        const int candidate = base + k * 12;
                        if (candidate < melLo || candidate > melHi)
                            continue;
                        if (snapToScale (candidate) != candidate)
                            continue;
                        const int distance = std::abs (candidate - target);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            best = candidate;
                        }
                    }
                }
                return juce::jlimit (melLo, melHi, snapToScale (best));
            };

            if ((strongBeat || longEvent) && semanticRoll < (strongBeat ? 0.72f : 0.58f))
            {
                const int anchor = nearestChordTone (note);
                const float blend = strongBeat ? 0.72f : 0.48f;
                note = juce::jlimit (
                    melLo, melHi,
                    snapToScale (juce::roundToInt (
                        (1.0f - blend) * (float) note + blend * (float) anchor)));
            }
            else if (! strongBeat && ! backBeat && gap <= 3 && ! generated.empty())
            {
                // Short weak events behave as connective tissue: move one scale
                // degree toward the next planned pitch, but avoid turning it into
                // a literal chord arpeggio.
                const int nextPlanned = (i + 1 < dPlan.size())
                    ? foldIntoLane (pitchForDegree (dPlan[i + 1], octave), melLo, melHi)
                    : note;
                const int direction = nextPlanned >= generated.back() ? 1 : -1;

                std::vector<int> candidates;
                const auto scale = scaleSemitones();
                for (int p = 0; p < 12; ++p)
                {
                    const int candidate = note + direction * p;
                    if (candidate < melLo || candidate > melHi)
                        continue;
                    if (snapToScale (candidate) != candidate)
                        continue;
                    if (chordTone (candidate))
                        continue;
                    candidates.push_back (candidate);
                }

                if (! candidates.empty() && semanticRoll < 0.72f)
                {
                    int best = candidates.front();
                    int bestDistance = std::abs (best - nextPlanned);
                    for (const int candidate : candidates)
                    {
                        const int distance = std::abs (candidate - nextPlanned);
                        if (distance < bestDistance)
                        {
                            best = candidate;
                            bestDistance = distance;
                        }
                    }
                    note = juce::jlimit (melLo, melHi,
                        snapToScale (juce::roundToInt (
                            0.62f * (float) note + 0.38f * (float) best)));
                }
            }

            // Semantic movement still obeys the sound-profile leap contract.
            if (! generated.empty())
            {
                const int priorNote = generated.back();
                const int semanticLeap = std::abs (note - priorNote);
                const int semanticMaxLeap = juce::jmin (
                    9,
                    prof.maxLeap > 0 ? juce::jmax (5, prof.maxLeap) : 7);
                if (semanticLeap > semanticMaxLeap)
                {
                    note = juce::jlimit (
                        melLo, melHi,
                        snapToScale (priorNote
                            + (note >= priorNote ? semanticMaxLeap : -semanticMaxLeap)));
                }
            }

            generated.back() = note;
            previous = note;
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
        const float chance = juce::jlimit (0.0f, 0.95f,
            fillAmount * 2.2f * (1.0f - 0.48f * fastTempo));
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

// -----------------------------------------------------------------------------
// 0.47 Human Phrase Engine
// -----------------------------------------------------------------------------
// Capture the first bar of a four-bar phrase as a compact melodic fingerprint.
// Later bars use that fingerprint as a compositional reference rather than
// independently re-rolling the motif. This creates A / A' / B / A'' behavior
// without making every bar a literal copy.
MidiForgeAudioProcessor::PhraseMotif
MidiForgeAudioProcessor::extractPhraseMotif (const Section& section, int phraseStartBar) const
{
    PhraseMotif motif;
    const int start = phraseStartBar * 16;
    const int end = start + 16;

    const NoteEvent* anchor = nullptr;
    for (const auto& n : section.notes)
    {
        if (n.channel == 3 && n.step >= start && n.step < end)
        {
            anchor = &n;
            break;
        }
    }
    if (anchor == nullptr)
        return motif;

    for (const auto& n : section.notes)
    {
        if (n.channel != 3 || n.step < start || n.step >= end)
            continue;
        motif.relativePitches.push_back (n.note - anchor->note);
        motif.relativeSteps.push_back (n.step - start);
        motif.lengths.push_back (n.length);
    }

    return motif;
}

void MidiForgeAudioProcessor::applyHumanPhraseRole (Section& section, int barOffset,
                                                    const PhraseMotif& motif) const
{
    if (motif.relativePitches.empty())
        return;

    const int role = barOffset & 3; // A, A', B, A''
    if (role == 0)
        return;

    const int start = barOffset * 16;
    const int end = start + 16;

    std::vector<NoteEvent*> current;
    for (auto& n : section.notes)
        if (n.channel == 3 && n.step >= start && n.step < end)
            current.push_back (&n);

    if (current.empty())
        return;

    std::sort (current.begin(), current.end(),
               [](const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

    const auto nearestMotifIndex = [&](size_t i) -> size_t
    {
        if (current.size() <= 1 || motif.relativePitches.size() <= 1)
            return 0;
        const double t = (double) i / (double) (current.size() - 1);
        return (size_t) juce::jlimit (
            0,
            (int) motif.relativePitches.size() - 1,
            juce::roundToInt (t * (double) (motif.relativePitches.size() - 1)));
    };

    const auto snapInMelodyLane = [&](int pitch) -> int
    {
        int lo = 62, hi = 86;
        melodyCoreLane (lo, hi);
        pitch = juce::jlimit (lo, hi, pitch);
        return juce::jlimit (lo, hi, snapToScale (pitch));
    };

    const int firstPitch = current.front()->note;

    for (size_t i = 0; i < current.size(); ++i)
    {
        NoteEvent& n = *current[i];
        const size_t mi = nearestMotifIndex (i);
        int target = firstPitch + motif.relativePitches[mi];

        if (role == 2)
        {
            // B = contrast. Invert the motif contour around its anchor and
            // push the middle toward a higher-tension register.
            target = firstPitch - motif.relativePitches[mi];
            if (i > 0 && i + 1 < current.size())
                target += (i & 1u) ? -2 : 3;
        }

        const float strength = (role == 1 ? 0.58f : role == 2 ? 0.22f : 0.72f);
        const int blended = juce::roundToInt ((float) n.note * (1.0f - strength)
                                              + (float) target * strength);
        n.note = snapInMelodyLane (blended);

        if (role == 1)
        {
            // A' keeps the identity but changes sustain/accent detail.
            if (mi < motif.lengths.size() && (i & 1u) == 0)
                n.length = juce::jlimit (1, 4,
                    juce::roundToInt (0.70f * (float) n.length
                                      + 0.30f * (float) motif.lengths[mi]));
            if ((i & 3u) == 1)
                n.velocity = juce::jlimit (40, 118, n.velocity + 4);
        }

        if (role == 2 && i == current.size() / 2)
        {
            // B gets the phrase peak instead of being louder everywhere.
            n.note = snapInMelodyLane (n.note + 3);
            n.velocity = juce::jlimit (40, 118, n.velocity + 7);
            n.length = juce::jmin (4, n.length + 1);
        }
    }

    if (role == 3)
    {
        // A'' gets an actual answer/cadence. Resolve the final melodic note
        // toward the current chord's root or third, choosing the closer option.
        NoteEvent& last = *current.back();
        const auto prog = progressionDegrees();
        if (!prog.empty())
        {
            const int degree = prog[(size_t) (barOffset % (int) prog.size())];
            int lo = 62, hi = 86;
            melodyCoreLane (lo, hi);

            auto inLane = [&](int p)
            {
                while (p < lo) p += 12;
                while (p > hi) p -= 12;
                return juce::jlimit (lo, hi, snapToScale (p));
            };

            const int root = inLane (degreeToPitch (degree, octave));
            const int third = inLane (degreeToPitch (degree + 2, octave));

            // 0.58.1: cadence is no longer mandatory on every loop. A controlled
            // minority of phrases ends on a tense scale tone and lets the loop
            // resolve on the next cycle instead of sounding permanently "nice".
            const uint32_t cadenceHash = hash32 (generationSeed
                ^ (uint32_t) (barOffset * 97 + melodyType * 31 + 0xCADA));
            const float unresolvedChance = juce::jlimit (0.10f, 0.36f,
                0.10f + 0.14f * dnaSurprise + 0.10f * ((cadenceHash >> 8) % 100u) / 100.0f);

            if ((float) (cadenceHash % 1000u) / 1000.0f < unresolvedChance)
            {
                int tension = last.note;
                int bestDist = 1000;
                for (int td : { degree + 1, degree + 3, degree + 6 })
                {
                    const int raw = degreeToPitch (td, octave);
                    for (int k = -2; k <= 2; ++k)
                    {
                        const int cand = inLane (raw + k * 12);
                        if (std::abs (cand - last.note) < bestDist)
                        {
                            bestDist = std::abs (cand - last.note);
                            tension = cand;
                        }
                    }
                }
                last.note = tension;
                last.length = juce::jlimit (1, 3, juce::jmax (last.length, 1));
            }
            else
            {
                last.note = (std::abs(root - last.note) <= std::abs(third - last.note)) ? root : third;
                last.length = juce::jlimit (2, 4, juce::jmax (last.length, 2));
            }
            last.velocity = juce::jlimit (40, 118, last.velocity + 3);
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

    if(!solo && (bar % 4) != 0)
    {
        const PhraseMotif phraseMotif = extractPhraseMotif(section, bar - (bar % 4));
        applyHumanPhraseRole(section, bar, phraseMotif);
    }
}
if(arpEnabled && !solo)
addArp(section,bar,deg,targetEnergy,r);
}

// 0.64: develop each complete four-bar phrase after all layers are known.
for (int phraseStart = 0; phraseStart + 3 < section.bars; phraseStart += 4)
{
    applyMotifDevelopment (section, phraseStart, variationSalt);
    applyMotifSemantics (section, phraseStart, variationSalt);
}

    applyLoopClosure (section, hash32 (generationSeed ^ (uint32_t) (variationSalt + 1) * 0x6A09E667u));
}

void MidiForgeAudioProcessor::applyMotifSemantics (Section& section,
                                                   int phraseStartBar,
                                                   int variationSalt) const
{
    if (! melodyEnabled || soundProfileFor (soundTarget).soloLine
        || phraseStartBar < 0 || phraseStartBar + 3 >= section.bars)
        return;

    const uint32_t identity = hash32 (
        generationSeed
        ^ (uint32_t) (phraseStartBar + 1) * 0x9e3779b9u
        ^ (uint32_t) (variationSalt + 1) * 0x85ebca6bu
        ^ 0x73A11CE5u);

    const auto plan = midiforge::MotifSemantics::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    auto collectBar = [&] (int bar)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < section.notes.size(); ++i)
        {
            const auto& n = section.notes[i];
            if (n.channel == 3 && n.step / 16 == bar)
                out.push_back (i);
        }

        std::stable_sort (out.begin(), out.end(),
            [&] (size_t a, size_t b)
            {
                if (section.notes[a].step != section.notes[b].step)
                    return section.notes[a].step < section.notes[b].step;
                return section.notes[a].note < section.notes[b].note;
            });
        return out;
    };

    const auto baseIndices = collectBar (phraseStartBar);
    if (baseIndices.size() < 2)
        return;

    std::vector<NoteEvent> base;
    base.reserve (baseIndices.size());
    for (const auto index : baseIndices)
        base.push_back (section.notes[index]);

    const int baseStart = phraseStartBar * 16;
    const int anchorPitch = base.front().note;

    int melodyLo = 40, melodyHi = 96, contractLeap = 9;
    melodyRegisterContract (melodyLo, melodyHi, contractLeap);

    auto safePitch = [&] (int pitch)
    {
        return juce::jlimit (melodyLo, melodyHi, snapToScale (pitch));
    };

    auto baseForNote = [&] (size_t i)
    {
        if (base.size() <= 1)
            return (size_t) 0;
        const float t = (float) i / (float) juce::jmax (1, (int) baseIndices.size() - 1);
        return (size_t) juce::jlimit (
            0, (int) base.size() - 1,
            juce::roundToInt (t * (float) (base.size() - 1)));
    };

    const auto rhythmPreserve = [&](int role)
    {
        const float roleBase = role == 1 ? 0.78f : role == 2 ? 0.34f : 0.84f;
        const float coreBias = 0.05f * (float) (plan.rhythmicCore % 4);
        return juce::jlimit (0.18f, 0.94f, roleBase + coreBias);
    };

    auto applyAxis = [&] (std::vector<size_t>& current, int axis, float amount, int role)
    {
        if (current.empty())
            return;

        const auto mappedBase = [&] (size_t i) -> const NoteEvent&
        {
            return base[baseForNote (i)];
        };

        switch (axis)
        {
            case 0: // Rhythmic Core
            {
                if (current.size() >= 2)
                {
                    size_t chosen = (size_t) juce::jlimit (
                        0, (int) current.size() - 1,
                        role == 2 ? (int) current.size() - 2 : (int) current.size() / 2);
                    auto& n = section.notes[current[chosen]];
                    const int dir = ((plan.rhythmicCore + role) & 1) ? 1 : -1;
                    n.step = juce::jlimit (0, section.bars * 16 - 1, n.step + dir);
                    n.length = juce::jlimit (
                        1, 8,
                        n.length + (((plan.rhythmicCore + (int) chosen) & 1) ? 1 : -1));
                }
                break;
            }

            case 1: // Interval Core
            {
                // current contains indices into section.notes, so its midpoint must be an
                // index into current itself. current.back() is a section.notes index and
                // becomes invalid when reused as current[chosen] for sparse bars.
                const size_t chosen = current.size() / 2;
                auto& n = section.notes[current[chosen]];
                const int intervalShape = plan.intervalCore % 4;
                const int delta = intervalShape == 0 ? 2
                                : intervalShape == 1 ? 3
                                : intervalShape == 2 ? 5
                                : -4;
                n.note = safePitch (n.note + ((chosen & 1u) ? -delta : delta));
                break;
            }

            case 2: // Starting Anchor
            {
                auto& n = section.notes[current.front()];
                static constexpr int anchorShifts[7] = { 0, 2, 4, -2, 7, -5, 9 };
                n.note = safePitch (anchorPitch + anchorShifts[plan.startingAnchor]);
                break;
            }

            case 3: // Peak Gesture
            {
                size_t peak = 0;
                for (size_t i = 1; i < current.size(); ++i)
                    if (section.notes[current[i]].note > section.notes[current[peak]].note)
                        peak = i;

                auto& n = section.notes[current[peak]];
                const int peakShift = 1 + (plan.peakGesture % 3) * 2;
                n.note = safePitch (n.note + ((plan.peakGesture & 1) ? -peakShift : peakShift));
                if (plan.peakGesture >= 4)
                    n.length = juce::jmin (6, n.length + 1);
                break;
            }

            case 4: // Ending Gesture
            {
                auto& n = section.notes[current.back()];
                const int bar = phraseStartBar + role;
                const auto prog = progressionDegrees();
                int target = n.note;

                if (!prog.empty())
                {
                    const int degree = prog[(size_t) (bar % (int) prog.size())];
                    const int root = degreeToPitch (degree, octave);
                    const int third = degreeToPitch (degree + 2, octave);

                    switch (plan.endingGesture)
                    {
                        case 0: target = root; break;
                        case 1: target = third; break;
                        case 2: target = juce::jmin (root, third) - 2; break;
                        case 3: target = anchorPitch + (anchorPitch - n.note); break;
                        case 4: target = n.note; n.length = juce::jmin (7, n.length + 2); break;
                        case 5: target = third + 2; break;
                        default: target = root + 2; break;
                    }
                }

                const float blend = role == 3 ? juce::jlimit (0.45f, 0.95f, amount)
                                              : juce::jlimit (0.20f, 0.70f, amount);
                n.note = safePitch (juce::roundToInt ((float) n.note * (1.0f - blend)
                                                    + (float) target * blend));
                break;
            }

            case 5: // Signature Leap
            {
                if (current.size() < 2)
                    break;

                const size_t chosen = current.size() / 2;
                const int prev = section.notes[current[chosen > 0 ? chosen - 1 : chosen]].note;
                const int next = section.notes[current[juce::jmin (chosen + 1, current.size() - 1)]].note;
                const int jump = 4 + (plan.signatureLeap % 3) * 2;
                const int direction = ((plan.signatureLeap + role) & 1) ? -1 : 1;
                const int target = ((prev + next) / 2) + direction * jump;
                section.notes[current[chosen]].note = safePitch (target);
                break;
            }

            default: // Answer Cell
            {
                if (current.size() < 2)
                    break;

                const size_t a = current.size() - 2;
                const size_t b = current.size() - 1;
                auto& first = section.notes[current[a]];
                auto& last = section.notes[current[b]];
                const int delta = last.note - first.note;

                switch (plan.answerCell)
                {
                    case 0: last.note = safePitch (first.note - delta); break;
                    case 1: last.note = safePitch (first.note + std::abs (delta)); break;
                    case 2: last.length = juce::jmax (1, last.length - 1); break;
                    case 3: last.length = juce::jmin (7, last.length + 1); break;
                    case 4: last.note = safePitch (last.note - 3); break;
                    case 5: last.note = safePitch (last.note + 3); break;
                    default:
                        last.note = safePitch (first.note + ((plan.answerCell & 1) ? 4 : -4));
                        break;
                }
                break;
            }
        }

        juce::ignoreUnused (mappedBase);
    };

    for (int role = 1; role <= 3; ++role)
    {
        auto current = collectBar (phraseStartBar + role);
        if (current.size() < 2)
            continue;

        const float preserve = rhythmPreserve (role);

        for (size_t i = 0; i < current.size(); ++i)
        {
            auto& n = section.notes[current[i]];
            const auto& src = base[baseForNote (i)];

            int targetPitch = anchorPitch + (src.note - anchorPitch);

            // B is the deliberate contrast cell: preserve the rhythm skeleton
            // less strongly and reverse part of the interval contour.
            if (role == 2)
            {
                targetPitch = anchorPitch - (src.note - anchorPitch);
                if ((i & 1u) != 0u)
                    targetPitch += (plan.answerCell & 1) ? 2 : -2;
            }

            const float pitchBlend = role == 1 ? 0.74f
                                    : role == 2 ? 0.36f
                                    : 0.86f;

            n.note = safePitch (juce::roundToInt (
                (float) n.note * (1.0f - pitchBlend)
                + (float) targetPitch * pitchBlend));

            const int targetStep = baseStart + role * 16 + src.step - baseStart;
            n.step = juce::jlimit (
                role * 16 + baseStart,
                role * 16 + baseStart + 15,
                juce::roundToInt (
                    (float) n.step * (1.0f - preserve)
                    + (float) targetStep * preserve));

            if (role != 2 && ((plan.rhythmicCore + (int) i) & 3) == 0)
                n.length = juce::jlimit (
                    1, 8,
                    juce::roundToInt ((float) n.length * 0.78f + (float) src.length * 0.22f));
        }

        if (role == 1)
        {
            // A' changes one semantic component while leaving the motif core
            // recognisable.
            applyAxis (current, plan.primaryMutation, plan.mutationStrength, role);
        }
        else if (role == 2)
        {
            // B deliberately mutates a second semantic component and increases
            // contrast without abandoning the original idea.
            applyAxis (current, plan.primaryMutation,
                       juce::jlimit (0.42f, 0.92f, plan.contrastStrength), role);
            applyAxis (current, plan.secondaryMutation,
                       juce::jlimit (0.30f, 0.80f, plan.contrastStrength * 0.82f), role);
        }
        else
        {
            // A'' restores the semantic core and gives the loop a fresh ending
            // gesture instead of cloning A literally.
            applyAxis (current, 4, plan.returnStrength, role);
            if (plan.primaryMutation == 0)
                applyAxis (current, 0, 0.34f, role);
        }
    }

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}

float MidiForgeAudioProcessor::motifSemanticsScore (const Section& section,
                                                    uint32_t identity) const
{
    if (section.bars < 4)
        return 0.55f;

    auto collect = [&] (int bar)
    {
        std::vector<const NoteEvent*> out;
        for (const auto& n : section.notes)
            if (n.channel == 3 && n.step / 16 == bar)
                out.push_back (&n);
        std::stable_sort (out.begin(), out.end(),
            [] (const NoteEvent* a, const NoteEvent* b)
            {
                if (a->step != b->step) return a->step < b->step;
                return a->note < b->note;
            });
        return out;
    };

    const auto a = collect (0);
    const auto ap = collect (1);
    const auto b = collect (2);
    const auto app = collect (3);
    if (a.size() < 2 || ap.size() < 2 || b.size() < 2 || app.size() < 2)
        return 0.35f;

    const auto similarity = [] (const std::vector<const NoteEvent*>& x,
                                const std::vector<const NoteEvent*>& y)
    {
        const size_t n = juce::jmin (x.size(), y.size());
        if (n == 0)
            return 0.0f;

        const int xa = x.front()->note;
        const int ya = y.front()->note;
        int onset = 0, pitch = 0;

        for (size_t i = 0; i < n; ++i)
        {
            if (std::abs ((x[i]->step % 16) - (y[i]->step % 16)) <= 1)
                ++onset;
            if (std::abs ((x[i]->note - xa) - (y[i]->note - ya)) <= 2)
                ++pitch;
        }

        const float countFit = 1.0f - juce::jlimit (
            0.0f, 1.0f,
            (float) std::abs ((int) x.size() - (int) y.size()) / 5.0f);

        return juce::jlimit (0.0f, 1.0f,
            0.42f * (float) onset / (float) n
            + 0.42f * (float) pitch / (float) n
            + 0.16f * countFit);
    };

    const auto plan = midiforge::MotifSemantics::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    const float aPrime = similarity (a, ap);
    const float contrast = 1.0f - similarity (a, b);
    const float returnFit = similarity (a, app);

    float endingFit = 0.5f;
    const int endingDelta = app.back()->note - a.back()->note;
    switch (plan.endingGesture)
    {
        case 0: endingFit = endingDelta <= 1 ? 0.9f : 0.55f; break;
        case 1: endingFit = endingDelta >= -2 ? 0.82f : 0.48f; break;
        case 2: endingFit = endingDelta < 0 ? 0.88f : 0.45f; break;
        case 3: endingFit = endingDelta != 0 ? 0.76f : 0.42f; break;
        case 4: endingFit = std::abs (app.back()->length - a.back()->length) >= 1 ? 0.84f : 0.55f; break;
        default: endingFit = 0.68f;
    }

    return juce::jlimit (0.0f, 1.0f,
        0.30f * aPrime
        + 0.30f * contrast
        + 0.28f * returnFit
        + 0.12f * endingFit);
}


void MidiForgeAudioProcessor::applyLoopClosure (Section& section, uint32_t identity) const
{
    if (! melodyEnabled || soundProfileFor (soundTarget).soloLine
        || section.bars < 2)
        return;

    std::vector<size_t> melody;
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.size() < 3)
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    const auto plan = midiforge::LoopClosure::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    const int totalSteps = section.bars * 16;
    const int finalBarStart = (section.bars - 1) * 16;

    std::vector<size_t> head;
    std::vector<size_t> tail;

    for (const auto index : melody)
    {
        const auto& n = section.notes[index];
        if (n.step < 32)
            head.push_back (index);
        if (n.step >= finalBarStart)
            tail.push_back (index);
    }

    if (head.size() < 2 || tail.empty())
        return;

    const size_t lastIndex = tail.back();
    const size_t previousIndex = tail.size() >= 2 ? tail[tail.size() - 2] : tail.back();

    int contractLo = 40, contractHi = 96, contractLeap = 9;
    melodyRegisterContract (contractLo, contractHi, contractLeap);
    auto safeMelodyPitch = [&] (int pitch)
    {
        return juce::jlimit (contractLo, contractHi, snapToScale (pitch));
    };

    const int firstPitch = section.notes[head.front()].note;
    const int openingDelta = section.notes[head[1]].note - firstPitch;
    const int lastPitch = section.notes[lastIndex].note;

    auto nearest = [] (int a, int b, int value)
    {
        return std::abs (a - value) <= std::abs (b - value) ? a : b;
    };

    int target = firstPitch;

    const auto prog = progressionDegrees();
    if (plan.targetStrategy == 0)
    {
        target = firstPitch;
    }
    else if (plan.targetStrategy == 1)
    {
        target = firstPitch - openingDelta;
    }
    else if (plan.targetStrategy == 2)
    {
        target = firstPitch + openingDelta;
    }
    else if (plan.targetStrategy >= 3 && ! prog.empty())
    {
        const int degree = prog[(size_t) ((section.bars - 1) % (int) prog.size())];
        const int root = degreeToPitch (degree, octave);
        const int third = degreeToPitch (degree + 2, octave);
        const int fifth = degreeToPitch (degree + 4, octave);

        if (plan.targetStrategy == 3)
            target = nearest (root, third, lastPitch);
        else if (plan.targetStrategy == 4)
            target = fifth;
        else
            target = firstPitch + ((openingDelta >= 0) ? -2 : 2);
    }

    // Bridge styles alter how strongly the end points back toward the opening.
    float blend = plan.returnStrength;
    if (plan.bridgeStyle == 1) // answer
        target = firstPitch - openingDelta;
    else if (plan.bridgeStyle == 2) // pickup
        blend *= 0.92f;
    else if (plan.bridgeStyle == 3) // sustain / release
        blend *= 0.84f;
    else if (plan.bridgeStyle == 4) // unresolved
        blend *= 0.62f;
    else if (plan.bridgeStyle == 5) // deceptive
        blend *= 0.72f;

    const uint32_t seamHash = hash32 (
        identity ^ (uint32_t) section.bars * 0x45d9f3bu ^ 0xC1045EAu);
    const float unresolvedRoll = (float) (seamHash % 1000u) / 1000.0f;

    if (unresolvedRoll < plan.unresolvedBias && ! prog.empty())
    {
        const int degree = prog[(size_t) ((section.bars - 1) % (int) prog.size())];
        int tensionTargets[3] =
        {
            degree + 1,
            degree + 3,
            degree + 6
        };

        int best = lastPitch;
        int bestDistance = 1000;
        for (const int degreeOffset : tensionTargets)
        {
            const int raw = degreeToPitch (degreeOffset, octave);
            for (int k = -2; k <= 2; ++k)
            {
                const int candidate = safeMelodyPitch (raw + 12 * k);
                const int distance = std::abs (candidate - lastPitch);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }
        }

        target = best;
        blend *= 0.74f;
    }

    const int blended = juce::roundToInt (
        (float) lastPitch * (1.0f - blend)
        + (float) target * blend);

    auto& last = section.notes[lastIndex];
    last.note = safeMelodyPitch (blended);

    if (plan.pickupBias > 0.28f
        && plan.pickupStyle != 0
        && last.step > finalBarStart + 1)
    {
        const bool hasSpaceBefore = previousIndex == lastIndex
            || last.step > section.notes[previousIndex].step + 1;

        if (hasSpaceBefore)
        {
            if (plan.pickupStyle == 1 || plan.pickupStyle == 3 || plan.pickupStyle == 4)
                last.step = juce::jmax (finalBarStart, last.step - 1);

            if (plan.pickupStyle == 2 && last.step < totalSteps - 1)
                last.step = juce::jmin (totalSteps - 1, last.step + 1);
        }
    }

    if (plan.releaseStyle == 1 || plan.bridgeStyle == 3)
    {
        last.length = juce::jmin (16 - (last.step % 16),
                                  juce::jmax (last.length, 2));
        last.length = juce::jmin (last.length,
                                  juce::jmax (1, totalSteps - last.step));
    }
    else if (plan.releaseStyle == 2)
    {
        last.length = juce::jmax (1, juce::jmin (last.length, 2));
    }
    else if (plan.releaseStyle == 3 && previousIndex != lastIndex)
    {
        section.notes[previousIndex].length =
            juce::jmax (1, juce::jmin (section.notes[previousIndex].length, 2));
    }

    // A tiny seam accent makes the final event feel intentional without
    // simply turning every loop ending into the same velocity peak.
    if (plan.bridgeStyle == 0 || plan.bridgeStyle == 1)
        last.velocity = juce::jlimit (40, 118, last.velocity + 2);
    else
        last.velocity = juce::jlimit (40, 118, last.velocity - 2);

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}

float MidiForgeAudioProcessor::loopClosureScore (const Section& section, uint32_t identity) const
{
    std::vector<const NoteEvent*> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    if (section.bars < 2 || melody.size() < 3)
        return 0.48f;

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    const NoteEvent* first = melody.front();
    const NoteEvent* second = melody.size() > 1 ? melody[1] : melody.front();

    const NoteEvent* last = nullptr;
    const NoteEvent* previous = nullptr;
    for (auto it = melody.rbegin(); it != melody.rend(); ++it)
    {
        if ((*it)->step / 16 == section.bars - 1)
        {
            if (last == nullptr)
                last = *it;
            else
            {
                previous = *it;
                break;
            }
        }
    }

    if (last == nullptr)
        return 0.36f;

    if (previous == nullptr)
        previous = last;

    const auto plan = midiforge::LoopClosure::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    const int seamDistance = std::abs (last->note - first->note);
    const float seamFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        (float) std::max (0, seamDistance - 2) / 18.0f);

    const int openingDelta = second->note - first->note;
    const int endingDelta = last->note - previous->note;

    float gestureFit = 0.5f;
    if (openingDelta == 0 || endingDelta == 0)
        gestureFit = 0.58f;
    else
    {
        const bool opposite = (openingDelta > 0) != (endingDelta > 0);
        const bool same = (openingDelta > 0) == (endingDelta > 0);
        gestureFit = opposite ? 0.92f : same ? 0.62f : 0.50f;

        if (plan.bridgeStyle == 1)
            gestureFit = juce::jmax (gestureFit, opposite ? 0.96f : 0.42f);
    }

    const auto prog = progressionDegrees();
    float harmonyFit = 0.55f;
    if (! prog.empty())
    {
        const int degree = prog[(size_t) ((section.bars - 1) % (int) prog.size())];
        const int root = degreeToPitch (degree, octave);
        const int third = degreeToPitch (degree + 2, octave);
        const int fifth = degreeToPitch (degree + 4, octave);
        const int rootDistance = std::abs (last->note - root);
        const int thirdDistance = std::abs (last->note - third);
        const int fifthDistance = std::abs (last->note - fifth);
        const int best = std::min (rootDistance, std::min (thirdDistance, fifthDistance));
        harmonyFit = 1.0f - juce::jlimit (0.0f, 1.0f,
            (float) std::max (0, best - 1) / 12.0f);
    }

    const int tailGap = juce::jmax (0, section.bars * 16 - (last->step + last->length));
    const float seamRelease = tailGap <= 1 ? 0.94f
        : tailGap <= 3 ? 0.78f
        : 0.58f;

    const float resolvedPenalty = plan.unresolvedBias > 0.28f && harmonyFit > 0.90f
        ? 0.10f : 0.0f;

    return juce::jlimit (0.0f, 1.0f,
        0.34f * seamFit
        + 0.28f * gestureFit
        + 0.22f * harmonyFit
        + 0.16f * seamRelease
        - resolvedPenalty);
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
if (humanizeEnabled)
    applyHumanPerformance(sec);
song.sections.push_back(std::move(sec));
}
}
void MidiForgeAudioProcessor::applyHumanPerformance (Section& section) const
{
    if (!humanizeEnabled || humanize <= 0.001f)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    std::stable_sort (melody.begin(), melody.end(),
                      [&] (size_t a, size_t b)
                      {
                          return section.notes[a].step < section.notes[b].step;
                      });

    if (melody.size() < 2)
        return;

    const float amount = juce::jlimit (0.0f, 1.0f, humanize);

    // Humanize is deliberately phrase-aware and opt-in. It does not randomize
    // every note: only selected anchors, pickups and resolutions are allowed to
    // move by one 16th step, preserving the electronic grid while breaking
    // machine-like repetition.
    for (size_t k = 0; k < melody.size(); ++k)
    {
        auto& cur = section.notes[melody[k]];
        const int barStart = (cur.step / 16) * 16;
        const int barEnd = barStart + 16;
        const int prevStep = (k > 0) ? section.notes[melody[k - 1]].step : cur.step;
        const int nextStep = (k + 1 < melody.size()) ? section.notes[melody[k + 1]].step : barEnd;

        const uint32_t h = hash32 (generationSeed
                                    ^ (uint32_t) (cur.step * 131 + cur.note * 17 + (int) k * 53)
                                    ^ 0x6B8B4567u);
        const float roll = (float) (h % 1000u) / 1000.0f;

        int move = 0;

        // Strong-beat anticipation: the player occasionally arrives one 16th
        // early when there is enough space after the previous note.
        const float anticipationChance = 0.22f * amount;
        if (cur.step > barStart && (cur.step % 4) == 0
            && nextStep > cur.step + 1
            && cur.step > prevStep + 1
            && roll < anticipationChance)
        {
            move = -1;
        }
        else
        {
            // Delayed resolution: phrase-ending/offbeat notes may lean late
            // instead of every note receiving the same timing error.
            const bool phraseRelease = ((cur.step % 16) >= 12)
                                     || (k + 1 == melody.size());
            const float delayChance = (phraseRelease ? 0.18f : 0.08f) * amount;
            if (phraseRelease && cur.step < barEnd - 1
                && nextStep > cur.step + 1
                && roll > 0.72f
                && roll < 0.72f + delayChance)
                move = 1;
        }

        if (move != 0)
        {
            const int candidate = juce::jlimit (barStart, barEnd - 1, cur.step + move);
            if (candidate > prevStep && candidate < nextStep)
                cur.step = candidate;
        }

        // Repeated notes are treated as a phrase pulse rather than identical
        // machine hits: alternate a little of their velocity and release.
        if (k > 0)
        {
            const auto& prev = section.notes[melody[k - 1]];
            if (prev.note == cur.note)
            {
                const bool lighterRepeat = ((h >> 9) & 1u) != 0u;
                cur.velocity = juce::jlimit (40, 118,
                    cur.velocity + (lighterRepeat ? -5 : 3));
                if (lighterRepeat)
                    cur.length = juce::jmax (1, cur.length - 1);
            }
        }

        // Human performance also slightly reshapes sustain, but never beyond
        // the next onset or the sound profile's musical boundaries.
        if ((h % 1000u) < (uint32_t) juce::roundToInt (90.0f * amount)
            && cur.length < 4)
        {
            const int available = juce::jmax (1, nextStep - cur.step);
            cur.length = juce::jmin (cur.length + 1, available);
        }
    }

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}

void MidiForgeAudioProcessor::applyMotifDevelopment (Section& section, int phraseStartBar, int variationSalt) const
{
    // 0.64 Motif Development Engine:
    // Turn the four-bar identity into a controlled A -> A' -> B -> A'' arc.
    // This is a structural pass, not a new random generator: rhythm/pitch
    // fingerprints remain anchored to the first bar while each later role gets
    // a deliberate development grammar.
    if (!melodyEnabled || soundProfileFor(soundTarget).soloLine
        || phraseStartBar < 0 || phraseStartBar + 3 >= section.bars)
        return;

    const PhraseMotif motif = extractPhraseMotif (section, phraseStartBar);
    if (motif.relativePitches.size() < 2)
        return;

    auto mix32 = [] (uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };

    enum DevelopmentStrategy
    {
        RepeatAlter = 0,
        RhythmicReduction,
        RhythmicExpansion,
        IntervalExpansion,
        Inversion,
        Fragmentation,
        CallResponse,
        Return
    };

    const uint32_t h = mix32 (generationSeed
                              ^ (uint32_t) (variationSalt + 1) * 0x9e3779b9u
                              ^ (uint32_t) (phraseStartBar + 1) * 0x85ebca6bu);

    // 0.65 Contextual Development: the grammar is selected from the phrase
    // state instead of a flat random 1-in-8 roll. This keeps the eight
    // development languages available, but asks the phrase what it currently
    // needs: more identity, more motion, more contrast, or a stronger return.
    const auto phraseState = melodyFeatures (section, h);
    std::array<float, 8> strategyWeight {};
    strategyWeight[RepeatAlter] = 0.60f
        + 1.10f * (1.0f - phraseState.motifIdentity)
        + 0.45f * (1.0f - phraseState.phraseMemory);
    strategyWeight[RhythmicReduction] = 0.52f
        + 2.00f * juce::jmax (0.0f, phraseState.density - 0.62f)
        + 0.45f * juce::jmax (0.0f, 0.42f - phraseState.space);
    strategyWeight[RhythmicExpansion] = 0.50f
        + 2.10f * juce::jmax (0.0f, 0.38f - phraseState.density)
        + 1.10f * juce::jmax (0.0f, 0.24f - phraseState.rhythmIdentity);
    strategyWeight[IntervalExpansion] = 0.48f
        + 1.70f * juce::jmax (0.0f, 0.16f - phraseState.leap)
        + 0.70f * juce::jmax (0.0f, 0.20f - phraseState.surprise);
    strategyWeight[Inversion] = 0.44f
        + 0.90f * juce::jmax (0.0f, 0.32f - phraseState.contour)
        + 0.35f * juce::jmax (0.0f, 0.34f - phraseState.variety);
    strategyWeight[Fragmentation] = 0.46f
        + 1.15f * juce::jmax (0.0f, 0.46f - phraseState.repetition)
        + 0.65f * juce::jmax (0.0f, phraseState.density - 0.58f);
    strategyWeight[CallResponse] = 0.54f
        + 1.20f * juce::jmax (0.0f, 0.56f - phraseState.phraseArc)
        + 1.05f * juce::jmax (0.0f, 0.44f - phraseState.tensionArc);
    strategyWeight[Return] = 0.50f
        + 0.95f * juce::jmax (0.0f, 0.48f - phraseState.loopQuality)
        + 0.90f * juce::jmax (0.0f, 0.48f - phraseState.tensionArc)
        + 0.45f * juce::jmax (0.0f, phraseState.seam - 0.82f);

    int selectedStrategy = 0;
    float bestStrategyScore = -1.0e9f;
    for (int strategyIndex = 0; strategyIndex < 8; ++strategyIndex)
    {
        const float jitter = 0.035f * (float)
            ((mix32 (h ^ (uint32_t) (strategyIndex * 0x45d9f3bu)) % 1000u) / 1000.0f);
        const float score = strategyWeight[(size_t) strategyIndex] + jitter;
        if (score > bestStrategyScore)
        {
            bestStrategyScore = score;
            selectedStrategy = strategyIndex;
        }
    }
    const auto strategy = (DevelopmentStrategy) selectedStrategy;

    auto collectBar = [&] (int bar)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < section.notes.size(); ++i)
        {
            const auto& n = section.notes[i];
            if (n.channel == 3 && n.step / 16 == bar)
                out.push_back (i);
        }

        std::stable_sort (out.begin(), out.end(),
            [&] (size_t a, size_t b)
            {
                if (section.notes[a].step != section.notes[b].step)
                    return section.notes[a].step < section.notes[b].step;
                return section.notes[a].note < section.notes[b].note;
            });
        return out;
    };

    // Harmonic Intelligence 2.0: development targets are blended toward the
    // actual chord voicing of the destination bar. It is a soft pull, so B can
    // still create tension instead of becoming a chord-arpeggio rewrite.
    auto nearestChordTone = [&] (int bar, int pitch) -> int
    {
        int best = pitch;
        int bestDistance = 1000;
        for (const auto& n : section.notes)
        {
            if (n.channel != 1 || n.step / 16 != bar)
                continue;
            for (int oct = -3; oct <= 3; ++oct)
            {
                const int candidate = n.note + 12 * oct;
                if (candidate < 30 || candidate > 108)
                    continue;
                const int distance = std::abs (candidate - pitch);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }
        }
        return best;
    };

    // Structural rhythm development: reduce or fragment B first. Then the pitch
    // pass below operates on the final note list, keeping every transformation
    // internally coherent.
    if (strategy == RhythmicReduction || strategy == Fragmentation)
    {
        for (int role = 1; role <= 2; ++role)
        {
            const int bar = phraseStartBar + role;
            auto notes = collectBar (bar);
            if (notes.size() <= 3)
                continue;

            const size_t originalCount = notes.size();
            std::vector<size_t> toErase;
            for (size_t k = 0; k < originalCount; ++k)
            {
                const bool reduce = strategy == RhythmicReduction && (k % 3u) == 1u;
                const bool fragment = strategy == Fragmentation
                                   && k >= ((originalCount + 1u) / 2u);
                const size_t minimum = strategy == Fragmentation ? 2u : 3u;

                if ((reduce || fragment)
                    && originalCount - toErase.size() > minimum)
                    toErase.push_back (notes[k]);
            }

            for (size_t k = toErase.size(); k-- > 0; )
                section.notes.erase (section.notes.begin() + (long long) toErase[k]);
        }
    }

    // Rhythmic expansion adds at most one connective note to A' and B.
    if (strategy == RhythmicExpansion)
    {
        for (int role = 1; role <= 2; ++role)
        {
            const int bar = phraseStartBar + role;
            const auto notes = collectBar (bar);
            if (notes.size() < 2)
                continue;

            for (size_t k = 0; k + 1 < notes.size(); ++k)
            {
                const auto& a = section.notes[notes[k]];
                const auto& b = section.notes[notes[k + 1]];
                if (b.step - a.step < 3)
                    continue;

                const int step = a.step + (b.step - a.step) / 2;
                const int rawPitch = juce::roundToInt (0.5f * ((float) a.note + (float) b.note));
                const int note = juce::jlimit (30, 108, snapToScale (rawPitch));
                const int velocity = juce::jlimit (44, 112, (a.velocity + b.velocity) / 2 - 3);
                section.notes.push_back ({ step, 1, note, velocity, 3, false });
                break;
            }
        }
    }

    for (int role = 1; role <= 3; ++role)
    {
        auto notes = collectBar (phraseStartBar + role);
        if (notes.empty())
            continue;

        const int firstNote = section.notes[notes.front()].note;
        const float roleBlend = role == 1 ? 0.64f : role == 2 ? 0.86f : 0.74f;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& cur = section.notes[notes[i]];
            const size_t src = i % motif.relativePitches.size();
            const int refRel = motif.relativePitches[src];
            const int rawCurrent = cur.note;

            float rel = (float) refRel;
            switch (strategy)
            {
                case RepeatAlter:
                    break;

                case RhythmicReduction:
                    rel *= (role == 2 ? 0.92f : 1.0f);
                    break;

                case RhythmicExpansion:
                    rel *= (role == 2 ? 1.08f : 1.0f);
                    break;

                case IntervalExpansion:
                    rel *= (role == 1 ? 1.22f : role == 2 ? 1.48f : 1.10f);
                    break;

                case Inversion:
                    rel = -rel;
                    if (role == 2)
                        rel += ((i & 1u) == 0u ? 2.0f : -2.0f);
                    break;

                case Fragmentation:
                    rel *= (role == 2 ? 0.82f : 1.0f);
                    break;

                case CallResponse:
                    if (role == 2)
                        rel += 4.0f;
                    else if (role == 3)
                        rel *= 0.94f;
                    break;

                case Return:
                    rel *= (role == 1 ? 0.96f : role == 2 ? 0.76f : 1.0f);
                    break;
            }

            int desired = firstNote + juce::roundToInt (rel);

            if (strategy == CallResponse && role == 1 && i + 1 == notes.size())
                desired -= 3;
            if (strategy == CallResponse && role == 2 && i + 1 == notes.size())
                desired += 5;
            if (strategy == RepeatAlter && role == 1 && i == notes.size() / 2)
                desired += ((h & 1u) != 0u ? 2 : -2);
            if (strategy == Return && role == 2 && i == 0)
                desired += ((h & 2u) != 0u ? 3 : -3);
            if (strategy == Fragmentation && role == 3 && i == notes.size() / 2)
                desired += 2;

            const int activeBar = phraseStartBar + role;
            const int chordNear = nearestChordTone (activeBar, desired);
            const float harmonyBlend = role == 3 ? 0.42f : role == 2 ? 0.16f : 0.28f;
            desired = juce::roundToInt (
                (float) desired * (1.0f - harmonyBlend)
                + (float) chordNear * harmonyBlend);

            // Cadence-aware release: the last note of A'' points toward the
            // loop-start harmony, but keeps enough of its developed contour to
            // avoid a hard "always land on root" formula.
            if (role == 3 && i + 1 == notes.size())
            {
                const int loopTarget = nearestChordTone (activeBar, rootAtBar (phraseStartBar));
                desired = juce::roundToInt (
                    0.42f * (float) desired + 0.58f * (float) loopTarget);
            }

            desired = juce::jlimit (30, 108, snapToScale (desired));
            const int blended = juce::roundToInt (
                (float) rawCurrent * (1.0f - roleBlend)
                + (float) desired * roleBlend);
            cur.note = juce::jlimit (30, 108, snapToScale (blended));
        }

        // Phrase roles also get a small articulation arc: question bar tightens,
        // B has room to breathe, and A'' restores the release.
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& cur = section.notes[notes[i]];
            if (role == 1 && (i & 1u) != 0u)
                cur.length = juce::jmax (1, cur.length - 1);
            else if (role == 2 && cur.length < 3 && i + 1 == notes.size())
                cur.length = juce::jmin (4, cur.length + 1);
            else if (role == 3 && i + 1 == notes.size())
                cur.length = juce::jmin (4, juce::jmax (2, cur.length + 1));
        }
    }

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}


MidiForgeAudioProcessor::MelodyFeatures MidiForgeAudioProcessor::melodyFeatures (const Section& sec, uint32_t identity) const
{

        
        MelodyFeatures f;
        std::vector<const NoteEvent*> m;
        for (const auto& n : sec.notes)
            if (n.channel == 3) m.push_back(&n);

        const int totalSteps = juce::jmax(1, sec.bars * 16);
        f.density = juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 6));
        f.space = 1.0f - juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 9));

        // 0.58.3 Context Judge: reward a melody that occupies useful gaps,
        // shares a few meaningful accents with the backing, and stays out of
        // the same register as dense chord voicings.
        {
            std::array<float, 16> occupied {};
            float chordCenter = 0.0f, chordWeight = 0.0f;

            for (const auto& n : sec.notes)
            {
                if (n.channel == 3 || n.step / 16 < 0 || n.step / 16 >= sec.bars)
                    continue;

                const int start = juce::jlimit (0, sec.bars * 16 - 1, n.step);
                const int end = juce::jmin (sec.bars * 16, start + juce::jmax (1, n.length));
                for (int step = start; step < end; ++step)
                {
                    const int local = step % 16;
                    const float w = n.channel == 1 ? 0.46f
                                  : n.channel == 2 ? 0.28f
                                  : n.channel == 4 ? 0.16f
                                                    : n.channel == 5 ? 0.10f : 0.04f;
                    occupied[(size_t) local] = juce::jlimit (0.0f, 1.0f,
                        occupied[(size_t) local] + w);

                    if (n.channel == 1)
                    {
                        chordCenter += (float) n.note * (float) juce::jmax (1, n.length);
                        chordWeight += (float) juce::jmax (1, n.length);
                    }
                }
            }

            int shared = 0;
            int open = 0;
            for (auto* n : m)
            {
                const float occ = occupied[(size_t) (n->step % 16)];
                if (occ >= 0.40f) ++shared;
                if (occ <= 0.18f) ++open;
            }

            const float syncRatio = m.empty() ? 0.0f : (float) shared / (float) m.size();
            const float gapRatio = m.empty() ? 0.0f : (float) open / (float) m.size();
            const float targetSync =
                (melodyType == HookMelody || melodyType == VocalLikeMelody) ? 0.48f
                : (melodyType == SparseLeadMelody ? 0.28f : 0.39f);
            const float syncFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (syncRatio - targetSync) / 0.48f);
            const float gapTarget = (melodyType == SparseLeadMelody || genre == Ambient) ? 0.58f : 0.44f;
            const float gapFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (gapRatio - gapTarget) / 0.52f);

            float registerFit = 0.62f;
            if (chordWeight > 0.0f && !m.empty())
            {
                chordCenter /= chordWeight;
                float melodyMean = 0.0f;
                for (auto* n : m) melodyMean += (float) n->note;
                melodyMean /= (float) m.size();
                const float separation = std::abs (melodyMean - chordCenter);
                registerFit = juce::jlimit (0.0f, 1.0f, separation / 14.0f);
            }

            f.context = juce::jlimit (0.0f, 1.0f,
                0.45f * syncFit + 0.35f * gapFit + 0.20f * registerFit);
        }

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

        // 0.58.4 Phrase Tension Judge: estimate tension per bar from
        // non-chord destinations, offbeat activity and interval pressure, then
        // compare the loop against the intended A/A'/B/A'' curve.
        if (m.size() >= 4 && sec.bars >= 4)
        {
            float judgeMoodTension = 0.0f;
            switch (mood)
            {
                case DarkMood:        judgeMoodTension =  0.24f; break;
                case MelancholicMood: judgeMoodTension =  0.18f; break;
                case EuphoricMood:    judgeMoodTension = -0.08f; break;
                case AggressiveMood:  judgeMoodTension =  0.12f; break;
                case DreamyMood:      judgeMoodTension =  0.04f; break;
                case NostalgicMood:   judgeMoodTension =  0.10f; break;
                case MysteriousMood:  judgeMoodTension =  0.22f; break;
                case EnergeticMood:   judgeMoodTension = -0.02f; break;
                default:              judgeMoodTension =  0.0f;  break;
            }

            std::array<float, 4> barTension {};
            std::array<int, 4> barCount {};
            std::array<std::vector<int>, 4> chordPcs;

            for (const auto& n : sec.notes)
            {
                const int b = juce::jlimit (0, 3, n.step / 16);
                if (n.channel == 1)
                {
                    const int pc = (n.note % 12 + 12) % 12;
                    if (std::find (chordPcs[(size_t) b].begin(),
                                   chordPcs[(size_t) b].end(), pc) == chordPcs[(size_t) b].end())
                        chordPcs[(size_t) b].push_back (pc);
                }
            }

            std::vector<const NoteEvent*> sortedMelody = m;
            std::sort (sortedMelody.begin(), sortedMelody.end(),
                       [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

            for (size_t i = 0; i < sortedMelody.size(); ++i)
            {
                const auto* n = sortedMelody[i];
                const int b = juce::jlimit (0, 3, n->step / 16);
                const int pc = (n->note % 12 + 12) % 12;
                bool chordTone = false;
                for (const int chordPc : chordPcs[(size_t) b])
                    if (pc == chordPc) { chordTone = true; break; }

                float t = chordTone ? 0.18f : 0.72f;
                if ((n->step % 4) != 0) t += 0.10f;
                if (i > 0 && std::abs (n->note - sortedMelody[i - 1]->note) >= 5)
                    t += 0.14f;
                if (n->step % 16 >= 12)
                    t += 0.05f;

                barTension[(size_t) b] += juce::jlimit (0.0f, 1.0f, t);
                ++barCount[(size_t) b];
            }

            const float targets[4] = { 0.24f, 0.42f, 0.78f, 0.50f };
            float error = 0.0f;
            int compared = 0;
            for (int b = 0; b < 4; ++b)
            {
                if (barCount[(size_t) b] == 0) continue;
                const float actual = barTension[(size_t) b] / (float) barCount[(size_t) b];
                const float target = juce::jlimit (0.08f, 0.94f,
                    targets[b] + 0.10f * judgeMoodTension + 0.08f * dnaSurprise);
                error += std::abs (actual - target);
                ++compared;
            }

            f.tensionArc = compared > 0
                ? 1.0f - juce::jlimit (0.0f, 1.0f, (error / (float) compared) / 0.42f)
                : 0.50f;
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
        // 0.52 Loop Quality 2.0: judge the loop as one circular musical object.
        // This layer looks beyond isolated notes: strong candidates should have
        // a convincing seam, recurring identity, controlled contrast, useful
        // density movement and healthy layer interplay.
        if (!m.empty())
        {
            const int barsN = juce::jmax (1, sec.bars);
            const int totalLoopSteps = juce::jmax (16, barsN * 16);

            // Circular seam timing: allow intentional breathing room, but not an
            // accidental hole large enough to make the loop feel disconnected.
            const int firstStep = m.front()->step;
            const int lastEnd = juce::jlimit (0, totalLoopSteps,
                                               m.back()->step + juce::jmax (1, m.back()->length));
            const int circularGap = juce::jmax (0, totalLoopSteps - lastEnd + firstStep);
            const float seamGapFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, (float) std::abs (circularGap - 2) / 6.0f);

            // Fingerprints ignore absolute pitch so the same idea can move with
            // harmony while retaining its rhythmic and interval identity.
            auto barFingerprint = [&] (int bar)
            {
                struct Fingerprint
                {
                    std::vector<int> onsets;
                    std::vector<int> intervals;
                    std::vector<int> relativePitches;
                } out;

                std::vector<const NoteEvent*> barNotes;
                for (auto* n : m)
                    if (n->step / 16 == bar)
                        barNotes.push_back (n);
                if (barNotes.empty()) return out;

                const int anchor = barNotes.front()->note;
                for (size_t i = 0; i < barNotes.size(); ++i)
                {
                    out.onsets.push_back (barNotes[i]->step % 16);
                    out.relativePitches.push_back (juce::jlimit (-24, 24,
                                                                barNotes[i]->note - anchor));
                    if (i > 0)
                        out.intervals.push_back (juce::jlimit (-12, 12,
                            barNotes[i]->note - barNotes[i - 1]->note));
                }
                return out;
            };

            auto fingerprintSimilarity = [] (const auto& a, const auto& b)
            {
                if (a.onsets.empty() || b.onsets.empty())
                    return 0.0f;

                const size_t onsetPairs = juce::jmin (a.onsets.size(), b.onsets.size());
                int sameOnsets = 0;
                for (size_t i = 0; i < onsetPairs; ++i)
                    if (std::abs (a.onsets[i] - b.onsets[i]) <= 1) ++sameOnsets;

                const size_t pitchPairs = juce::jmin (a.relativePitches.size(), b.relativePitches.size());
                int samePitchShape = 0;
                for (size_t i = 0; i < pitchPairs; ++i)
                    if (std::abs (a.relativePitches[i] - b.relativePitches[i]) <= 2) ++samePitchShape;

                const size_t intervalPairs = juce::jmin (a.intervals.size(), b.intervals.size());
                int sameIntervals = 0;
                for (size_t i = 0; i < intervalPairs; ++i)
                    if (a.intervals[i] == b.intervals[i]) ++sameIntervals;

                const float countFit = 1.0f - juce::jlimit (0.0f, 1.0f,
                    (float) std::abs ((int) a.onsets.size() - (int) b.onsets.size()) / 5.0f);
                const float onsetFit = (float) sameOnsets / (float) onsetPairs;
                const float pitchFit = pitchPairs > 0 ? (float) samePitchShape / (float) pitchPairs : onsetFit;
                const float intervalFit = intervalPairs > 0 ? (float) sameIntervals / (float) intervalPairs : pitchFit;
                return juce::jlimit (0.0f, 1.0f,
                    0.30f * onsetFit + 0.28f * pitchFit + 0.27f * intervalFit + 0.15f * countFit);
            };

            const auto firstFingerprint = barFingerprint (0);
            float recurrenceSum = 0.0f;
            int recurrenceCount = 0;
            float localContrastSum = 0.0f;
            int localContrastCount = 0;
            int previousBarCount = -1;
            std::vector<float> allBarCounts;
            allBarCounts.reserve ((size_t) barsN);

            for (int bar = 0; bar < barsN; ++bar)
            {
                const auto fp = barFingerprint (bar);
                if (! firstFingerprint.onsets.empty() && bar > 0 && ! fp.onsets.empty())
                {
                    recurrenceSum += fingerprintSimilarity (firstFingerprint, fp);
                    ++recurrenceCount;
                }

                int totalBarNotes = 0;
                for (const auto& n : sec.notes)
                    if (n.step / 16 == bar)
                        ++totalBarNotes;
                allBarCounts.push_back ((float) totalBarNotes);

                if (previousBarCount >= 0)
                {
                    const float delta = std::abs ((float) totalBarNotes - (float) previousBarCount)
                                      / (float) juce::jmax (3, juce::jmax (totalBarNotes, previousBarCount));
                    localContrastSum += juce::jlimit (0.0f, 1.0f, delta);
                    ++localContrastCount;
                }
                previousBarCount = totalBarNotes;
            }

            const float motifRecurrence = recurrenceCount > 0
                ? recurrenceSum / (float) recurrenceCount : 0.55f;

            // Reward some internal movement, but reject bar-to-bar chaos. The
            // target sits around a light human phrase contrast.
            const float rawContrast = localContrastCount > 0
                ? localContrastSum / (float) localContrastCount : 0.0f;
            const float contrastFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (rawContrast - 0.28f) / 0.42f);

            float densityMean = 0.0f;
            for (const auto count : allBarCounts) densityMean += count;
            densityMean /= (float) juce::jmax<size_t> (1, allBarCounts.size());
            float densityVariance = 0.0f;
            for (const auto count : allBarCounts)
            {
                const float d = count - densityMean;
                densityVariance += d * d;
            }
            densityVariance /= (float) juce::jmax<size_t> (1, allBarCounts.size());
            const float densityCv = std::sqrt (densityVariance) / juce::jmax (1.0f, densityMean);
            const float densityFlow = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (densityCv - 0.30f) / 0.55f);

            // Melody/bass independence: perfect alignment everywhere is rigid,
            // while no shared accents at all makes the layers feel disconnected.
            int melodyOnsets = 0;
            int supportedOnsets = 0;
            std::vector<int> bassStarts;
            for (const auto& n : sec.notes)
                if (n.channel == 2) bassStarts.push_back (n.step);
            std::sort (bassStarts.begin(), bassStarts.end());
            for (auto* n : m)
            {
                ++melodyOnsets;
                bool nearbyBass = false;
                for (const auto step : bassStarts)
                {
                    if (std::abs (step - n->step) <= 1) { nearbyBass = true; break; }
                    if (step > n->step + 1) break;
                }
                if (nearbyBass) ++supportedOnsets;
            }
            const float sharedAccentRatio = melodyOnsets > 0
                ? (float) supportedOnsets / (float) melodyOnsets : 0.0f;
            const float layerInterplay = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (sharedAccentRatio - 0.56f) / 0.46f);

            // Closure shape: let the final bar hand the loop back to bar one.
            float closure = 0.55f;
            if (barsN >= 2)
            {
                int finalBarCount = 0;
                int penultimateBarCount = 0;
                for (const auto& n : sec.notes)
                {
                    if (n.step / 16 == barsN - 1) ++finalBarCount;
                    else if (n.step / 16 == barsN - 2) ++penultimateBarCount;
                }
                const float densityRatio = penultimateBarCount > 0
                    ? (float) finalBarCount / (float) penultimateBarCount : 1.0f;
                const float densityClosure = 1.0f
                    - juce::jlimit (0.0f, 1.0f, std::abs (densityRatio - 0.88f) / 0.90f);
                const float tailLengthFit = juce::jlimit (0.0f, 1.0f,
                    (float) juce::jmax (0, m.back()->length - 1) / 3.0f);
                closure = 0.62f * densityClosure + 0.38f * tailLengthFit;
            }

            const float seamScore = 0.55f * f.seam + 0.45f * seamGapFit;
            const float repetitionBalance = 0.58f * motifRecurrence + 0.42f * contrastFit;
            f.loopQuality = juce::jlimit (0.0f, 1.0f,
                0.24f * seamScore
                + 0.22f * repetitionBalance
                + 0.18f * densityFlow
                + 0.14f * layerInterplay
                + 0.12f * closure
                + 0.10f * f.phraseArc);
        }


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
    
}

MidiForgeAudioProcessor::IdeaFingerprint MidiForgeAudioProcessor::makeIdeaFingerprint (const Section& sec) const
{

        IdeaFingerprint fp;
        const int barsN = juce::jmax (1, sec.bars);

        const NoteEvent* anchor = nullptr;
        std::vector<const NoteEvent*> notes;
        for (int bar = 0; bar < barsN && notes.size() < 2; ++bar)
        {
            notes.clear();
            const int start = bar * 16;
            const int end = start + 16;
            for (const auto& n : sec.notes)
                if (n.channel == 3 && n.step >= start && n.step < end)
                    notes.push_back (&n);
            std::stable_sort (notes.begin(), notes.end(),
                [] (const NoteEvent* a, const NoteEvent* b)
                {
                    if (a->step != b->step) return a->step < b->step;
                    return a->note < b->note;
                });
            if (notes.size() >= 2)
                anchor = notes.front();
        }

        if (notes.size() < 2)
        {
            notes.clear();
            for (const auto& n : sec.notes)
                if (n.channel == 3) notes.push_back (&n);
            std::stable_sort (notes.begin(), notes.end(),
                [] (const NoteEvent* a, const NoteEvent* b)
                {
                    if (a->step != b->step) return a->step < b->step;
                    return a->note < b->note;
                });
            if (!notes.empty()) anchor = notes.front();
        }

        if (anchor == nullptr)
            return fp;

        const size_t count = juce::jmin<size_t> (7, notes.size());
        fp.count = (int) count;
        for (size_t i = 0; i < count; ++i)
        {
            fp.onsets[i] = notes[i]->step % 16;
            fp.relativePitches[i] = juce::jlimit (-24, 24, notes[i]->note - anchor->note);
            if (i > 0)
                fp.intervals[i - 1] = juce::jlimit (-12, 12, notes[i]->note - notes[i - 1]->note);
        }
        fp.intervalCount = juce::jmax (0, fp.count - 1);

        int repeats = 0;
        int positive = 0;
        int negative = 0;
        int wide = 0;
        for (int i = 0; i < fp.intervalCount; ++i)
        {
            const int iv = fp.intervals[(size_t) i];
            if (iv == 0) ++repeats;
            if (iv > 0) ++positive;
            if (iv < 0) ++negative;
            if (std::abs (iv) >= 7) ++wide;
        }

        const bool zigzag = positive > 0 && negative > 0
                          && positive + negative >= juce::jmax (2, fp.intervalCount - 1);
        const bool pickup = fp.count > 0 && (fp.onsets[0] % 16) != 0;
        if (repeats >= juce::jmax (1, fp.intervalCount / 2))
            fp.family = 0; // repeated/chant identity
        else if (wide >= juce::jmax (1, fp.intervalCount / 3))
            fp.family = 1; // wide-leap identity
        else if (zigzag)
            fp.family = 2; // angular/answering identity
        else if (positive >= negative + 2)
            fp.family = 3; // rising identity
        else if (negative >= positive + 2)
            fp.family = 4; // falling identity
        else if (pickup)
            fp.family = 5; // pickup-led identity
        else if (fp.count >= 5 && (fp.onsets[2] - fp.onsets[1]) >= 4)
            fp.family = 6; // long-gap / conversational identity
        else
            fp.family = 7; // balanced identity

        return fp;
    
}

float MidiForgeAudioProcessor::ideaSimilarity (const IdeaFingerprint& a, const IdeaFingerprint& b) const
{

        if (a.count < 2 || b.count < 2)
            return 0.0f;

        const int n = juce::jmin (a.count, b.count);
        int onsetHits = 0;
        int pitchHits = 0;
        int intervalHits = 0;
        int directionHits = 0;

        for (int i = 0; i < n; ++i)
        {
            if (std::abs (a.onsets[(size_t) i] - b.onsets[(size_t) i]) <= 1) ++onsetHits;
            if (std::abs (a.relativePitches[(size_t) i] - b.relativePitches[(size_t) i]) <= 2) ++pitchHits;
        }

        const int ni = juce::jmin (a.intervalCount, b.intervalCount);
        for (int i = 0; i < ni; ++i)
        {
            const int ia = a.intervals[(size_t) i];
            const int ib = b.intervals[(size_t) i];
            if (ia == ib || std::abs (ia - ib) <= 1) ++intervalHits;
            if ((ia == 0 && ib == 0) || (ia > 0 && ib > 0) || (ia < 0 && ib < 0))
                ++directionHits;
        }

        const float lengthFit = 1.0f
            - juce::jlimit (0.0f, 1.0f, (float) std::abs (a.count - b.count) / 4.0f);
        const float onsetFit = (float) onsetHits / (float) n;
        const float pitchFit = (float) pitchHits / (float) n;
        const float intervalFit = ni > 0 ? (float) intervalHits / (float) ni : pitchFit;
        const float directionFit = ni > 0 ? (float) directionHits / (float) ni : 0.5f;
        const float familyFit = a.family == b.family ? 1.0f : 0.0f;

        return juce::jlimit (0.0f, 1.0f,
            0.24f * onsetFit
            + 0.25f * pitchFit
            + 0.25f * intervalFit
            + 0.10f * directionFit
            + 0.08f * lengthFit
            + 0.08f * familyFit);
    
}

float MidiForgeAudioProcessor::motifMemoryScore (const Section& sec) const
{

        struct MotifCell
        {
            std::vector<int> onsets;
            std::vector<int> intervals;
            std::vector<int> relativePitches;
            int sourceBar = -1;
        };

        const int barsN = juce::jmax (1, sec.bars);
        std::vector<std::vector<const NoteEvent*>> barsMelody ((size_t) barsN);
        std::vector<std::vector<const NoteEvent*>> barsBass ((size_t) barsN);
        for (const auto& n : sec.notes)
        {
            const int b = juce::jlimit (0, barsN - 1, n.step / 16);
            if (n.channel == 3) barsMelody[(size_t) b].push_back (&n);
            else if (n.channel == 2) barsBass[(size_t) b].push_back (&n);
        }
        for (auto& v : barsMelody)
            std::stable_sort (v.begin(), v.end(), [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });
        for (auto& v : barsBass)
            std::stable_sort (v.begin(), v.end(), [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

        auto makeCell = [&](int bar) -> MotifCell
        {
            MotifCell out;
            out.sourceBar = bar;
            const auto& notes = barsMelody[(size_t) bar];
            const size_t limit = juce::jmin<size_t> (7, notes.size());
            if (limit == 0) return out;
            const int anchorPitch = notes.front()->note;
            for (size_t i = 0; i < limit; ++i)
            {
                out.onsets.push_back (notes[i]->step % 16);
                out.relativePitches.push_back (juce::jlimit (-24, 24, notes[i]->note - anchorPitch));
                if (i > 0)
                    out.intervals.push_back (juce::jlimit (-12, 12, notes[i]->note - notes[i - 1]->note));
            }
            return out;
        };

        auto cellSimilarity = [&](const MotifCell& a, const MotifCell& b)
        {
            if (a.onsets.empty() || b.onsets.empty()) return 0.0f;
            const size_t n = juce::jmin (a.onsets.size(), b.onsets.size());
            int onsetHits = 0;
            int pitchHits = 0;
            for (size_t i = 0; i < n; ++i)
            {
                if (std::abs (a.onsets[i] - b.onsets[i]) <= 1) ++onsetHits;
                if (std::abs (a.relativePitches[i] - b.relativePitches[i]) <= 2) ++pitchHits;
            }
            const size_t ni = juce::jmin (a.intervals.size(), b.intervals.size());
            int intervalHits = 0;
            for (size_t i = 0; i < ni; ++i)
                if (a.intervals[i] == b.intervals[i]) ++intervalHits;

            const float lengthFit = 1.0f - juce::jlimit (0.0f, 1.0f,
                (float) std::abs ((int) a.onsets.size() - (int) b.onsets.size()) / 4.0f);
            const float onsetFit = (float) onsetHits / (float) n;
            const float pitchFit = (float) pitchHits / (float) n;
            const float intervalFit = ni > 0 ? (float) intervalHits / (float) ni : pitchFit;

            // Inverted contour is still a recognizable transformation, but gets
            // less credit than preserving the original interval direction.
            int inverseHits = 0;
            for (size_t i = 0; i < ni; ++i)
                if (a.intervals[i] == -b.intervals[i]) ++inverseHits;
            const float inverseFit = ni > 0 ? (float) inverseHits / (float) ni : 0.0f;

            return juce::jlimit (0.0f, 1.0f,
                0.30f * onsetFit
                + 0.30f * pitchFit
                + 0.24f * intervalFit
                + 0.08f * inverseFit
                + 0.08f * lengthFit);
        };

        int firstBar = -1;
        for (int b = 0; b < barsN; ++b)
            if (barsMelody[(size_t) b].size() >= 2) { firstBar = b; break; }
        if (firstBar < 0) return 0.45f;

        const MotifCell mainMotif = makeCell (firstBar);
        if (mainMotif.onsets.size() < 2) return 0.45f;

        float mainSimilaritySum = 0.0f;
        int comparableBars = 0;
        int strongMainRepeats = 0;
        std::vector<float> barSimilarity ((size_t) barsN, 0.0f);
        for (int b = firstBar + 1; b < barsN; ++b)
        {
            const auto cell = makeCell (b);
            if (cell.onsets.size() < 2) continue;
            const float sim = cellSimilarity (mainMotif, cell);
            barSimilarity[(size_t) b] = sim;
            mainSimilaritySum += sim;
            ++comparableBars;
            if (sim >= 0.64f) ++strongMainRepeats;
        }
        const float mainRecurrence = comparableBars > 0
            ? mainSimilaritySum / (float) comparableBars : 0.0f;

        // Pick a secondary motif that is genuinely different from the main one,
        // then reward it when it reappears later as an answer/counter-idea.
        int secondaryBar = -1;
        float secondaryDistinctness = 0.0f;
        for (int b = firstBar + 1; b < barsN; ++b)
        {
            const auto cell = makeCell (b);
            if (cell.onsets.size() < 2) continue;
            const float distinctness = 1.0f - cellSimilarity (mainMotif, cell);
            if (distinctness > secondaryDistinctness)
            {
                secondaryDistinctness = distinctness;
                secondaryBar = b;
            }
        }

        float secondaryRecurrence = 0.0f;
        int secondaryComparisons = 0;
        if (secondaryBar >= 0 && secondaryDistinctness >= 0.22f)
        {
            const auto secondaryMotif = makeCell (secondaryBar);
            for (int b = secondaryBar + 1; b < barsN; ++b)
            {
                const auto cell = makeCell (b);
                if (cell.onsets.size() < 2) continue;
                secondaryRecurrence += cellSimilarity (secondaryMotif, cell);
                ++secondaryComparisons;
            }
        }
        if (secondaryComparisons > 0)
            secondaryRecurrence /= (float) secondaryComparisons;

        // Rhythmic fingerprint: compare the main motif's onset pattern with all
        // layers. This is deliberately independent from pitch.
        float rhythmFingerprint = 0.0f;
        int rhythmComparisons = 0;
        for (int b = firstBar + 1; b < barsN; ++b)
        {
            const auto& melody = barsMelody[(size_t) b];
            if (melody.size() < 2) continue;
            const size_t n = juce::jmin (mainMotif.onsets.size(), melody.size());
            int hits = 0;
            for (size_t i = 0; i < n; ++i)
                if (std::abs (mainMotif.onsets[i] - (melody[i]->step % 16)) <= 1) ++hits;
            rhythmFingerprint += (float) hits / (float) n;
            ++rhythmComparisons;
        }
        if (rhythmComparisons > 0)
            rhythmFingerprint /= (float) rhythmComparisons;

        // Bass fingerprint: compare bar-to-bar bass movement rather than absolute
        // notes so the same harmonic idea can shift register without losing identity.
        std::vector<int> bassRoots;
        for (int b = 0; b < barsN; ++b)
            if (!barsBass[(size_t) b].empty())
                bassRoots.push_back (barsBass[(size_t) b].front()->note);
        float bassFingerprint = 0.5f;
        if (bassRoots.size() >= 3)
        {
            std::vector<int> bassIntervals;
            for (size_t i = 1; i < bassRoots.size(); ++i)
                bassIntervals.push_back (juce::jlimit (-12, 12, bassRoots[i] - bassRoots[i - 1]));

            int repeated = 0;
            for (size_t i = 1; i < bassIntervals.size(); ++i)
                if (bassIntervals[i] == bassIntervals[i - 1]) ++repeated;
            bassFingerprint = 0.35f
                + 0.65f * juce::jlimit (0.0f, 1.0f,
                    (float) repeated / (float) juce::jmax<size_t> (1, bassIntervals.size() - 1));
        }

        // Main motif should recur, but a good loop should not be a bar copier.
        // Shape the reward around a healthy recurrence range.
        const float recurrenceTarget = barsN <= 2 ? 0.72f : 0.58f;
        const float recurrenceFit = 1.0f
            - juce::jlimit (0.0f, 1.0f, std::abs (mainRecurrence - recurrenceTarget) / 0.58f);
        const float mainPresence = barsN <= 2
            ? juce::jlimit (0.0f, 1.0f, mainRecurrence)
            : juce::jlimit (0.0f, 1.0f, (float) strongMainRepeats / (float) juce::jmax (1, barsN / 2));
        const float secondaryPresence = secondaryComparisons > 0
            ? juce::jlimit (0.0f, 1.0f, secondaryRecurrence) : 0.45f;

        // If every later bar is nearly identical to the first, memory becomes
        // mechanical repetition rather than compositional identity.
        int literalBars = 0;
        for (int b = firstBar + 1; b < barsN; ++b)
            if (barSimilarity[(size_t) b] >= 0.92f) ++literalBars;
        const float literalPenalty = barsN > 2
            ? juce::jlimit (0.0f, 1.0f, (float) literalBars / (float) juce::jmax (1, barsN - 1))
            : 0.0f;

        return juce::jlimit (0.0f, 1.0f,
            0.28f * mainPresence
            + 0.20f * recurrenceFit
            + 0.16f * rhythmFingerprint
            + 0.14f * secondaryPresence
            + 0.12f * bassFingerprint
            + 0.10f * recurrenceTarget
            - 0.18f * literalPenalty);
    
}

void MidiForgeAudioProcessor::applyGrooveEngine (Section& sec, uint32_t identity) const
{

        const int totalSteps = juce::jmax (16, sec.bars * 16);
        int pocket[16] = { 0 };
        int accents[16] = { 0 };

        const uint32_t grooveSeed = hash32 (identity ^ 0x6A09E667u);
        const int baseBias = (int) (grooveSeed % 3u) - 1;
        for (int pos = 0; pos < 16; ++pos)
        {
            const uint32_t h = hash32 (grooveSeed ^ (uint32_t) (pos + 1) * 0x9E3779B9u);
            const bool anchorPos = (pos % 4) == 0;
            if (anchorPos)
            {
                pocket[pos] = 0;
                accents[pos] = (pos == 0 ? 7 : (pos == 8 ? 5 : 3));
            }
            else
            {
                const int roll = (int) (h % 100u);
                pocket[pos] = (roll < 24 ? -1 : (roll > 80 ? 1 : 0));
                if (swing > 0.08f && (pos & 1) && pocket[pos] == 0 && roll > 42)
                    pocket[pos] = 1;
                if (rhythm == Syncopated && (pos % 4) == 3 && roll > 34)
                    pocket[pos] = 1;
                if (rhythm == Broken && (pos == 3 || pos == 10 || pos == 13) && roll > 46)
                    pocket[pos] = -1;
                pocket[pos] = juce::jlimit (-1, 1, pocket[pos] + ((pos % 4) == 2 ? baseBias : 0));
                accents[pos] = ((pos % 4) == 2 ? 2 : ((pos % 4) == 3 ? 1 : 0));
                if ((h % 100u) < 18u) accents[pos] += 2;
            }
        }

        auto layerStrength = [] (int channel)
        {
            switch (channel)
            {
                case 1: return 0.34f;
                case 2: return 0.78f;
                case 3: return 0.92f;
                case 4: return 0.66f;
                case 5: return 0.84f;
                default: return 0.50f;
            }
        };

        for (size_t i = 0; i < sec.notes.size(); ++i)
        {
            auto& n = sec.notes[i];
            const int pos = ((n.step % 16) + 16) % 16;
            const float strength = layerStrength (n.channel);
            const uint32_t h = hash32 (grooveSeed ^ (uint32_t) i * 0x85EBCA6Bu ^ (uint32_t) n.step * 0x27D4EB2Du);

            int delta = juce::roundToInt ((float) pocket[pos] * strength);
            if ((pos % 4) == 0 || n.channel == 1)
                delta = 0;
            n.step = juce::jlimit (0, totalSteps - 1, n.step + delta);

            const int localPos = ((n.step % 16) + 16) % 16;
            int velocityDelta = accents[localPos];
            if (localPos == 0 || localPos == 8) velocityDelta += 3;
            if ((h % 100u) < 22u) velocityDelta -= 2;
            else if ((h % 100u) > 80u) velocityDelta += 2;
            velocityDelta = juce::roundToInt ((float) velocityDelta * (0.65f + 0.35f * strength));
            n.velocity = juce::jlimit (28, 122, n.velocity + velocityDelta);

            if (n.channel != 5)
            {
                if (accents[localPos] >= 3 && (h & 3u) != 0u)
                    n.length = juce::jlimit (1, 16, n.length + 1);
                else if (accents[localPos] == 0 && (h % 100u) < 28u)
                    n.length = juce::jmax (1, n.length - 1);
            }
            else if ((h % 100u) < 38u)
            {
                n.length = juce::jmax (1, juce::jmin (2, n.length));
            }
        }

        removeDuplicateNotes (sec.notes);
        cleanMelodyLine (sec.notes);
        std::sort (sec.notes.begin(), sec.notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step) return a.step < b.step;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.note < b.note;
        });
    
}

float MidiForgeAudioProcessor::grooveQualityScore (const Section& sec) const
{

        int positionCount[16] = { 0 };
        int positionVelocity[16] = { 0 };
        int layerMask[16] = { 0 };
        float lengthSum = 0.0f;
        int lengthCount = 0;

        for (const auto& n : sec.notes)
        {
            const int p = ((n.step % 16) + 16) % 16;
            ++positionCount[p];
            positionVelocity[p] += n.velocity;
            if (n.channel >= 1 && n.channel <= 5)
                layerMask[p] |= (1 << (n.channel - 1));
            if (n.channel != 5)
            {
                lengthSum += (float) n.length;
                ++lengthCount;
            }
        }

        int occupied = 0;
        float velocityMean = 0.0f;
        for (int p = 0; p < 16; ++p)
        {
            if (positionCount[p] > 0)
            {
                ++occupied;
                velocityMean += (float) positionVelocity[p] / (float) positionCount[p];
            }
        }
        velocityMean /= (float) juce::jmax (1, occupied);

        float accentVariance = 0.0f;
        int offbeatHits = 0;
        int sharedPositions = 0;
        for (int p = 0; p < 16; ++p)
        {
            if ((p & 1) != 0 && positionCount[p] > 0) ++offbeatHits;
            unsigned mask = (unsigned) layerMask[p];
            int layersAtPos = 0;
            while (mask != 0u) { layersAtPos += (int) (mask & 1u); mask >>= 1; }
            if (layersAtPos >= 2) ++sharedPositions;
            if (positionCount[p] > 0)
            {
                const float d = ((float) positionVelocity[p] / (float) positionCount[p]) - velocityMean;
                accentVariance += d * d;
            }
        }
        accentVariance /= (float) juce::jmax (1, occupied);

        const float accentContrast = juce::jlimit (0.0f, 1.0f, std::sqrt (accentVariance) / 18.0f);
        const float offbeatRatio = (float) offbeatHits / (float) juce::jmax (1, occupied);
        const float offbeatFit = 1.0f - juce::jlimit (0.0f, 1.0f, std::abs (offbeatRatio - 0.42f) / 0.48f);
        const float sharedLayerFit = 1.0f - juce::jlimit (0.0f, 1.0f,
            std::abs ((float) sharedPositions / 16.0f - 0.46f) / 0.46f);
        const float contrastFit = 1.0f - juce::jlimit (0.0f, 1.0f, std::abs (accentContrast - 0.44f) / 0.50f);
        const float lengthFit = lengthCount > 0
            ? juce::jlimit (0.0f, 1.0f, (lengthSum / (float) lengthCount) / 5.0f) : 0.5f;

        return juce::jlimit (0.0f, 1.0f,
            0.30f * contrastFit
            + 0.24f * offbeatFit
            + 0.24f * sharedLayerFit
            + 0.12f * lengthFit
            + 0.10f * juce::jlimit (0.0f, 1.0f, (float) occupied / 16.0f));
    
}

int MidiForgeAudioProcessor::rootAtBar (int bar) const
{
const auto magicProgression = progressionDegrees();
        
        if (magicProgression.empty())
            return 12 * octave + rootPc;
        return degreeToPitch (magicProgression[(size_t) (bar % (int) magicProgression.size())], octave);
    
}

void MidiForgeAudioProcessor::applyMagicArchetype (Section& flat, int archetype, uint32_t identity) const
{
const auto magicProgression = progressionDegrees();
        
        const int barsN = juce::jmax (1, flat.bars);
        const int totalSteps = juce::jmax (16, barsN * 16);

        const auto isStructuralAnchor = [&] (const NoteEvent& n)
        {
            if ((n.step % 16) != 0)
                return false;
            return (n.channel == 2 && !soundProfileFor(soundTarget).bassOff)
                || (n.channel == 5 && n.note == 36)
                || (n.channel == 3 && soundProfileFor(soundTarget).soloLine);
        };

        auto melodyNotes = [&]()
        {
            std::vector<NoteEvent> out;
            for (const auto& n : flat.notes)
                if (n.channel == 3) out.push_back (n);
            std::stable_sort (out.begin(), out.end(), [] (const NoteEvent& a, const NoteEvent& b)
            {
                if (a.step != b.step) return a.step < b.step;
                return a.note < b.note;
            });
            return out;
        };

        auto eraseMelody = [&]()
        {
            flat.notes.erase (std::remove_if (flat.notes.begin(), flat.notes.end(),
                [] (const NoteEvent& n) { return n.channel == 3; }), flat.notes.end());
        };

        // 0 HOOK: preserve one memorable opening cell and let harmony move it.
        if (archetype == 0)
        {
            const auto source = melodyNotes();
            if (! source.empty())
            {
                std::vector<NoteEvent> firstBar;
                for (const auto& n : source)
                    if (n.step < 16) firstBar.push_back (n);

                if (! firstBar.empty())
                {
                    eraseMelody();
                    for (int bar = 0; bar < barsN; ++bar)
                    {
                        const int rootDelta = rootAtBar (bar) - rootAtBar (0);
                        for (const auto& src : firstBar)
                        {
                            auto n = src;
                            n.step = bar * 16 + (src.step % 16);
                            n.note = snapToScale (src.note + rootDelta);
                            if (n.step < totalSteps) flat.notes.push_back (n);
                        }
                    }
                }
            }
        }

        // 1 GROOVE: create pocket through shared micro-shifts and accents.
        if (archetype == 1)
        {
            for (size_t i = 0; i < flat.notes.size(); ++i)
            {
                auto& n = flat.notes[i];
                if (n.channel == 1) continue;
                const uint32_t h = hash32 (identity ^ (uint32_t) n.step * 0x27d4eb2du ^ (uint32_t) i);
                const bool structuralAnchor =
                    (n.step % 16) == 0
                    && ((n.channel == 2 && !soundProfileFor(soundTarget).bassOff)
                        || (n.channel == 5 && n.note == 36)
                        || (n.channel == 3 && soundProfileFor(soundTarget).soloLine));
                if (! structuralAnchor && (n.step % 4) == 0 && (h % 100u) < 42u)
                    n.step = juce::jlimit (0, totalSteps - 1, n.step + 1);
                const int pos = n.step % 16;
                const int accent = (pos == 0 || pos == 8) ? 8 : ((pos % 4) == 0 ? 3 : -2);
                n.velocity = juce::jlimit (25, 122, n.velocity + accent);
                if (n.channel != 5 && (h & 31u) == 0u)
                    n.length = juce::jlimit (1, 8, n.length + 1);
            }
        }

        // 2 HARMONY: pull melodic and bass destinations toward the active chord.
        if (archetype == 2 && ! magicProgression.empty())
        {
            int bassLo = 28, bassHi = 52;
            registerLane (1, bassLo, bassHi);
            for (size_t i = 0; i < flat.notes.size(); ++i)
            {
                auto& n = flat.notes[i];
                const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
                const int degree = magicProgression[(size_t) (bar % (int) magicProgression.size())];
                if (n.channel == 3)
                {
                    int best = n.note;
                    int bestDist = 1000;
                    for (const int chordDegree : { degree, degree + 2, degree + 4 })
                    {
                        const int chordPitch = degreeToPitch (chordDegree, octave);
                        for (int oct = -2; oct <= 2; ++oct)
                        {
                            const int candidate = chordPitch + oct * 12;
                            const int dist = std::abs (candidate - n.note);
                            if (candidate >= 48 && candidate <= 108 && dist < bestDist)
                            {
                                best = candidate;
                                bestDist = dist;
                            }
                        }
                    }
                    n.note = snapToScale (best);
                }
                else if (n.channel == 2)
                {
                    const int root = juce::jlimit (bassLo, bassHi, degreeToPitch (degree, 2));
                    const uint32_t h = hash32 (identity ^ (uint32_t) n.step * 13u ^ (uint32_t) i);
                    if ((h % 100u) < 72u)
                        n.note = juce::jlimit (bassLo, bassHi, snapToScale (root));
                }
            }
        }

        // 3 MOTIF: repeat a two-bar fingerprint through the arrangement.
        if (archetype == 3)
        {
            const auto source = melodyNotes();
            std::vector<NoteEvent> motif;
            for (const auto& n : source)
                if (n.step < 32) motif.push_back (n);

            if (! motif.empty())
            {
                eraseMelody();
                for (int phraseStart = 0; phraseStart < totalSteps; phraseStart += 32)
                {
                    const int rootDelta = rootAtBar (phraseStart / 16) - rootAtBar (0);
                    for (const auto& src : motif)
                    {
                        auto n = src;
                        n.step = phraseStart + (src.step % 32);
                        if (n.step >= totalSteps) continue;
                        n.note = snapToScale (src.note + rootDelta);
                        flat.notes.push_back (n);
                    }
                }
            }
        }

        // 4 MINIMAL: reduce supporting clutter while preserving the strongest melody hits.
        if (archetype == 4)
        {
            std::vector<char> keepMelody (flat.notes.size(), 0);
            for (int bar = 0; bar < barsN; ++bar)
            {
                std::vector<size_t> candidatesInBar;
                for (size_t i = 0; i < flat.notes.size(); ++i)
                    if (flat.notes[i].channel == 3 && flat.notes[i].step / 16 == bar)
                        candidatesInBar.push_back (i);

                std::sort (candidatesInBar.begin(), candidatesInBar.end(), [&] (size_t a, size_t b)
                {
                    const auto& na = flat.notes[a];
                    const auto& nb = flat.notes[b];
                    const int sa = na.velocity + (na.step % 4 == 0 ? 28 : 0) + na.length * 4;
                    const int sb = nb.velocity + (nb.step % 4 == 0 ? 28 : 0) + nb.length * 4;
                    return sa > sb;
                });
                const size_t limit = juce::jmin<size_t> (3, candidatesInBar.size());
                for (size_t k = 0; k < limit; ++k)
                    keepMelody[candidatesInBar[k]] = 1;
            }

            for (size_t i = flat.notes.size(); i-- > 0; )
            {
                auto& n = flat.notes[i];
                if (n.channel == 3 && ! keepMelody[i])
                    flat.notes.erase (flat.notes.begin() + (long long) i);
            }

            int arpSeen = 0;
            for (size_t i = flat.notes.size(); i-- > 0; )
            {
                auto& n = flat.notes[i];
                if (n.channel == 4)
                {
                    if ((arpSeen++ & 1) != 0)
                        flat.notes.erase (flat.notes.begin() + (long long) i);
                }
                else if (n.channel == 5)
                {
                    const uint32_t h = hash32 (identity ^ (uint32_t) n.step * 17u ^ (uint32_t) i);
                    if ((n.step % 4) != 0 && n.velocity < 72 && (h % 100u) < 35u)
                        flat.notes.erase (flat.notes.begin() + (long long) i);
                }
            }
        }

        // 5 WEIRD: controlled register jumps and asymmetric timing, still scale-safe.
        if (archetype == 5)
        {
            int melodySeen = 0;
            for (auto& n : flat.notes)
            {
                if (n.channel != 3) continue;
                const uint32_t h = hash32 (identity ^ (uint32_t) melodySeen * 0x9e3779b9u);
                if ((melodySeen++ % 4) == 2)
                {
                    const int jump = ((h & 1u) != 0u) ? 7 : -7;
                    n.note = snapToScale (juce::jlimit (28, 108, n.note + jump));
                }
                if ((h % 100u) < 18u)
                    n.step = juce::jlimit (0, totalSteps - 1, n.step + (((h >> 8) & 1u) ? 1 : -1));
            }
        }

        // 6 EMOTIONAL: explicit rise -> peak -> release in register and dynamics.
        if (archetype == 6)
        {
            for (size_t i = 0; i < flat.notes.size(); ++i)
            {
                auto& n = flat.notes[i];
                const float t = barsN > 1 ? (float) (n.step / 16) / (float) (barsN - 1) : 0.0f;
                const float arc = 1.0f - std::abs (2.0f * t - 1.0f);
                if (n.channel == 3)
                {
                    n.note = snapToScale (juce::jlimit (36, 108, n.note + juce::roundToInt (arc * 6.0f)));
                    n.velocity = juce::jlimit (35, 120, n.velocity + juce::roundToInt (arc * 12.0f));
                    if (n.step / 16 == barsN - 1)
                        n.length = juce::jmin (6, juce::jmax (2, n.length + 1));
                }
                else if (n.channel == 2 && (n.step % 16) == 0)
                {
                    n.velocity = juce::jlimit (40, 118, n.velocity + juce::roundToInt (arc * 7.0f));
                }
            }

            if (! magicProgression.empty())
            {
                const int lastBar = barsN - 1;
                const int degree = magicProgression[(size_t) (lastBar % (int) magicProgression.size())];
                int lastMelody = -1;
                for (int i = (int) flat.notes.size() - 1; i >= 0; --i)
                    if (flat.notes[(size_t) i].channel == 3)
                    {
                        lastMelody = i;
                        break;
                    }
                if (lastMelody >= 0)
                    flat.notes[(size_t) lastMelody].note = snapToScale (degreeToPitch (degree, octave));
            }
        }

        // 7 WILDCARD: a lighter hybrid branch deliberately escapes the main archetypes.
        if (archetype == 7)
        {
            const int branch = (int) ((identity >> 8) % 3u);
            for (size_t i = 0; i < flat.notes.size(); ++i)
            {
                auto& n = flat.notes[i];
                const uint32_t h = hash32 (identity ^ (uint32_t) i * 0x85ebca6bu);
                if (n.channel == 3 && branch == 0 && (h % 100u) < 35u)
                    n.note = snapToScale (juce::jlimit (32, 108, n.note + 5));
                else if (n.channel != 1 && branch == 1 && !isStructuralAnchor (n)
                         && (n.step % 4) == 0 && (h % 100u) < 40u)
                    n.step = juce::jlimit (0, totalSteps - 1, n.step + 1);
                else if (n.channel == 3 && branch == 2 && (h % 100u) < 26u)
                    n.note = snapToScale (juce::jlimit (32, 108, n.note - 12));
            }
        }

        removeDuplicateNotes (flat.notes);
        cleanMelodyLine (flat.notes);
        std::sort (flat.notes.begin(), flat.notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step) return a.step < b.step;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.note < b.note;
        });
    
}

float MidiForgeAudioProcessor::similarity (const Section& a, const Section& b) const
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
    
}

float MidiForgeAudioProcessor::behaviorDistance (const Candidate& a, const Candidate& b)
{

        const float d[] =
        {
            std::abs (a.density - b.density),
            std::abs (a.space - b.space),
            std::abs (a.rhythm - b.rhythm),
            std::abs (a.motif - b.motif),
            std::abs (a.leap - b.leap),
            std::abs (a.reg - b.reg),
            std::abs (a.surprise - b.surprise),
            std::abs (a.context - b.context),
            std::abs (a.loop - b.loop),
            std::abs (a.groove - b.groove),
            std::abs (a.memory - b.memory),
            std::abs (a.phraseArc - b.phraseArc),
            std::abs (a.tension - b.tension)
        };

        // Tension, surprise, rhythm and density carry slightly more weight:
        // they are the dimensions most likely to make two otherwise similar
        // loops feel like different musical behaviors.
        const float w[] =
        {
            0.09f, 0.06f, 0.12f, 0.08f, 0.09f, 0.06f, 0.12f,
            0.06f, 0.08f, 0.08f, 0.06f, 0.05f, 0.13f
        };

        float sum = 0.0f, weight = 0.0f;
        for (size_t i = 0; i < sizeof (d) / sizeof (d[0]); ++i)
        {
            sum += d[i] * w[i];
            weight += w[i];
        }
        return weight > 0.0f ? juce::jlimit (0.0f, 1.0f, sum / weight) : 0.0f;
    
}

MidiForgeAudioProcessor::Section MidiForgeAudioProcessor::flatten (const SongData& song, int candidateIndex, juce::Random& local, int mLo, int mHi) const
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
    
}

void MidiForgeAudioProcessor::buildAdaptiveProfile (const std::vector<Candidate>& candidates, int firstPassCandidates, AdaptiveProfile& adaptive) const
{

        if ((int) candidates.size() < firstPassCandidates)
            return;

        std::vector<size_t> ranked ((size_t) firstPassCandidates);
        std::iota (ranked.begin(), ranked.end(), 0u);
        const size_t keep = 48;
        std::partial_sort (ranked.begin(), ranked.begin() + (long long) keep, ranked.end(),
            [&] (size_t a, size_t b) { return candidates[a].quality > candidates[b].quality; });

        float weights[10] = { 0 };
        float sums[10] = { 0 };
        float totalWeight = 0.0f;
        for (size_t k = 0; k < keep; ++k)
        {
            const Candidate& c = candidates[ranked[k]];
            const float rankWeight = 1.0f - 0.65f * ((float) k / (float) juce::jmax<size_t> (1, keep - 1));
            sums[0] += c.density * rankWeight;
            sums[1] += c.space * rankWeight;
            sums[2] += c.rhythm * rankWeight;
            sums[3] += c.motif * rankWeight;
            sums[4] += c.leap * rankWeight;
            sums[5] += c.reg * rankWeight;
            sums[6] += c.surprise * rankWeight;
            sums[7] += c.loop * rankWeight;
            sums[8] += c.groove * rankWeight;
            sums[9] += c.memory * rankWeight;
            totalWeight += rankWeight;
        }
        for (float& w : weights) w = totalWeight;
        if (totalWeight > 0.0f)
        {
            adaptive.density = sums[0] / weights[0];
            adaptive.space = sums[1] / weights[1];
            adaptive.rhythm = sums[2] / weights[2];
            adaptive.motif = sums[3] / weights[3];
            adaptive.leap = sums[4] / weights[4];
            adaptive.reg = sums[5] / weights[5];
            adaptive.surprise = sums[6] / weights[6];
            adaptive.loop = sums[7] / weights[7];
            adaptive.groove = sums[8] / weights[8];
            adaptive.memory = sums[9] / weights[9];
            adaptive.ready = true;
        }
    
}

float MidiForgeAudioProcessor::minDiversityToSelected (const Candidate& candidate, const std::vector<Candidate>& selected) const
{

        if (selected.empty()) return 1.0f;

        float minimum = 1.0f;
        for (const auto& s : selected)
        {
            const float sim = similarity (candidate.section, s.section);
            const float behavior = behaviorDistance (candidate, s);
            const float ideaDistance = 1.0f - ideaSimilarity (candidate.idea, s.idea);
            const float combined = 0.48f * (1.0f - sim)
                                 + 0.30f * behavior
                                 + 0.22f * ideaDistance;
            minimum = juce::jmin (minimum, juce::jlimit (0.0f, 1.0f, combined));
        }
        return minimum;
    
}

MidiForgeAudioProcessor::Section MidiForgeAudioProcessor::transformLoop (Section source, int mode, uint32_t identity) const
{

        const int barsN = juce::jmax (1, source.bars);
        const int totalSteps = juce::jmax (16, barsN * 16);
        const bool tight = (mode == 1 || mode == 6);
        const bool sparse = (mode == 2 || mode == 7);
        const bool dark = (mode == 3 || mode == 7);
        const bool bigger = (mode == 4);
        const bool weird = (mode == 5 || mode == 6);

        if (tight)
        {
            for (auto& n : source.notes)
            {
                if (n.channel == 5) continue;
                const int pos = n.step % 16;
                if ((pos & 1) != 0)
                {
                    const int target = pos <= 7 ? juce::jmax (0, pos - 1)
                                                : juce::jmin (14, pos + 1);
                    n.step = juce::jlimit (0, totalSteps - 1,
                                           (n.step / 16) * 16 + target);
                }
                if (n.channel == 3 && n.length > 2 && (n.step % 4) != 0)
                    n.length = juce::jmax (1, n.length - 1);
            }
        }

        if (sparse)
        {
            std::vector<int> melodyPerBar ((size_t) barsN, 0);
            std::vector<int> arpPerBar ((size_t) barsN, 0);
            for (const auto& n : source.notes)
            {
                const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
                if (n.channel == 3) ++melodyPerBar[(size_t) bar];
                if (n.channel == 4) ++arpPerBar[(size_t) bar];
            }

            source.notes.erase (std::remove_if (source.notes.begin(), source.notes.end(),
                [&] (const NoteEvent& n)
                {
                    const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
                    const uint32_t h = hash32 (identity
                                               ^ (uint32_t) (n.step * 131 + n.note * 17)
                                               ^ (uint32_t) n.channel * 0x9E3779B9u);

                    if (n.channel == 3)
                    {
                        if (melodyPerBar[(size_t) bar] <= 1) return false;
                        return (h % 100u) < 28u;
                    }

                    if (n.channel == 4)
                    {
                        if (arpPerBar[(size_t) bar] <= 1) return false;
                        return (h % 100u) < 34u;
                    }

                    if (n.channel == 5)
                        return (h % 100u) < 18u;

                    return false; // keep chords and bass structurally intact
                }), source.notes.end());
        }

        if (dark)
        {
            for (auto& n : source.notes)
            {
                const uint32_t h = hash32 (identity ^ (uint32_t) n.step * 0x45D9F3Bu
                                           ^ (uint32_t) n.note * 0x27D4EB2Du);

                if (n.channel == 3 || n.channel == 4)
                {
                    n.note = foldIntoLane (snapToScale (n.note - ((h % 3u) == 0u ? 7 : 5)), 30, 96);
                    n.velocity = juce::jmax (30, n.velocity - 6);
                }
                else if (n.channel == 2 && (h % 100u) < 42u)
                {
                    n.note = juce::jlimit (24, 55, n.note - 12);
                    n.velocity = juce::jmax (34, n.velocity - 4);
                }
                else if (n.channel == 1 && (h % 100u) < 20u)
                {
                    n.note = juce::jlimit (24, 108, n.note - 12);
                    n.velocity = juce::jmax (30, n.velocity - 3);
                }
            }
        }

        if (bigger)
        {
            for (auto& n : source.notes)
            {
                const uint32_t h = hash32 (identity ^ (uint32_t) n.step * 0x9E3779B9u
                                           ^ (uint32_t) n.channel * 0x85EBCA6Bu);

                if (n.channel == 3 && (h % 100u) < 36u)
                {
                    n.note = juce::jlimit (36, 108, n.note + 12);
                    n.velocity = juce::jlimit (35, 122, n.velocity + 5);
                    n.length = juce::jmin (16, n.length + 1);
                }
                else if (n.channel == 4 && (h % 100u) < 24u)
                {
                    n.note = juce::jlimit (40, 108, n.note + 12);
                }
                else if (n.channel == 2 && (h % 100u) < 30u)
                {
                    n.note = juce::jmax (24, n.note - 12);
                    n.velocity = juce::jlimit (35, 118, n.velocity + 3);
                }
            }

            // Widen each chord hit by moving its top voice up one octave on a
            // subset of hits. This preserves the progression while making it
            // physically feel larger rather than simply adding random notes.
            std::sort (source.notes.begin(), source.notes.end(),
                       [] (const NoteEvent& a, const NoteEvent& b)
                       {
                           if (a.channel != b.channel) return a.channel < b.channel;
                           if (a.step != b.step) return a.step < b.step;
                           return a.note < b.note;
                       });

            for (size_t pos = 0; pos < source.notes.size(); )
            {
                if (source.notes[pos].channel != 1)
                {
                    ++pos;
                    continue;
                }

                const int step = source.notes[pos].step;
                size_t endPos = pos;
                while (endPos < source.notes.size()
                       && source.notes[endPos].channel == 1
                       && source.notes[endPos].step == step)
                    ++endPos;

                const uint32_t h = hash32 (identity ^ (uint32_t) step * 0x51ED270Bu);
                if (endPos > pos && (h % 100u) < 46u)
                {
                    auto& top = source.notes[endPos - 1];
                    top.note = juce::jlimit (24, 108, top.note + 12);
                    top.length = juce::jmin (16, top.length + 1);
                }

                pos = endPos;
            }
        }

        if (weird)
        {
            std::vector<int> anchors ((size_t) barsN, -1);
            for (const auto& n : source.notes)
            {
                if (n.channel == 3)
                {
                    const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
                    if (anchors[(size_t) bar] < 0)
                        anchors[(size_t) bar] = n.note;
                }
            }

            for (size_t i = 0; i < source.notes.size(); ++i)
            {
                auto& n = source.notes[i];
                const uint32_t h = hash32 (identity
                                           ^ (uint32_t) i * 0xC2B2AE35u
                                           ^ (uint32_t) n.step * 0x27D4EB2Du);

                if (n.channel == 3)
                {
                    const int bar = juce::jlimit (0, barsN - 1, n.step / 16);
                    if (anchors[(size_t) bar] >= 0 && (h % 100u) < 24u)
                    {
                        const int rel = n.note - anchors[(size_t) bar];
                        n.note = foldIntoLane (snapToScale (anchors[(size_t) bar] - rel), 34, 108);
                    }

                    if ((h % 100u) >= 24u && (h % 100u) < 42u)
                    {
                        const int delta = (h & 1u) ? 1 : -1;
                        n.step = juce::jlimit (0, totalSteps - 1, n.step + delta);
                    }
                }
                else if (n.channel == 4 && (h % 100u) < 28u)
                {
                    n.step = juce::jlimit (0, totalSteps - 1,
                                           n.step + ((h & 1u) ? 2 : -2));
                }
                else if (n.channel == 5 && (h % 100u) < 20u)
                {
                    n.velocity = juce::jlimit (26, 122, n.velocity + ((h & 1u) ? 7 : -7));
                }
            }
        }

        removeDuplicateNotes (source.notes);
        cleanMelodyLine (source.notes);
        std::sort (source.notes.begin(), source.notes.end(),
                   [] (const NoteEvent& a, const NoteEvent& b)
                   {
                       if (a.step != b.step) return a.step < b.step;
                       if (a.channel != b.channel) return a.channel < b.channel;
                       return a.note < b.note;
                   });
        return source;
    
}

float MidiForgeAudioProcessor::creativeRangeScore (const Section& sec, uint32_t identity) const
{
    std::vector<const NoteEvent*> melody;
    for (const auto& n : sec.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    if (melody.size() < 3)
        return 0.42f;

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    const auto plan = midiforge::CreativeRange::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    int minPitch = 127, maxPitch = 0;
    int leapCount = 0;
    int directionChanges = 0;
    int shortNotes = 0, longNotes = 0;
    std::vector<int> intervalKinds;

    int previousDelta = 0;
    for (size_t i = 0; i < melody.size(); ++i)
    {
        minPitch = juce::jmin (minPitch, melody[i]->note);
        maxPitch = juce::jmax (maxPitch, melody[i]->note);

        if (melody[i]->length <= 1) ++shortNotes;
        if (melody[i]->length >= 3) ++longNotes;

        if (i == 0) continue;

        const int delta = melody[i]->note - melody[i - 1]->note;
        const int absDelta = std::abs (delta);
        if (absDelta >= 7) ++leapCount;

        if (absDelta > 0)
        {
            intervalKinds.push_back (juce::jlimit (0, 12, absDelta));
            if (previousDelta != 0 && ((previousDelta > 0) != (delta > 0)))
                ++directionChanges;
            previousDelta = delta;
        }
    }

    std::sort (intervalKinds.begin(), intervalKinds.end());
    intervalKinds.erase (std::unique (intervalKinds.begin(), intervalKinds.end()), intervalKinds.end());

    const float leapShare = (float) leapCount / (float) juce::jmax<size_t> (1, melody.size() - 1);
    const float leapTarget = juce::jlimit (0.05f, 0.62f,
        0.06f + 0.60f * plan.leapBias);
    const float leapFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (leapShare - leapTarget) / 0.46f);

    const float contourTurnRate = (float) directionChanges
        / (float) juce::jmax<size_t> (1, melody.size() - 2);
    const float turnTarget = juce::jlimit (0.10f, 0.72f,
        0.16f + 0.54f * plan.asymmetry);
    const float contourFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (contourTurnRate - turnTarget) / 0.52f);

    const float intervalVariety = juce::jlimit (0.0f, 1.0f,
        (float) intervalKinds.size() / 5.0f);

    const int barsN = juce::jmax (1, sec.bars);
    std::vector<int> barMasks ((size_t) barsN, 0);
    std::vector<float> barPitchMeans ((size_t) barsN, 0.0f);
    std::vector<int> barPitchCounts ((size_t) barsN, 0);
    int offbeats = 0;

    for (const auto* n : melody)
    {
        const int b = juce::jlimit (0, barsN - 1, n->step / 16);
        const int local = juce::jlimit (0, 15, n->step % 16);
        barMasks[(size_t) b] |= (1 << local);
        barPitchMeans[(size_t) b] += (float) n->note;
        ++barPitchCounts[(size_t) b];
        if ((n->step % 4) != 0) ++offbeats;
    }

    int repeatedBars = 0;
    int populatedBars = 0;
    for (int b = 0; b < barsN; ++b)
    {
        if (barPitchCounts[(size_t) b] == 0)
            continue;

        ++populatedBars;
        barPitchMeans[(size_t) b] /= (float) barPitchCounts[(size_t) b];

        bool repeated = false;
        for (int prev = 0; prev < b; ++prev)
            if (barMasks[(size_t) prev] == barMasks[(size_t) b] && barMasks[(size_t) b] != 0)
            {
                repeated = true;
                break;
            }
        if (repeated) ++repeatedBars;
    }

    float repetitionTarget = 0.50f;
    switch (plan.repetitionStyle)
    {
        case 0: repetitionTarget = 0.72f; break;
        case 1: repetitionTarget = 0.42f; break;
        case 2: repetitionTarget = 0.52f; break;
        case 3: repetitionTarget = 0.24f; break;
        case 4: repetitionTarget = 0.64f; break;
        case 5: repetitionTarget = 0.50f; break;
        case 6: repetitionTarget = 0.28f; break;
        case 7: repetitionTarget = 0.70f; break;
        default: break;
    }

    const float repetitionObserved = populatedBars > 1
        ? (float) repeatedBars / (float) (populatedBars - 1) : repetitionTarget;
    const float repetitionFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (repetitionObserved - repetitionTarget) / 0.55f);

    const float offbeatShare = (float) offbeats / (float) melody.size();
    const float offbeatTarget = juce::jlimit (0.18f, 0.86f,
        0.24f + 0.56f * plan.asymmetry);
    const float rhythmFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (offbeatShare - offbeatTarget) / 0.52f);

    float journeyMovement = 0.0f;
    int journeyPairs = 0;
    for (int b = 1; b < barsN; ++b)
    {
        if (barPitchCounts[(size_t) (b - 1)] == 0 || barPitchCounts[(size_t) b] == 0)
            continue;
        journeyMovement += std::abs (barPitchMeans[(size_t) b] - barPitchMeans[(size_t) (b - 1)]);
        ++journeyPairs;
    }
    journeyMovement = journeyPairs > 0
        ? juce::jlimit (0.0f, 1.0f, journeyMovement / (float) journeyPairs / 14.0f)
        : 0.0f;

    const float journeyTarget = (plan.registerJourney == 0)
        ? 0.10f
        : juce::jlimit (0.14f, 0.82f, 0.24f + 0.34f * plan.novelty);
    const float journeyFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (journeyMovement - journeyTarget) / 0.48f);

    const float durationContrast = (float) (shortNotes + longNotes > 0
        ? std::abs (longNotes - shortNotes) / (double) (shortNotes + longNotes) : 0.0);
    const float durationTarget = juce::jlimit (0.08f, 0.92f,
        0.16f + 0.72f * plan.durationContrast);
    const float durationFit = 1.0f - juce::jlimit (0.0f, 1.0f,
        std::abs (durationContrast - durationTarget) / 0.80f);

    return juce::jlimit (0.0f, 1.0f,
        0.18f * intervalVariety
        + 0.18f * leapFit
        + 0.16f * contourFit
        + 0.14f * rhythmFit
        + 0.14f * repetitionFit
        + 0.10f * journeyFit
        + 0.10f * durationFit);
}

float MidiForgeAudioProcessor::loopForgeScore (const Section& sec) const
{

        if (sec.notes.empty()) return -1.0f;

        const auto f = melodyFeatures (sec, generationSeed);
        const float motif = motifMemoryScore (sec);
        const float groove = grooveQualityScore (sec);
        const float rhythmGrammar = rhythmGrammarScore (sec);
        const float melodyExpression = melodyExpressionScore (sec);
        const float harmonicIntelligence = harmonicIntelligenceScore (sec);
        const float phraseMemory4 = phraseMemory4Score (sec);
        const float composerGrammar = composerGrammarScore (sec);
        const float melodicProsody = melodicProsodyScore (sec);
        const float creativeRange = creativeRangeScore (sec, generationSeed);
        const float motifSemantics = motifSemanticsScore (sec, generationSeed);
        const float loopClosure = loopClosureScore (sec, generationSeed);
        int melodyCount = 0;
        int chordCount = 0;
        int bassCount = 0;
        int offbeatMelody = 0;
        int scaleSafe = 0;

        for (const auto& n : sec.notes)
        {
            if (n.channel == 3)
            {
                ++melodyCount;
                if ((n.step % 4) != 0) ++offbeatMelody;
                if (n.note == snapToScale (n.note)) ++scaleSafe;
            }
            else if (n.channel == 1) ++chordCount;
            else if (n.channel == 2) ++bassCount;
        }

        const float densityHealth = melodyCount > 0
            ? juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (f.density - 0.50f) / 0.65f)
            : (melodyType == SparseLeadMelody ? 0.65f : 0.20f);
        const float rhythmicIntent = melodyCount > 0
            ? juce::jlimit (0.0f, 1.0f,
                0.55f * f.rhythmIdentity
                + 0.25f * ((float) offbeatMelody / (float) melodyCount)
                + 0.20f * groove)
            : 0.35f;
        const float scaleSafety = melodyCount > 0
            ? (float) scaleSafe / (float) melodyCount : 1.0f;
        const float layerPresence =
            ((chordCount > 0 || !chordsEnabled) ? 0.5f : 0.0f)
            + ((bassCount > 0 || !bassEnabled) ? 0.5f : 0.0f);

        return
            0.24f * f.loopQuality
            + 0.16f * f.context
            + 0.14f * f.tensionArc
            + 0.10f * f.phraseArc
            + 0.12f * motif
            + 0.10f * groove
            + 0.06f * densityHealth
            + 0.04f * rhythmicIntent
            + 0.10f * rhythmGrammar
            + 0.10f * melodyExpression
            + 0.10f * harmonicIntelligence
            + 0.08f * phraseMemory4
            + 0.05f * melodicProsody
            + 0.07f * creativeRange
            + 0.06f * motifSemantics
            + 0.06f * loopClosure
            + 0.02f * scaleSafety
            + 0.02f * layerPresence;
    
}

float MidiForgeAudioProcessor::composerJudgeScore (const Section& section, uint32_t identity,
                                                    const ComposerJudgeInputs& inputs) const
{
    if (section.notes.empty() || section.bars < 1)
        return 0.28f;

    std::vector<const NoteEvent*> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    if (melody.size() < 3)
        return 0.35f;

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    const int barsN = juce::jmax (1, section.bars);
    const auto& features = inputs.features;
    const auto grammar = midiforge::ComposerGrammar::makePlan (
        barsN, energy, complexity, melodyType, mood, genre, identity);

    std::vector<int> counts ((size_t) barsN, 0);
    std::vector<float> meanPitch ((size_t) barsN, 0.0f);
    std::vector<float> meanVelocity ((size_t) barsN, 0.0f);
    std::vector<int> offbeats ((size_t) barsN, 0);

    for (const auto* n : melody)
    {
        const int b = juce::jlimit (0, barsN - 1, n->step / 16);
        ++counts[(size_t) b];
        meanPitch[(size_t) b] += (float) n->note;
        meanVelocity[(size_t) b] += (float) n->velocity;
        if ((n->step % 4) != 0)
            ++offbeats[(size_t) b];
    }

    const int maxBarCount = *std::max_element (counts.begin(), counts.end());
    const float safeMaxCount = (float) juce::jmax (4, maxBarCount);

    float roleSum = 0.0f;
    int roleCount = 0;

    for (int b = 0; b < barsN; ++b)
    {
        const auto state = grammar.stateFor (b / 4);
        const float actualDensity = juce::jlimit (
            0.0f, 1.0f, (float) counts[(size_t) b] / safeMaxCount);

        const float actualSpace = 1.0f - actualDensity;

        float actualTension = 0.46f;
        if (counts[(size_t) b] > 0)
        {
            meanPitch[(size_t) b] /= (float) counts[(size_t) b];
            meanVelocity[(size_t) b] /= (float) counts[(size_t) b];

            const float velocityPart = juce::jlimit (
                0.0f, 1.0f, (meanVelocity[(size_t) b] - 48.0f) / 58.0f);
            const float registerPart = juce::jlimit (
                0.0f, 1.0f,
                ((meanPitch[(size_t) b] - 48.0f) / 36.0f));
            const float rhythmicPart = (float) offbeats[(size_t) b]
                / (float) juce::jmax (1, counts[(size_t) b]);

            actualTension = juce::jlimit (
                0.0f, 1.0f,
                0.42f * velocityPart
                + 0.28f * registerPart
                + 0.30f * rhythmicPart);
        }

        const float densityFit = 1.0f - std::abs (actualDensity - state.density);
        const float spaceFit = 1.0f - std::abs (actualSpace - state.space);
        const float tensionFit = 1.0f - std::abs (actualTension - state.tension);

        roleSum += juce::jlimit (
            0.0f, 1.0f,
            0.48f * densityFit
            + 0.16f * spaceFit
            + 0.36f * tensionFit);
        ++roleCount;
    }

    const float roleConsistency = roleCount > 0
        ? roleSum / (float) roleCount : 0.5f;

    float arc = 0.50f;
    if (barsN >= 4)
    {
        const int phraseBars = juce::jmin (4, barsN);
        std::vector<float> p ((size_t) phraseBars, 0.0f);
        std::vector<int> pc ((size_t) phraseBars, 0);

        for (int b = 0; b < phraseBars; ++b)
        {
            for (const auto* n : melody)
            {
                if (n->step / 16 != b)
                    continue;
                p[(size_t) b] += (float) n->note;
                ++pc[(size_t) b];
            }
            if (pc[(size_t) b] > 0)
                p[(size_t) b] /= (float) pc[(size_t) b];
        }

        const float rise = pc[2] > 0 && pc[0] > 0 ? p[2] - p[0] : 0.0f;
        const float release = pc[2] > 0 && pc[3] > 0 ? p[2] - p[3] : 0.0f;
        const float riseFit = 1.0f - juce::jlimit (
            0.0f, 1.0f, std::abs (rise - 2.0f) / 10.0f);
        const float releaseFit = 1.0f - juce::jlimit (
            0.0f, 1.0f, std::abs (release - 1.0f) / 9.0f);
        arc = juce::jlimit (
            0.0f, 1.0f,
            0.52f * features.phraseArc
            + 0.24f * features.tensionArc
            + 0.14f * riseFit
            + 0.10f * releaseFit);
    }
    else
    {
        arc = 0.62f * features.phraseArc + 0.38f * features.tensionArc;
    }

    const auto creativePlan = midiforge::CreativeRange::makePlan (
        melodyType, mood, genre, energy, complexity, identity);

    const float actualNovelty = juce::jlimit (
        0.0f, 1.0f, 0.55f * features.variety + 0.45f * features.surprise);
    const float noveltyTarget = juce::jlimit (
        0.12f, 0.88f, 0.28f + 0.48f * creativePlan.novelty);
    const float noveltyBalance = 1.0f - juce::jlimit (
        0.0f, 1.0f, std::abs (actualNovelty - noveltyTarget) / 0.58f);

    const float motifDevelopment = juce::jlimit (
        0.0f, 1.0f,
        0.46f * inputs.motifSemantics
        + 0.30f * inputs.motifMemory
        + 0.24f * inputs.development);

    const float expression = juce::jlimit (
        0.0f, 1.0f,
        0.58f * inputs.melodyExpression
        + 0.42f * inputs.melodicProsody);

    const float harmony = inputs.harmonicIntelligence;
    const float prosody = inputs.melodicProsody;
    const float closure = juce::jlimit (
        0.0f, 1.0f, 0.62f * inputs.loopClosure
        + 0.38f * features.seam);

    const float densityTarget = juce::jlimit (
        0.16f, 0.90f, soundProfileFor (soundTarget).densityMul * 0.68f);
    const float densityFit = 1.0f - juce::jlimit (
        0.0f, 1.0f, std::abs (features.density - densityTarget) / 0.54f);
    const float spaceFit = juce::jlimit (
        0.0f, 1.0f, features.space);
    const float densitySpace = 0.64f * densityFit + 0.36f * spaceFit;

    const float rhythm = juce::jlimit (
        0.0f, 1.0f, 0.52f * inputs.rhythmGrammar
        + 0.28f * features.rhythmIdentity
        + 0.20f * inputs.grooveQuality);

    const float groove = inputs.grooveQuality;

    return midiforge::ComposerJudge::score ({
        juce::jlimit (0.0f, 1.0f, arc),
        juce::jlimit (0.0f, 1.0f, roleConsistency),
        juce::jlimit (0.0f, 1.0f, motifDevelopment),
        juce::jlimit (0.0f, 1.0f, expression),
        juce::jlimit (0.0f, 1.0f, harmony),
        juce::jlimit (0.0f, 1.0f, densitySpace),
        juce::jlimit (0.0f, 1.0f, rhythm),
        juce::jlimit (0.0f, 1.0f, features.registerScore),
        juce::jlimit (0.0f, 1.0f, closure),
        juce::jlimit (0.0f, 1.0f, noveltyBalance),
        juce::jlimit (0.0f, 1.0f, groove),
        juce::jlimit (0.0f, 1.0f, prosody),
        juce::jlimit (0.0f, 1.0f, inputs.development)
    });
}


void MidiForgeAudioProcessor::finalizeLoop (Section& sec) const
{
        const auto profile = soundProfileFor (soundTarget);
        const bool solo808 = profile.soloLine;

        int melodyLo = 28, melodyHi = 50, melodyMaxLeap = 7;
        melodyRegisterContract (melodyLo, melodyHi, melodyMaxLeap);

        for (auto& n : sec.notes)
        {
            if (n.channel == 3)
            {
                n.note = foldIntoLane (snapToScale (n.note), melodyLo, melodyHi);
                n.velocity = juce::jlimit (30, 122, n.velocity);
            }
            else
            {
                if (n.channel == 2)
                    n.note = juce::jlimit (28, 52, n.note);
                n.velocity = juce::jlimit (25, 122, n.velocity);
            }
            n.length = juce::jlimit (1, 16, n.length);
        }

        // 0.65 structural invariants: these are repaired here because the
        // candidate archetypes may legally mutate timing after addBass/add808.
        const auto prog = progressionDegrees();
        for (int bar = 0; bar < juce::jmax (1, sec.bars); ++bar)
        {
            const int start = bar * 16;
            const int rootPitch = !prog.empty()
                ? foldIntoLane (degreeToPitch (prog[(size_t) (bar % (int) prog.size())], 2),
                                28, solo808 ? 50 : 52)
                : 28;

            if (bassEnabled && !solo808)
            {
                size_t anchor = (size_t) -1;
                size_t earliest = (size_t) -1;
                for (size_t i = 0; i < sec.notes.size(); ++i)
                {
                    const auto& n = sec.notes[i];
                    if (n.channel != 2 || n.step / 16 != bar) continue;
                    if (earliest == (size_t) -1 || n.step < sec.notes[earliest].step) earliest = i;
                    if (n.step == start) { anchor = i; break; }
                }
                if (anchor == (size_t) -1 && earliest != (size_t) -1)
                {
                    sec.notes[earliest].step = start;
                    sec.notes[earliest].note = rootPitch;
                    sec.notes[earliest].length = juce::jmin (7, sec.notes[earliest].length);
                }
                else if (anchor == (size_t) -1)
                {
                    sec.notes.push_back ({ start, 7, rootPitch, 98, 2, false });
                }
            }

            if (drumsEnabled)
            {
                size_t kickAnchor = (size_t) -1;
                size_t earliestKick = (size_t) -1;
                for (size_t i = 0; i < sec.notes.size(); ++i)
                {
                    const auto& n = sec.notes[i];
                    if (n.channel != 5 || n.note != 36 || n.step / 16 != bar) continue;
                    if (earliestKick == (size_t) -1 || n.step < sec.notes[earliestKick].step) earliestKick = i;
                    if (n.step == start) { kickAnchor = i; break; }
                }
                if (kickAnchor == (size_t) -1 && earliestKick != (size_t) -1)
                    sec.notes[earliestKick].step = start;
                else if (kickAnchor == (size_t) -1)
                    sec.notes.push_back ({ start, 1, 36, 114, 5, false });
            }

            if (solo808)
            {
                size_t anchor = (size_t) -1;
                size_t earliest = (size_t) -1;
                for (size_t i = 0; i < sec.notes.size(); ++i)
                {
                    const auto& n = sec.notes[i];
                    if (n.channel != 3 || n.step / 16 != bar) continue;
                    if (earliest == (size_t) -1 || n.step < sec.notes[earliest].step) earliest = i;
                    if (n.step == start) { anchor = i; break; }
                }
                if (anchor == (size_t) -1 && earliest != (size_t) -1)
                {
                    sec.notes[earliest].step = start;
                    sec.notes[earliest].note = rootPitch;
                }
                else if (anchor != (size_t) -1)
                    sec.notes[anchor].note = rootPitch;
                else
                    sec.notes.push_back ({ start, 5, rootPitch, 104, 3, false });
            }
        }

        // When drums are enabled, keep a one-voice 808 genuinely kick-locked
        // in both directions: every 808 onset has a kick and every kick has a
        // corresponding 808 onset. We add the missing side instead of shifting
        // an existing musical phrase.
        if (solo808 && drumsEnabled)
        {
            std::vector<int> kickSteps;
            for (const auto& n : sec.notes)
                if (n.channel == 5 && n.note == 36)
                    kickSteps.push_back (n.step);
            std::sort (kickSteps.begin(), kickSteps.end());
            kickSteps.erase (std::unique (kickSteps.begin(), kickSteps.end()), kickSteps.end());

            const auto hasKickAt = [&] (int step)
            {
                return std::find (kickSteps.begin(), kickSteps.end(), step) != kickSteps.end();
            };
            const auto has808At = [&] (int step)
            {
                for (const auto& n : sec.notes)
                    if (n.channel == 3 && n.step == step)
                        return true;
                return false;
            };

            const size_t noteCountBefore = sec.notes.size();
            for (size_t i = 0; i < noteCountBefore; ++i)
            {
                const auto& n = sec.notes[i];
                if (n.channel != 3 || hasKickAt (n.step))
                    continue;
                sec.notes.push_back ({ n.step, 1, 36, 104, 5, false });
                kickSteps.push_back (n.step);
            }

            std::vector<int> missing808;
            for (const int kickStep : kickSteps)
                if (!has808At (kickStep))
                    missing808.push_back (kickStep);

            for (const int step : missing808)
            {
                const int bar = juce::jlimit (0, juce::jmax (0, sec.bars - 1), step / 16);
                const int degree = !prog.empty()
                    ? prog[(size_t) (bar % (int) prog.size())]
                    : 0;
                const int targetRoot = foldIntoLane (degreeToPitch (degree, 2), 28, 50);

                int note = targetRoot;
                int nearest = 999;
                for (const auto& n : sec.notes)
                {
                    if (n.channel != 3)
                        continue;
                    const int dist = std::abs (n.step - step);
                    if (dist < nearest)
                    {
                        nearest = dist;
                        note = n.note;
                    }
                }
                note = foldIntoLane (note, 28, 50);
                sec.notes.push_back ({ step, 1, note, 104, 3, false });
            }
        }

        // Final shared melodic register contract. This runs after all
        // structural repairs so later candidate transforms cannot leave the
        // accepted melody outside the same bounds used by earlier safety stages.
        {
            std::vector<size_t> melody;
            for (size_t i = 0; i < sec.notes.size(); ++i)
                if (sec.notes[i].channel == 3)
                    melody.push_back (i);

            std::stable_sort (melody.begin(), melody.end(),
                [&] (size_t a, size_t b)
                {
                    if (sec.notes[a].step != sec.notes[b].step)
                        return sec.notes[a].step < sec.notes[b].step;
                    return sec.notes[a].note < sec.notes[b].note;
                });

            int previous = -1;
            for (const auto index : melody)
            {
                auto& n = sec.notes[index];
                n.note = foldIntoLane (snapToScale (n.note), melodyLo, melodyHi);
                if (previous >= 0)
                {
                    const int boundedLo = juce::jmax (melodyLo, previous - melodyMaxLeap);
                    const int boundedHi = juce::jmin (melodyHi, previous + melodyMaxLeap);
                    n.note = juce::jlimit (boundedLo, boundedHi, snapToScale (n.note));
                }
                previous = n.note;
            }
        }

        removeDuplicateNotes (sec.notes);
        cleanMelodyLine (sec.notes);
        std::sort (sec.notes.begin(), sec.notes.end(),
                   [] (const NoteEvent& a, const NoteEvent& b)
                   {
                       if (a.step != b.step) return a.step < b.step;
                       if (a.channel != b.channel) return a.channel < b.channel;
                       return a.note < b.note;
                   });
    
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

    

    

    // 0.63 Melodic Memory 3.0: capture the actual musical idea of the
    // candidate separately from its general quality/behavior. The fingerprint
    // is transposition-safe and small enough to compare across the 1000-candidate
    // search without adding a second generator.
    

    

    

    // 0.53 Motif Memory 2.0: remember more than a single interval pattern.
    // A loop can carry a main motif, a secondary answer, a rhythmic fingerprint
    // and a bass fingerprint. Matching is transposition-safe and tolerates small
    // human changes, so a motif can evolve without becoming a literal copy.
    

    // 0.54 Groove Engine: one loop-specific pocket is shared by every layer.
    // The groove is generated once from the candidate identity, then applied with
    // different strengths so melody/bass/drums move together without becoming a
    // rigid quantized block.
    

    

    

    // 0.59.1 Judge Diversity Gate: compare behavioral fingerprints as well as
    // literal note similarity. This stops the final eight from becoming eight
    // versions of the same musical behavior just because their archetype labels
    // differ.
    

    int mLo = 62, mHi = 86;
    registerLane(2, mLo, mHi);
    

    // MAGIC 3.0: multi-archetype search. Each candidate is born into a
    // deliberate musical archetype instead of competing in one generic pool.
    static constexpr const char* archetypeNames[8] =
    {
        "HOOK", "GROOVE", "HARMONY", "MOTIF",
        "MINIMAL", "WEIRD", "EMOTIONAL", "WILDCARD"
    };
    const bool sparseTypeAllowed = (melodyType == SparseLeadMelody || genre == Ambient);
    const auto judgeProf = soundProfileFor(soundTarget);

#ifdef MIDIFORGE_HEADLESS
    struct MelodyPipelineStageStats
    {
        const char* name = "";
        int minPitch = 127;
        int maxPitch = 0;
        int maxLeap = 0;
        int invalidPitches = 0;
        int outsideContract = 0;
        int melodyNotes = 0;
    };

    static bool pipelineTraceDone = false;
    const bool traceRequested = std::getenv ("MIDIFORGE_PIPELINE_TRACE") != nullptr;
    const bool traceThisGeneration = traceRequested
        && ! pipelineTraceDone
        && soundTarget == 0
        && bars == 4
        && melodyType == 0
        && std::abs (complexity - 0.68f) < 0.001f
        && std::abs (energy - 0.72f) < 0.001f;

    std::array<MelodyPipelineStageStats, 9> pipelineTrace {};
    if (traceThisGeneration)
    {
        static constexpr const char* names[9] =
        {
            "flatten", "archetype", "rhythm", "expression", "memory",
            "prosody", "harmony", "groove", "foundation+pleasantness"
        };
        for (size_t i = 0; i < pipelineTrace.size(); ++i)
            pipelineTrace[i].name = names[i];
    }

    auto traceMelodyStage = [&] (size_t stage, const Section& section)
    {
        if (! traceThisGeneration || stage >= pipelineTrace.size())
            return;

        auto& stats = pipelineTrace[stage];
        int previous = -1;
        int laneLo = 40, laneHi = 96, contractLeap = 9;
        melodyRegisterContract (laneLo, laneHi, contractLeap);

        std::vector<const NoteEvent*> melody;
        for (const auto& n : section.notes)
            if (n.channel == 3)
                melody.push_back (&n);

        std::stable_sort (melody.begin(), melody.end(),
            [] (const NoteEvent* a, const NoteEvent* b)
            {
                if (a->step != b->step) return a->step < b->step;
                return a->note < b->note;
            });

        stats.melodyNotes += (int) melody.size();
        for (const auto* n : melody)
        {
            if (n->note < 0 || n->note > 127)
            {
                ++stats.invalidPitches;
                continue;
            }

            stats.minPitch = juce::jmin (stats.minPitch, n->note);
            stats.maxPitch = juce::jmax (stats.maxPitch, n->note);

            if (n->note < laneLo || n->note > laneHi)
                ++stats.outsideContract;

            if (previous >= 0)
                stats.maxLeap = juce::jmax (stats.maxLeap, std::abs (n->note - previous));
            previous = n->note;
        }
    };
#else
    const bool traceThisGeneration = false;
    const auto traceMelodyStage = [] (size_t, const Section&) {};
#endif

    std::vector<Candidate> candidates;
    std::vector<taste::Vec> tasteFeatures;
    constexpr int candidateCount = 1000;
    constexpr int firstPassCandidates = 600;
    candidates.reserve(candidateCount);
    tasteFeatures.reserve(candidateCount);
    AdaptiveProfile adaptive;
    for(int c=0;c<candidateCount;++c)
    {
        if (c == firstPassCandidates)
            buildAdaptiveProfile(candidates, firstPassCandidates, adaptive);

        const uint32_t identity=hash32(generationSeed ^ (uint32_t)(c+1)*0x45d9f3bu);
        juce::Random local((juce::int64)identity);
        SongData song;
        const float oldVariation = variationAmount;
        const float oldMelodyDensity = melodyDensity;
        const float oldPauseChance = pauseChance;
        const float oldLeapChance = leapChance;
        const float oldMotifStrength = motifStrength;
        const float oldComplexity = complexity;
        const float oldSwing = swing;
        const int oldOctave = octave;
        const float randomJitter = ((float) ((identity >> 8) % 1000u) / 1000.0f - 0.5f);

        // First pass explores broadly. After 600 candidates, MAGIC 4 nudges the
        // same generator toward the best discovered feature neighborhood while
        // retaining deterministic local jitter so it keeps exploring.
        if (adaptive.ready)
        {
            const float adapt = 0.62f;
            const float targetDensity = juce::jlimit (0.16f, 0.88f,
                adaptive.density + randomJitter * 0.12f);
            const float targetLeap = juce::jlimit (0.04f, 0.48f,
                adaptive.leap + randomJitter * 0.12f);
            const float targetMotif = juce::jlimit (0.35f, 0.98f,
                adaptive.motif + randomJitter * 0.10f);
            const float targetSurprise = juce::jlimit (0.10f, 0.90f,
                adaptive.surprise + randomJitter * 0.14f);
            melodyDensity = juce::jlimit (0.10f, 0.92f,
                oldMelodyDensity * (1.0f - adapt) + targetDensity * adapt);
            pauseChance = juce::jlimit (0.02f, 0.46f,
                (1.0f - melodyDensity) * 0.34f + adaptive.space * 0.16f);
            leapChance = juce::jlimit (0.03f, 0.52f,
                oldLeapChance * (1.0f - adapt) + targetLeap * adapt);
            motifStrength = juce::jlimit (0.30f, 0.99f,
                oldMotifStrength * (1.0f - adapt) + targetMotif * adapt);
            complexity = juce::jlimit (0.14f, 0.94f,
                oldComplexity * (1.0f - adapt) + targetSurprise * adapt);
            swing = juce::jlimit (0.0f, 0.55f,
                oldSwing * (1.0f - adapt) + adaptive.rhythm * 0.42f * adapt);
            octave = juce::jlimit (2, 6, oldOctave + (adaptive.reg > 0.62f ? 1 : adaptive.reg < 0.30f ? -1 : 0));
        }

        variationAmount = juce::jlimit (0.f, 1.f,
            oldVariation + randomJitter * (adaptive.ready ? 0.11f : 0.035f));
        buildBaseSong(song,local,c+1);

        variationAmount = oldVariation;
        melodyDensity = oldMelodyDensity;
        pauseChance = oldPauseChance;
        leapChance = oldLeapChance;
        motifStrength = oldMotifStrength;
        complexity = oldComplexity;
        swing = oldSwing;
        octave = oldOctave;

        const int archetype = c % 8;
        Section flat=flatten(song,c,local,mLo,mHi);
        traceMelodyStage (0, flat);

        applyMagicArchetype (flat, archetype, identity);
        traceMelodyStage (1, flat);

        applyRhythmGrammar (flat, identity);
        traceMelodyStage (2, flat);

        applyMelodyExpression (flat, identity);
        traceMelodyStage (3, flat);

        applyPhraseMemory4 (flat, identity);
        traceMelodyStage (4, flat);

        applyMelodicProsody (flat, identity);
        traceMelodyStage (5, flat);

        applyHarmonicIntelligence (flat, identity);
        traceMelodyStage (6, flat);

        applyGrooveEngine (flat, identity);
        traceMelodyStage (7, flat);

        applyMelodyFoundation (flat, identity);
        applyMelodyPleasantness (flat, identity);
        traceMelodyStage (8, flat);
        const auto f=melodyFeatures(flat,identity);
        const float grooveQuality = grooveQualityScore (flat);
        const float motifMemory = motifMemoryScore(flat);
        const float rhythmGrammarQuality = rhythmGrammarScore (flat);
        const float melodyExpressionQuality = melodyExpressionScore (flat);
        const float harmonicIntelligenceQuality = harmonicIntelligenceScore (flat);
        const float phraseMemory4Quality = phraseMemory4Score (flat);
        const float composerGrammarQuality = composerGrammarScore (flat);
        const float melodicProsodyQuality = melodicProsodyScore (flat);
        const float creativeRangeQuality = creativeRangeScore (flat, identity);

        // 0.64 Development Judge: reward a phrase that develops an identity
        // instead of either copying bar 1 or abandoning it completely.
        auto developmentCoherence = [&] (const Section& sec)
        {
            const int barsN = juce::jmax (1, sec.bars);
            if (barsN < 4)
                return 0.55f;

            auto similarity = [] (const std::vector<const NoteEvent*>& a,
                                  const std::vector<const NoteEvent*>& b)
            {
                if (a.size() < 2 || b.size() < 2)
                    return 0.0f;

                const size_t n = juce::jmin (a.size(), b.size());
                int onsetHits = 0, pitchHits = 0, intervalHits = 0, directionHits = 0;
                const int aAnchor = a.front()->note;
                const int bAnchor = b.front()->note;

                for (size_t i = 0; i < n; ++i)
                {
                    if (std::abs ((a[i]->step % 16) - (b[i]->step % 16)) <= 1) ++onsetHits;
                    if (std::abs ((a[i]->note - aAnchor) - (b[i]->note - bAnchor)) <= 2) ++pitchHits;
                }

                const size_t ni = juce::jmin (a.size() - 1, b.size() - 1);
                for (size_t i = 1; i <= ni; ++i)
                {
                    const int ia = a[i]->note - a[i - 1]->note;
                    const int ib = b[i]->note - b[i - 1]->note;
                    if (ia == ib || std::abs (ia - ib) <= 1) ++intervalHits;
                    if ((ia == 0 && ib == 0) || (ia > 0 && ib > 0) || (ia < 0 && ib < 0))
                        ++directionHits;
                }

                const float onsetFit = (float) onsetHits / (float) n;
                const float pitchFit = (float) pitchHits / (float) n;
                const float intervalFit = ni > 0 ? (float) intervalHits / (float) ni : pitchFit;
                const float directionFit = ni > 0 ? (float) directionHits / (float) ni : 0.5f;
                const float countFit = 1.0f - juce::jlimit (0.0f, 1.0f,
                    (float) std::abs ((int) a.size() - (int) b.size()) / 5.0f);

                return juce::jlimit (0.0f, 1.0f,
                    0.30f * onsetFit
                    + 0.30f * pitchFit
                    + 0.22f * intervalFit
                    + 0.08f * directionFit
                    + 0.10f * countFit);
            };

            float sum = 0.0f;
            int phraseCount = 0;
            for (int start = 0; start + 3 < barsN; start += 4)
            {
                std::array<std::vector<const NoteEvent*>, 4> phraseBars;
                for (const auto& n : sec.notes)
                {
                    if (n.channel != 3) continue;
                    const int local = n.step / 16 - start;
                    if (local >= 0 && local < 4)
                        phraseBars[(size_t) local].push_back (&n);
                }

                for (auto& v : phraseBars)
                    std::stable_sort (v.begin(), v.end(),
                        [] (const NoteEvent* a, const NoteEvent* b)
                        {
                            if (a->step != b->step) return a->step < b->step;
                            return a->note < b->note;
                        });

                if (phraseBars[0].size() < 2
                    || phraseBars[1].empty()
                    || phraseBars[2].empty()
                    || phraseBars[3].empty())
                    continue;

                const float aPrime = similarity (phraseBars[0], phraseBars[1]);
                const float bContrast = similarity (phraseBars[0], phraseBars[2]);
                const float aReturn = similarity (phraseBars[0], phraseBars[3]);

                const float aPrimeFit = 1.0f
                    - juce::jlimit (0.0f, 1.0f, std::abs (aPrime - 0.70f) / 0.55f);
                const float bFit = 1.0f
                    - juce::jlimit (0.0f, 1.0f, std::abs (bContrast - 0.38f) / 0.45f);
                const float returnFit = 1.0f
                    - juce::jlimit (0.0f, 1.0f, std::abs (aReturn - 0.68f) / 0.50f);

                const float contrastOrdering = juce::jlimit (0.0f, 1.0f,
                    0.5f + 1.2f * (aPrime - bContrast));
                const float returnOrdering = juce::jlimit (0.0f, 1.0f,
                    0.5f + 1.0f * (aReturn - bContrast));

                sum += 0.30f * aPrimeFit
                     + 0.34f * bFit
                     + 0.26f * returnFit
                     + 0.05f * contrastOrdering
                     + 0.05f * returnOrdering;
                ++phraseCount;
            }

            return phraseCount > 0
                ? juce::jlimit (0.0f, 1.0f, sum / (float) phraseCount)
                : 0.55f;
        };

        const float development = developmentCoherence (flat);
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
        quality += 0.13f * development;
        quality += 0.09f * rhythmGrammarQuality;
        quality += 0.10f * melodyExpressionQuality;
        quality += 0.10f * harmonicIntelligenceQuality;
        quality += 0.08f * phraseMemory4Quality;
        quality += 0.07f * composerGrammarQuality;
        quality += 0.07f * melodicProsodyQuality;
        quality += 0.08f * creativeRangeQuality;
        quality += 0.05f*f.registerScore;
        quality += 0.05f*f.surprise;

        // 0.61 Tempo Feel Judge: score the generated loop in the same temporal
        // language that the generator used. The old judge rewarded progressively
        // emptier loops as BPM increased, which could erase the fast-tempo work done
        // by the generator during MAGIC re-ranking.
        {
            const double bpm = juce::jlimit (40.0, 240.0, currentBpm.load());
            const float fast = juce::jlimit (0.0f, 1.0f, ((float)bpm - 120.0f) / 60.0f);
            const float slow = juce::jlimit (0.0f, 1.0f, (120.0f - (float)bpm) / 60.0f);
            const float veryFast = juce::jlimit (0.0f, 1.0f, ((float)bpm - 170.0f) / 50.0f);

            const float targetDensity = juce::jlimit (0.18f, 0.88f,
                0.70f + 0.04f * slow + 0.05f * fast + 0.03f * veryFast);
            const float targetSpace = juce::jlimit (0.20f, 0.84f,
                0.40f - 0.04f * fast - 0.03f * veryFast + 0.04f * slow);

            const float densityFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (f.density - targetDensity) / 0.46f);
            const float spaceFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (f.space - targetSpace) / 0.56f);

            // Estimate melodic note-rate from the actual loop duration. This
            // distinguishes "same notes per bar" from "same perceived activity".
            int melodyCount = 0;
            const int loopBars = juce::jmax (1, flat.bars);
            for (const auto& ev : flat.notes)
                if (ev.channel == 3)
                    ++melodyCount;
            const float loopSeconds = (float)loopBars * 4.0f * 60.0f / (float)juce::jmax (40.0, bpm);
            const float notesPerSecond = loopSeconds > 0.0f
                ? (float)melodyCount / loopSeconds
                : 0.0f;
            const float targetNotesPerSecond = juce::jlimit (1.0f, 6.5f,
                1.65f + 0.0125f * (float)bpm
                + (melodyType == RiffMelody ? 0.45f : 0.0f)
                + (melodyType == SparseLeadMelody ? -0.45f : 0.0f));
            const float rateFit = 1.0f
                - juce::jlimit (0.0f, 1.0f,
                    std::abs (notesPerSecond - targetNotesPerSecond)
                    / juce::jmax (1.5f, 1.8f + 0.9f * fast));

            // At fast BPM, 1/16 and offbeat activity are useful evidence of a
            // genuinely tempo-native line. Measure them directly from the MIDI.
            int melodySixteenths = 0;
            int melodyOffbeats = 0;
            for (const auto& ev : flat.notes)
            {
                if (ev.channel != 3) continue;
                if ((ev.step & 1) != 0) ++melodySixteenths;
                if ((ev.step % 4) != 0) ++melodyOffbeats;
            }
            const float sixteenthRatio = melodyCount > 0
                ? (float)melodySixteenths / (float)melodyCount : 0.0f;
            const float offbeatRatio = melodyCount > 0
                ? (float)melodyOffbeats / (float)melodyCount : 0.0f;
            const float targetSixteenth = juce::jlimit (0.18f, 0.82f,
                0.24f + 0.34f * fast + 0.20f * veryFast);
            const float targetOffbeat = juce::jlimit (0.20f, 0.82f,
                0.32f + 0.10f * fast + 0.10f * veryFast);
            const float subdivisionFit = 1.0f
                - juce::jlimit (0.0f, 1.0f,
                    std::abs (sixteenthRatio - targetSixteenth) / 0.62f);
            const float offbeatFit = 1.0f
                - juce::jlimit (0.0f, 1.0f,
                    std::abs (offbeatRatio - targetOffbeat) / 0.58f);

            quality += 0.045f * densityFit
                     + 0.028f * spaceFit
                     + 0.042f * rateFit
                     + 0.030f * subdivisionFit
                     + 0.022f * offbeatFit;
        }

        // Loop Quality 2.0: judge the circular behavior of the whole loop while
        // keeping the existing MAGIC DNA/archetype system in control.
        quality += 0.22f*f.loopQuality;
        quality += 0.17f * grooveQuality;
        // Motif Memory 2.0 becomes a shared signal for every archetype. The
        // dedicated MOTIF archetype gets a stronger weight above, while the
        // rest still benefit from recognizable identity without being forced
        // into literal repetition.
        quality += 0.18f * motifMemory;
        quality += 0.075f * f.context;
        quality += 0.095f * f.tensionArc;
        quality += 0.045f*melodyFit + 0.045f*rhythmFit + 0.045f*motifFit;
        quality += 0.030f*registerFit + 0.025f*surpriseFit;

        // MAGIC 4 adaptive exploitation: only the second search phase receives
        // this bonus. The first phase remains the broad explorer that discovers
        // the promising region in the first place.
        if (adaptive.ready)
        {
            const float adaptiveFit =
                0.17f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.density - adaptive.density)))
                + 0.12f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.space - adaptive.space)))
                + 0.14f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.rhythmIdentity - adaptive.rhythm)))
                + 0.18f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.motifIdentity - adaptive.motif)))
                + 0.10f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.leap - adaptive.leap)))
                + 0.08f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.registerScore - adaptive.reg)))
                + 0.10f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.surprise - adaptive.surprise)))
                + 0.06f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.loopQuality - adaptive.loop)))
                + 0.05f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (grooveQuality - adaptive.groove)))
                + 0.04f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (motifMemory - adaptive.memory)));
            quality += 0.20f * adaptiveFit;
        }

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

        // 0.59 Musical Judge 2.0:
        // Evaluate the loop as one musical statement instead of a sum of mostly
        // independent metrics. The judge explicitly balances identity, tension,
        // novelty, contour, groove, harmony, register, layer interaction, seam
        // continuity and density trajectory, then adds a small anti-boredom gate.
        {
            const int barsN = juce::jmax (1, flat.bars);
            std::vector<std::vector<const NoteEvent*>> melodyBars ((size_t) barsN);
            std::vector<std::vector<int>> chordPcs ((size_t) barsN);
            std::vector<std::vector<const NoteEvent*>> bassBars ((size_t) barsN);

            for (const auto& n : flat.notes)
            {
                const int b = juce::jlimit (0, barsN - 1, n.step / 16);
                if (n.channel == 1)
                {
                    const int pc = (n.note % 12 + 12) % 12;
                    if (std::find (chordPcs[(size_t) b].begin(),
                                   chordPcs[(size_t) b].end(), pc) == chordPcs[(size_t) b].end())
                        chordPcs[(size_t) b].push_back (pc);
                }
                else if (n.channel == 2)
                {
                    bassBars[(size_t) b].push_back (&n);
                }
                else if (n.channel == 3)
                {
                    melodyBars[(size_t) b].push_back (&n);
                }
            }

            for (auto& v : melodyBars)
                std::stable_sort (v.begin(), v.end(),
                    [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });
            for (auto& v : bassBars)
                std::stable_sort (v.begin(), v.end(),
                    [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

            // 1) Identity: keep a recognizable idea, but not literal copying.
            const float identityScore = juce::jlimit (0.0f, 1.0f,
                0.56f * motifMemory + 0.24f * f.motifIdentity + 0.20f * f.phraseMemory);

            // 2) Repetition vs novelty: reward a useful middle ground instead
            // of always preferring either maximum repetition or maximum chaos.
            const float novelty = juce::jlimit (0.0f, 1.0f,
                0.48f * f.variety + 0.30f * f.surprise + 0.22f * f.contour);
            const float identityNoveltyBalance = 1.0f
                - juce::jlimit (0.0f, 1.0f,
                    std::abs ((0.62f * identityScore + 0.38f * novelty) - 0.58f) / 0.58f);

            // 3) Tension/release and phrase contour.
            const float tensionRelease = juce::jlimit (0.0f, 1.0f,
                0.62f * f.tensionArc + 0.38f * f.phraseArc);

            float barPitch[8] = {};
            int barPitchCount[8] = {};
            int barMelodyCount[8] = {};
            for (int b = 0; b < juce::jmin (8, barsN); ++b)
            {
                for (const auto* n : melodyBars[(size_t) b])
                {
                    barPitch[b] += (float) n->note;
                    ++barPitchCount[b];
                    ++barMelodyCount[b];
                }
            }

            float contourScore = 0.50f;
            float densityTrajectory = 0.50f;
            if (barsN >= 4)
            {
                for (int b = 0; b < 4; ++b)
                    if (barPitchCount[b] > 0) barPitch[b] /= (float) barPitchCount[b];

                const float peakRise = barPitch[2] - barPitch[0];
                const float returnDrop = barPitch[2] - barPitch[3];
                const float riseFit = juce::jlimit (0.0f, 1.0f,
                    1.0f - std::abs (peakRise - 2.0f) / 9.0f);
                const float releaseFit = juce::jlimit (0.0f, 1.0f,
                    1.0f - std::abs (returnDrop - 1.0f) / 8.0f);

                const float localTurn = std::abs (barPitch[1] - barPitch[0])
                                      + std::abs (barPitch[2] - barPitch[1]);
                const float usefulTurn = juce::jlimit (0.0f, 1.0f, localTurn / 8.0f);
                contourScore = juce::jlimit (0.0f, 1.0f,
                    0.46f * riseFit + 0.34f * releaseFit + 0.20f * usefulTurn);

                const float peakDensity = (float) barMelodyCount[2];
                const float firstDensity = (float) barMelodyCount[0];
                const float returnDensity = (float) barMelodyCount[3];
                const float peakLift = juce::jlimit (0.0f, 1.0f,
                    (peakDensity - firstDensity + 1.0f) / 5.0f);
                const float returnFit = juce::jlimit (0.0f, 1.0f,
                    1.0f - std::abs (returnDensity - peakDensity * 0.78f) / 4.0f);
                densityTrajectory = 0.58f * peakLift + 0.42f * returnFit;
            }

            // 4) Harmony: stable chord tones on important material, with enough
            // non-chord color to avoid turning every melody into an arpeggio.
            float harmonicSum = 0.0f;
            float harmonicWeight = 0.0f;
            for (int b = 0; b < barsN; ++b)
            {
                const auto& cp = chordPcs[(size_t) b];
                if (cp.empty()) continue;
                for (const auto* n : melodyBars[(size_t) b])
                {
                    const int pc = (n->note % 12 + 12) % 12;
                    const bool chordTone = std::find (cp.begin(), cp.end(), pc) != cp.end();
                    const float strongBeat = ((n->step % 4) == 0) ? 1.20f : 0.82f;
                    const float lengthWeight = 0.80f + 0.20f
                        * juce::jlimit (0.0f, 1.0f, (float) juce::jmax (1, n->length) / 8.0f);
                    harmonicSum += (chordTone ? 1.0f : 0.58f) * strongBeat * lengthWeight;
                    harmonicWeight += strongBeat * lengthWeight;
                }
            }
            const float harmonyScore = harmonicWeight > 0.0f
                ? juce::jlimit (0.0f, 1.0f, harmonicSum / harmonicWeight)
                : 0.55f;

            // 5) Register balance: the lead should sit above the harmonic bed,
            // but not so far away that the loop stops feeling like one object.
            float melodyMean = 0.0f;
            int melodyCount = 0;
            float chordMean = 0.0f;
            int chordCount = 0;
            float bassMean = 0.0f;
            int bassCount = 0;
            for (int b = 0; b < barsN; ++b)
            {
                for (const auto* n : melodyBars[(size_t) b]) { melodyMean += n->note; ++melodyCount; }
                for (const auto& n : flat.notes)
                    if (n.channel == 1 && n.step / 16 == b) { chordMean += n.note; ++chordCount; }
                for (const auto* n : bassBars[(size_t) b]) { bassMean += n->note; ++bassCount; }
            }
            const float leadMean = melodyCount > 0 ? melodyMean / (float) melodyCount : 72.0f;
            const float bedMean = chordCount > 0 ? chordMean / (float) chordCount : leadMean - 12.0f;
            const float bassCenter = bassCount > 0 ? bassMean / (float) bassCount : bedMean - 18.0f;
            const float leadChordGap = std::abs (leadMean - bedMean);
            const float leadBassGap = std::abs (leadMean - bassCenter);
            const float chordGapFit = 1.0f - juce::jlimit (0.0f, 1.0f,
                std::abs (leadChordGap - 11.0f) / 18.0f);
            const float bassGapFit = 1.0f - juce::jlimit (0.0f, 1.0f,
                std::abs (leadBassGap - 22.0f) / 24.0f);
            const float registerBalance = juce::jlimit (0.0f, 1.0f,
                0.62f * chordGapFit + 0.38f * bassGapFit);

            // 6) Melody/bass relationship: a good pair shares a few anchors but
            // does not mirror every movement. Score both rhythmic support and
            // directional independence between bar-level movements.
            float sharedAccentRatio = 0.0f;
            int sharedCount = 0;
            int totalMelody = 0;
            for (int b = 0; b < barsN; ++b)
            {
                for (const auto* mNote : melodyBars[(size_t) b])
                {
                    ++totalMelody;
                    bool nearBass = false;
                    for (const auto* bNote : bassBars[(size_t) b])
                    {
                        if (std::abs (mNote->step - bNote->step) <= 1) { nearBass = true; break; }
                    }
                    if (nearBass) ++sharedCount;
                }
            }
            sharedAccentRatio = totalMelody > 0 ? (float) sharedCount / (float) totalMelody : 0.0f;
            const float sharedAccentFit = 1.0f
                - juce::jlimit (0.0f, 1.0f, std::abs (sharedAccentRatio - 0.52f) / 0.48f);

            int motionComparisons = 0;
            int independentMotion = 0;
            for (int b = 1; b < barsN; ++b)
            {
                if (melodyBars[(size_t) b].empty() || melodyBars[(size_t) (b - 1)].empty()
                    || bassBars[(size_t) b].empty() || bassBars[(size_t) (b - 1)].empty())
                    continue;

                const int md = melodyBars[(size_t) b].front()->note
                            - melodyBars[(size_t) (b - 1)].front()->note;
                const int bd = bassBars[(size_t) b].front()->note
                            - bassBars[(size_t) (b - 1)].front()->note;
                if (md == 0 || bd == 0 || (md > 0) != (bd > 0))
                    ++independentMotion;
                ++motionComparisons;
            }
            const float motionFit = motionComparisons > 0
                ? juce::jlimit (0.0f, 1.0f,
                    1.0f - std::abs ((float) independentMotion / (float) motionComparisons - 0.62f) / 0.62f)
                : 0.55f;
            const float layerRelationship = juce::jlimit (0.0f, 1.0f,
                0.58f * sharedAccentFit + 0.42f * motionFit);

            // 7) Seam: the last phrase event should be able to hand the loop back
            // to the first event without a giant register discontinuity.
            float seamScore = juce::jlimit (0.0f, 1.0f, f.seam);
            if (melodyCount >= 2)
            {
                const NoteEvent* first = nullptr;
                const NoteEvent* last = nullptr;
                for (const auto* n : melodyBars.front())
                    if (first == nullptr) { first = n; break; }
                for (auto it = melodyBars.rbegin(); it != melodyBars.rend() && last == nullptr; ++it)
                    if (! it->empty()) last = it->back();

                if (first != nullptr && last != nullptr)
                {
                    const int loopInterval = std::abs (first->note - last->note);
                    const float pitchClosure = 1.0f
                        - juce::jlimit (0.0f, 1.0f, std::abs ((float) loopInterval - 3.0f) / 9.0f);
                    seamScore = juce::jlimit (0.0f, 1.0f, 0.65f * f.seam + 0.35f * pitchClosure);
                }
            }

            // 8) Holistic "musical statement" score.
            const float musicalStatement =
                0.12f * identityScore
                + 0.10f * tensionRelease
                + 0.10f * identityNoveltyBalance
                + 0.10f * contourScore
                + 0.10f * f.grooveQuality
                + 0.14f * harmonyScore
                + 0.08f * registerBalance
                + 0.10f * layerRelationship
                + 0.07f * seamScore
                + 0.09f * densityTrajectory;

            // 9) Anti-boredom gate: technically correct loops with strong identity
            // but weak novelty, contour and surprise get a controlled penalty.
            const float staleIdentity = juce::jlimit (0.0f, 1.0f,
                (identityScore - 0.70f) / 0.30f);
            const float lowNovelty = juce::jlimit (0.0f, 1.0f,
                (0.34f - novelty) / 0.34f);
            const float weakArc = juce::jlimit (0.0f, 1.0f,
                (0.42f - juce::jmax (f.phraseArc, f.tensionArc)) / 0.42f);
            const float boringPenalty = staleIdentity * lowNovelty * (0.55f + 0.45f * weakArc);

            quality += 0.20f * juce::jlimit (0.0f, 1.0f, musicalStatement);
            quality -= 0.065f * boringPenalty;
        }


        // 0.40 Taste ML: the features of the whole loop are collected here; the
        // model scores every candidate after the pool statistics are known (below).
        tasteFeatures.push_back(taste::extractFeatures(flat.notes, flat.bars));

        // 0.75 Composer Judge 2.0: one top-level coherence score over the
        // already-generated candidate. It evaluates the composition as a whole
        // and never rewrites the MIDI.
        const float motifSemantics = motifSemanticsScore (flat, identity);
        const float loopClosure = loopClosureScore (flat, identity);
        const ComposerJudgeInputs composerJudgeInputs
        {
            f,
            motifMemory,
            grooveQuality,
            rhythmGrammarQuality,
            melodyExpressionQuality,
            harmonicIntelligenceQuality,
            composerGrammarQuality,
            melodicProsodyQuality,
            motifSemantics,
            loopClosure,
            development
        };
        const float composerJudge = composerJudgeScore (flat, identity, composerJudgeInputs);
        quality += 0.15f * composerJudge;
        const float pleasantness = melodyPleasantnessScore (flat);
        quality += 0.24f * pleasantness;
        if (pleasantness < 0.48f)
            quality -= 0.12f * (0.48f - pleasantness);

        // Archetype-specific focus: the generic judge remains dominant, while
        // this pass makes sure each creative route gets a meaningful chance.
        float archetypeFit = 0.5f;
        switch (archetype)
        {
            case 0: // HOOK
                archetypeFit = 0.36f * f.hook + 0.26f * f.motifIdentity + 0.20f * f.phraseMemory + 0.18f * f.repetition;
                break;
            case 1: // GROOVE
                archetypeFit = 0.48f * f.rhythmIdentity + 0.22f * f.velocity + 0.18f * f.density + 0.12f * f.hook;
                break;
            case 2: // HARMONY
                archetypeFit = 0.38f * f.phraseMemory + 0.28f * f.hook + 0.20f * f.seam + 0.14f * f.registerScore;
                break;
            case 3: // MOTIF
                archetypeFit = 0.38f * motifMemory + 0.24f * f.motifIdentity + 0.20f * f.phraseMemory + 0.18f * f.repetition;
                break;
            case 4: // MINIMAL
                archetypeFit = 0.55f * f.space + 0.25f * (1.0f - f.density) + 0.20f * f.repetition;
                break;
            case 5: // WEIRD
                archetypeFit = 0.40f * f.surprise + 0.26f * f.leap + 0.22f * f.variety + 0.12f * f.contour;
                break;
            case 6: // EMOTIONAL
                archetypeFit = 0.46f * f.phraseArc + 0.24f * f.seam + 0.18f * f.registerScore + 0.12f * f.surprise;
                break;
            default: // WILDCARD
                archetypeFit = 0.26f * f.hook + 0.20f * f.rhythmIdentity + 0.18f * f.motifIdentity
                             + 0.18f * f.surprise + 0.18f * f.variety;
                break;
        }
        const float archetypeTargetsDensity[8] = { .55f, .68f, .52f, .46f, .25f, .58f, .50f, .52f };
        const float archetypeTargetsSpace[8]   = { .42f, .28f, .44f, .40f, .84f, .34f, .46f, .48f };
        archetypeFit += 0.14f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.density - archetypeTargetsDensity[archetype])));
        archetypeFit += 0.12f * (1.0f - juce::jlimit (0.0f, 1.0f, std::abs (f.space - archetypeTargetsSpace[archetype])));
        quality += 0.19f * juce::jlimit (0.0f, 1.0f, archetypeFit);

        {
            const IdeaFingerprint idea = makeIdeaFingerprint (flat);
            candidates.push_back({std::move(flat), quality, identity, archetype,
                                  f.density, f.space, f.rhythmIdentity, f.motifIdentity,
                                  f.leap, f.registerScore, f.surprise, f.context, f.loopQuality,
                                  grooveQuality, motifMemory, f.phraseArc, f.tensionArc, development, idea});
        }
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
                const float recent = tasteModel.recentPreference (z);
                candidates[c].quality += gain * (2.0f * p - 1.0f);
                // Taste ML 2.0: short-term preference memory is deliberately
                // lighter than the long-term classifier, so recent feedback can
                // steer the next generation without hijacking it.
                candidates[c].quality += 0.08f * conf * recent;
            }
        }
    }

    std::vector<Candidate> selected;
    selected.reserve(8);
    std::vector<bool> used(candidates.size(),false);
    std::array<bool,8> usedArchetypes {};

    // 0.59.1 Diversity Gate: keep one winner per archetype, but require the
    // candidate to differ in both note-level identity and behavioral fingerprint.
    // A hard floor is attempted first; if a slot would otherwise become empty,
    // the gate relaxes rather than returning fewer than eight variations.
    // 0.63 Melodic Memory 3.0: selected variations become an explicit
    // memory bank of used musical ideas. Note-level similarity alone is not
    // enough: transposed or rhythm-preserving copies should also count as reuse.
    auto ideaNoveltyToSelected = [&] (const Candidate& candidate)
    {
        if (selected.empty()) return 1.0f;
        float maxIdeaSimilarity = 0.0f;
        for (const auto& s : selected)
            maxIdeaSimilarity = juce::jmax (maxIdeaSimilarity,
                                            ideaSimilarity (candidate.idea, s.idea));
        return 1.0f - maxIdeaSimilarity;
    };

    

    for (int slot = 0; slot < 8; ++slot)
    {
        int best = -1;
        float bestScore = -1000.0f;
        const float diversityFloor = slot < 3 ? 0.23f : 0.20f;

        // Pass 1: hard-ish gate. Musical score still dominates; diversity only
        // prevents near-clones from occupying multiple variation slots.
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (used[i] || usedArchetypes[(size_t) candidates[i].archetype]) continue;
            const float diversity = minDiversityToSelected (candidates[i], selected);
            if (diversity < diversityFloor) continue;

            float maxSim = 0.0f;
            for (const auto& s : selected)
                maxSim = juce::jmax (maxSim, similarity (candidates[i].section, s.section));
            const float ideaNovelty = ideaNoveltyToSelected (candidates[i]);
            const float familyCollision = std::count_if (selected.begin(), selected.end(),
                [&] (const Candidate& s) { return s.idea.family == candidates[i].idea.family; }) > 0 ? 1.0f : 0.0f;

            const float score = candidates[i].quality
                              - 0.62f * maxSim
                              + 0.13f * diversity
                              + 0.11f * ideaNovelty
                              - 0.035f * familyCollision;
            if (score > bestScore)
            {
                bestScore = score;
                best = (int) i;
            }
        }

        // Pass 2: controlled relaxation. This is only reached when the hard gate
        // would make the bank incomplete; the chosen candidate still pays a
        // diversity penalty and therefore does not bypass the gate for free.
        if (best < 0)
        {
            float relaxedBest = -1000.0f;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (used[i] || usedArchetypes[(size_t) candidates[i].archetype]) continue;
                const float diversity = minDiversityToSelected (candidates[i], selected);
                float maxSim = 0.0f;
                for (const auto& s : selected)
                    maxSim = juce::jmax (maxSim, similarity (candidates[i].section, s.section));
                const float ideaNovelty = ideaNoveltyToSelected (candidates[i]);
                const float familyCollision = std::count_if (selected.begin(), selected.end(),
                    [&] (const Candidate& s) { return s.idea.family == candidates[i].idea.family; }) > 0 ? 1.0f : 0.0f;

                const float gatePenalty = juce::jmax (0.0f, diversityFloor - diversity) * 1.8f;
                const float score = candidates[i].quality
                                  - 0.76f * maxSim
                                  - gatePenalty
                                  + 0.08f * diversity
                                  + 0.075f * ideaNovelty
                                  - 0.025f * familyCollision;
                if (score > relaxedBest)
                {
                    relaxedBest = score;
                    best = (int) i;
                    bestScore = score;
                }
            }
        }

        if (best < 0) break;
        used[(size_t) best] = true;
        usedArchetypes[(size_t) candidates[(size_t) best].archetype] = true;
        selected.push_back (std::move (candidates[(size_t) best]));
    }

    // A candidate may be excellent on paper but still be the same musical
    // thought as a selected slot. Idea Memory remains a soft selection pressure:
    // it never alters a candidate's intrinsic musical quality.
    // 0.56 Loop Transformation: MAGIC 3 discovers multiple strong archetypal
    // candidates first, then this stage picks the strongest discovered loop and
    // turns it into a coherent family of standalone transformations. The musical
    // identity stays anchored in the same source loop; only one intentional
    // transformation domain changes per variant.
    

    static constexpr const char* transformationNames[8] =
    {
        "ORIGINAL", "TIGHT", "SPARSE", "DARK",
        "BIGGER", "WEIRD", "TIGHT+WEIRD", "SPARSE+DARK"
    };

    // 0.60 Loop Forge: final integration pass after generation, judging,
    // diversity and transformation. A transformed loop must survive one
    // last coherence check before entering the final variation bank.
    

    
    // 0.63 Melodic Memory 3.0 + 0.76 Variation Intelligence:
    // keep the selected idea bank alive, but stop assigning transformations by
    // arbitrary slot order. A dark source should be allowed to feed DARK, a
    // rhythmic source should feed TIGHT, and a high-surprise source should feed
    // WEIRD. The assignment is globally optimized and remains one-to-one.
    std::vector<Section> transformationSources;
    transformationSources.reserve (8);
    std::vector<uint32_t> transformationSeeds;
    transformationSeeds.reserve (8);
    std::vector<midiforge::VariationTraits> transformationTraits;
    transformationTraits.reserve (8);

    for (const auto& candidate : selected)
    {
        transformationSources.push_back (candidate.section);
        transformationSeeds.push_back (candidate.identity);

        midiforge::VariationTraits traits;
        traits.density = candidate.density;
        traits.space = candidate.space;
        traits.rhythm = candidate.rhythm;
        traits.motif = candidate.motif;
        traits.leap = candidate.leap;
        traits.reg = candidate.reg;
        traits.surprise = candidate.surprise;
        traits.context = candidate.context;
        traits.loop = candidate.loop;
        traits.groove = candidate.groove;
        traits.memory = candidate.memory;
        traits.phraseArc = candidate.phraseArc;
        traits.tension = candidate.tension;
        traits.development = candidate.development;
        transformationTraits.push_back (traits);
    }

    const auto intelligentSourceByMode =
        midiforge::VariationIntelligence::assign (transformationTraits);

    std::vector<Section> result;
    result.reserve (8);

    if (! transformationSources.empty())
    {
        for (int mode = 0; mode < 8; ++mode)
        {
            int sourceSlot = intelligentSourceByMode[(size_t) mode];
            if (sourceSlot < 0)
                sourceSlot = juce::jmin (mode, (int) transformationSources.size() - 1);

            const size_t sourceIndex = (size_t) sourceSlot;

            Section flat = transformLoop (
                transformationSources[sourceIndex],
                mode,
                hash32 (transformationSeeds[sourceIndex]
                        ^ (uint32_t) (mode + 1) * 0x6D2B79F5u));

            flat.sourceArchetype = selected[sourceIndex].archetype;
            flat.transformMode = mode;
            flat.name = "VARIATION " + juce::String (mode + 1)
                      + " • " + juce::String (transformationNames[mode]);

            auto applyLock = [&] (int channel, bool locked)
            {
                if (! locked) return;
                flat.notes.erase (std::remove_if (flat.notes.begin(), flat.notes.end(),
                    [channel] (const NoteEvent& n) { return n.channel == channel; }),
                    flat.notes.end());

                for (const auto& n : previousSelected.notes)
                    if (n.channel == channel && n.step < flat.bars * 16)
                        flat.notes.push_back (n);
            };

            applyLock (1, lockChordsLayer);
            applyLock (2, lockBassLayer);
            applyLock (3, lockMelodyLayer);
            applyLock (4, lockArpLayer);

            removeDuplicateNotes (flat.notes);
            cleanMelodyLine (flat.notes);
            std::sort (flat.notes.begin(), flat.notes.end(),
                       [] (const NoteEvent& a, const NoteEvent& b)
                       {
                           if (a.step != b.step) return a.step < b.step;
                           if (a.channel != b.channel) return a.channel < b.channel;
                           return a.note < b.note;
                       });

            // Final Forge gate: transformations are valuable only when they
            // preserve the musical coherence established by the Judge stack.
            // Keep the pre-finalized form when cleanup materially hurts it.
            const Section beforeForge = flat;
            const float beforeForgeScore = loopForgeScore (beforeForge);
            finalizeLoop (flat);
            const float afterForgeScore = loopForgeScore (flat);
            if (afterForgeScore + 0.055f < beforeForgeScore)
                flat = beforeForge;

            // Safety is outside the Forge rollback. A score regression may undo
            // a stylistic transform, but it must never undo tonal/register safety.
            applyMelodyFoundation (flat, transformationSeeds[sourceIndex]);
            enforceFinalMelodyContract (flat, transformationSeeds[sourceIndex]);
            removeDuplicateNotes (flat.notes);
            cleanMelodyLine (flat.notes);
            std::sort (flat.notes.begin(), flat.notes.end(),
                       [] (const NoteEvent& a, const NoteEvent& b)
                       {
                           if (a.step != b.step) return a.step < b.step;
                           if (a.channel != b.channel) return a.channel < b.channel;
                           return a.note < b.note;
                       });

            result.push_back (std::move (flat));
        }
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

#ifdef MIDIFORGE_HEADLESS
    if (traceThisGeneration)
    {
        std::printf ("[PIPELINE TRACE] Piano register regression seed=%d\\n", seed);
        int traceLo = 40, traceHi = 96, traceLeap = 9;
        melodyRegisterContract (traceLo, traceHi, traceLeap);
        std::printf ("  contract: %d..%d, max leap %d\\n", traceLo, traceHi, traceLeap);

        for (const auto& stats : pipelineTrace)
        {
            std::printf (
                "  %-22s notes=%d min=%d max=%d maxLeap=%d outside=%d invalid=%d%s\\n",
                stats.name,
                stats.melodyNotes,
                stats.melodyNotes > 0 ? stats.minPitch : -1,
                stats.melodyNotes > 0 ? stats.maxPitch : -1,
                stats.maxLeap,
                stats.outsideContract,
                stats.invalidPitches,
                stats.invalidPitches > 0 ? "  <-- invalid pitch observed" : "");
        }
        pipelineTraceDone = true;
    }
#endif

    likeCounts.fill(0);
    dislikeCounts.fill(0);
}

void MidiForgeAudioProcessor::magicRandomize()
{
    if (isGenerating()) return;   // 0.78: the worker is reading the controls
    // MAGIC 2.0: first create one coherent musical DNA, then derive the
    // existing controls from it. This keeps the search space expressive
    // without introducing a second parallel generation engine.
    auto magicHash32 = [](uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
        x *= 0x846ca68bu; x ^= x >> 16; return x;
    };

    const uint32_t timeSeed = (uint32_t) juce::Time::currentTimeMillis();
    magicDnaSeed = magicHash32(generationSeed ^ timeSeed ^ 0x51A7D00Du);
    juce::Random r((juce::int64) magicDnaSeed);
    auto pick = [&](int maxExclusive) { return r.nextInt(maxExclusive); };
    auto rf = [&](float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };

    // DNA axes. Related axes are intentionally sampled together rather than
    // treating every parameter as an independent dice roll.
    dnaMelody  = rf(.15f, .90f);
    dnaRhythm  = juce::jlimit(.0f,1.0f, dnaMelody * .35f + rf(.15f,.85f) * .65f);
    dnaHarmony = rf(.20f, .90f);
    dnaMotif   = juce::jlimit(.0f,1.0f, .35f + dnaMelody * .45f + rf(-.12f,.18f));
    dnaRegister= rf(.20f, .85f);
    dnaGroove  = juce::jlimit(.0f,1.0f, .25f + dnaRhythm * .55f + rf(-.12f,.20f));
    dnaEnergy  = juce::jlimit(.0f,1.0f, .20f + rf(.0f,.70f));
    dnaSurprise= juce::jlimit(.0f,1.0f, .12f + rf(.0f,.58f));

    // Keep layer locks meaningful: a locked layer keeps its character controls.
    if (!lockChordsLayer)
    {
        rootPc = pick(12);
        scale = pick(12);
        progression = pick(7);
        chordDensity = juce::jlimit(.35f,1.0f,.50f + dnaHarmony*.48f);
        chordExtensions = r.nextFloat() > (.48f - dnaHarmony*.22f);
        inversions = r.nextFloat() > .28f;
        voicingWidth = juce::jlimit(.20f,.85f,.25f + dnaHarmony*.55f);
    }

    if (!lockBassLayer)
    {
        bassDensity = juce::jlimit(.25f,.95f,.28f + dnaRhythm*.52f);
    }

    if (!lockMelodyLayer)
    {
        melodyDensity = juce::jlimit(.20f,.88f,.22f + dnaMelody*.62f);
        melodyLength = juce::jlimit(.12f,.82f,.18f + dnaMelody*.48f);
        pauseChance = juce::jlimit(.04f,.42f,.32f - dnaRhythm*.20f);
        leapChance = juce::jlimit(.04f,.48f,.06f + dnaRegister*.34f);
        ghostChance = juce::jlimit(.01f,.24f,.03f + dnaGroove*.12f);
        motifStrength = juce::jlimit(.45f,.98f,dnaMotif);
        variationAmount = juce::jlimit(.18f,.85f,.22f + dnaSurprise*.55f);
    }

    if (!lockArpLayer)
    {
        arpDensity = juce::jlimit(.03f,.58f,.05f + dnaRhythm*.42f);
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
    { static constexpr int octaveChoices[] = {3, 4, 4, 5}; octave = octaveChoices[pick(4)]; }   // 6 stays available manually
    era = pick(6);

    swing = juce::jlimit(.0f,.40f, dnaGroove*.34f);
    humanize = juce::jlimit(.05f,.30f,.07f + dnaGroove*.16f);
    complexity = juce::jlimit(.20f,.92f,.25f + dnaSurprise*.48f + dnaMelody*.15f);
    fillAmount = juce::jlimit(.04f,.38f,.06f + dnaEnergy*.25f);
    energy = dnaEnergy;

    // Rhythm DNA and genre still get a chance to create distinct identities.
    if (dnaRhythm > .72f && r.nextFloat() > .35f) rhythm = Syncopated;
    if (dnaRhythm < .28f && r.nextFloat() > .30f) rhythm = Straight;
    hookMode = dnaMotif > .46f;
    chordsEnabled = true;
    bassEnabled = r.nextFloat() > .06f;
    melodyEnabled = true;
    arpEnabled = dnaRhythm > .52f || r.nextFloat() > .72f;
    leadStyleSoundCloud = false;

    // Seed controls the candidate search; DNA seed remains stable for
    // REROLL, so REROLL explores the same musical universe.
    seed = static_cast<int>(magicDnaSeed);
    regenerate();
}

void MidiForgeAudioProcessor::rerollSameDNA()
{
    if (isGenerating()) return;
    // Preserve all DNA axes and controls; only change the generation identity.
    seed = static_cast<int>(hash32(magicDnaSeed ^ generationNonce ^ 0x6d2b79f5u));
    startGeneration (0);
}

void MidiForgeAudioProcessor::mutateSelected(float amount)
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    std::vector<VisibleNote> notes = getVisibleNotes();
    if (notes.empty()) { rerollSameDNA(); return; }

    const uint32_t mutationId = ++mutationCounter;
    const uint32_t base = hash32 (generationSeed
                                   ^ 0xA17E5EEDu
                                   ^ (uint32_t) (amount * 1000.0f)
                                   ^ mutationId * 0x85ebca6bu);
    const int totalSteps = juce::jmax (16, getVisibleBars() * 16);
    const int phraseSteps = juce::jmin (64, totalSteps);
    const float strength = juce::jlimit (0.0f, 1.0f, amount);

    auto locked = [&] (int channel)
    {
        return (channel == 1 && lockChordsLayer)
            || (channel == 2 && lockBassLayer)
            || (channel == 3 && lockMelodyLayer)
            || (channel == 4 && lockArpLayer);
    };

    auto snapLane = [&] (int note, int part)
    {
        int lo = 28, hi = 108;
        registerLane (part, lo, hi);
        return juce::jlimit (lo, hi, snapToScale (note));
    };

    auto mutateMelodyPhrase = [&] (int phraseStart, int mode, uint32_t seed)
    {
        std::vector<size_t> idx;
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].channel == 3
                && notes[i].step >= phraseStart
                && notes[i].step < phraseStart + phraseSteps)
                idx.push_back (i);

        if (idx.empty() || locked (3))
            return;

        std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b)
        {
            return notes[a].step < notes[b].step;
        });

        const uint32_t h = hash32 (seed ^ (uint32_t) phraseStart * 0x9e3779b9u);
        const int anchor = notes[idx.front()].note;

        // MOTIF: transpose or gently invert the whole phrase contour. The
        // relative shape survives, so the result still sounds like the same idea.
        if (mode == 0 || mode == 4)
        {
            const bool invert = (h & 4u) != 0u && idx.size() >= 4;
            const int move = ((h >> 8) & 1u) ? 2 : -2;
            for (size_t k : idx)
            {
                const int rel = notes[k].note - anchor;
                int target = invert ? anchor - rel : notes[k].note + move;
                target = snapLane (target, 2);
                const float blend = invert ? (0.24f + 0.34f * strength)
                                           : (0.55f + 0.35f * strength);
                notes[k].note = snapLane (
                    juce::roundToInt ((float) notes[k].note * (1.0f - blend)
                                      + (float) target * blend), 2);
            }
        }

        // RHYTHM: move a phrase segment together rather than randomly moving
        // individual notes. This preserves the recognizable rhythmic grammar.
        if (mode == 1 || mode == 4)
        {
            const int delta = ((h >> 5) & 1u) ? 2 : -2;
            const int pivot = phraseStart + ((h >> 12) & 31);
            for (size_t k : idx)
                if (notes[k].step >= pivot)
                    notes[k].step = juce::jlimit (phraseStart, phraseStart + phraseSteps - 1,
                                                  notes[k].step + delta);
        }

        // CADENCE: the final note is pulled toward the ending bar's root or
        // third, giving the mutation a new destination instead of a random pitch.
        if (mode == 2 || mode == 4)
        {
            size_t last = idx.back();
            int finalBar = notes[last].step / 16;
            const auto prog = progressionDegrees();
            if (!prog.empty())
            {
                const int degree = prog[(size_t) (finalBar % (int) prog.size())];
                const int root = degreeToPitch (degree, octave);
                const int third = degreeToPitch (degree + 2, octave);
                const int target = std::abs(root - notes[last].note)
                    <= std::abs(third - notes[last].note) ? root : third;
                const float blend = 0.55f + 0.40f * strength;
                notes[last].note = snapLane (
                    juce::roundToInt ((float) notes[last].note * (1.0f - blend)
                                      + (float) target * blend), 2);
                notes[last].length = juce::jlimit (1, 4, juce::jmax (notes[last].length, 2));
            }
        }

        // GROOVE: preserve note pitches but reshape the accents and sustain at
        // phrase level. Strong beats remain strong; pickups can breathe.
        if (mode == 3 || mode == 4)
        {
            for (size_t k = 0; k < idx.size(); ++k)
            {
                auto& n = notes[idx[k]];
                const int local = (n.step - phraseStart) & 15;
                int delta = (local == 0) ? 5 : (local == 8 ? 3 : ((local & 3) == 0 ? 1 : -1));
                if (((h >> (k & 15)) & 1u) != 0u) delta = -delta;
                n.velocity = juce::jlimit (40, 118,
                    n.velocity + juce::roundToInt ((float) delta * (0.55f + 0.65f * strength)));
                if (k + 1 == idx.size() || (n.step & 7) == 0)
                    n.length = juce::jlimit (1, 4, n.length + (((h + (uint32_t) k) & 1u) ? 1 : -1));
            }
        }
    };

    const int mode = (int) (base % 4u); // 0 motif, 1 rhythm, 2 cadence, 3 groove

    // One musical domain is chosen per MUTATE press, so a mutation feels like a
    // deliberate edit. EVOLVE calls the same system with a smaller amount.
    for (int phraseStart = 0; phraseStart < totalSteps; phraseStart += phraseSteps)
        mutateMelodyPhrase (phraseStart, mode, base ^ (uint32_t) phraseStart);

    // Bass and arp follow the same mutation idea, but remain role-safe.
    if (!locked (2))
    {
        for (auto& n : notes)
        {
            if (n.channel != 2) continue;
            const int phraseStart = (n.step / phraseSteps) * phraseSteps;
            const uint32_t h = hash32 (base ^ (uint32_t) phraseStart * 0x27d4eb2du);

            if (mode == 0)
            {
                const int move = ((h >> (n.step & 15)) & 1u) ? 2 : -2;
                n.note = snapLane (n.note + move, 1);
            }
            else if (mode == 1 && n.step % 16 >= 10 && n.step % 16 <= 14)
            {
                const auto prog = progressionDegrees();
                if (! prog.empty())
                {
                    const int nextDegree = prog[(size_t) ((n.step / 16 + 1) % (int) prog.size())];
                    if ((h % 100u) < (uint32_t) (25.0f + 40.0f * strength))
                        n.note = snapLane (degreeToPitch (nextDegree, 2), 1);
                }
            }
            else if (mode == 2 && (n.step % 16) == 0)
            {
                const auto prog = progressionDegrees();
                if (! prog.empty())
                    n.note = snapLane (
                        degreeToPitch (prog[(size_t) ((n.step / 16) % (int) prog.size())], 2), 1);
            }
            else if (mode == 3)
            {
                n.velocity = juce::jlimit (45, 118,
                    n.velocity + ((n.step % 16) == 0 ? 5 : -2));
                if ((h & 15u) == 0u)
                    n.length = juce::jlimit (1, 8, n.length + 1);
            }
        }
    }

    if (!locked (4))
    {
        for (auto& n : notes)
        {
            if (n.channel != 4) continue;
            const uint32_t h = hash32 (base ^ (uint32_t) (n.step * 31 + 7));
            if (mode == 0 || mode == 4)
                n.note = juce::jlimit (36, 108,
                    snapToScale (n.note + (((h & 1u) != 0u) ? 2 : -2)));
            if (mode == 1 && (h % 100u) < (uint32_t) (18.0f + 28.0f * strength))
                n.step = juce::jlimit (0, totalSteps - 1, n.step + (((h >> 8) & 1u) ? 2 : -2));
            if (mode == 3)
                n.velocity = juce::jlimit (40, 112, n.velocity + ((n.step % 8) == 0 ? 4 : -2));
        }
    }

    if (!locked (1))
    {
        // Chord notes can arrive interleaved with other layers after rhythm mutations.
        // Sort only for grouping here; the final note order is normalized below.
        std::sort (notes.begin(), notes.end(), [] (const VisibleNote& a, const VisibleNote& b)
        {
            if (a.channel != b.channel) return a.channel < b.channel;
            if (a.step != b.step) return a.step < b.step;
            return a.note < b.note;
        });

        // Chord mutations never change the progression itself: only one voice
        // per selected hit may move by an octave, preserving harmonic identity.
        for (size_t pos = 0; pos < notes.size(); )
        {
            if (notes[pos].channel != 1) { ++pos; continue; }
            const int step = notes[pos].step;
            std::vector<size_t> group;
            while (pos < notes.size() && notes[pos].channel == 1 && notes[pos].step == step)
            {
                group.push_back (pos++);
            }
            if (!group.empty() && (mode == 0 || mode == 2))
            {
                const uint32_t h = hash32 (base ^ (uint32_t) step * 0x51ed270bu);
                const size_t voice = group.size() > 2 ? 1u : 0u;
                const int dir = (h & 1u) ? 12 : -12;
                notes[group[voice]].note = juce::jlimit (24, 108, notes[group[voice]].note + dir);
            }
            if (mode == 3)
                for (size_t k : group)
                    notes[k].velocity = juce::jlimit (40, 105, notes[k].velocity + ((step % 8) == 0 ? 3 : -2));
        }
    }

    if (!locked (5))
    {
        // Drums get a groove mutation, never pitch mutations. Keep every row's
        // identity while shifting one selected phrase cell as a unit.
        std::vector<size_t> drumIdx;
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].channel == 5)
                drumIdx.push_back (i);

        if (mode == 1 && !drumIdx.empty())
        {
            const int delta = (base & 1u) ? 2 : -2;
            const int selectedRow = (int) ((base >> 6) % (uint32_t) kDrumRows);
            for (size_t k : drumIdx)
            {
                if (drumRowForNote (notes[k].note) != selectedRow) continue;
                const int cell = notes[k].step / phraseSteps;
                const int local = notes[k].step % phraseSteps;
                if (cell == (int) ((base >> 12) % (uint32_t) juce::jmax (1, (totalSteps + phraseSteps - 1) / phraseSteps)))
                    notes[k].step = juce::jlimit (0, totalSteps - 1, cell * phraseSteps + local + delta);
            }
        }

        for (size_t k : drumIdx)
        {
            const uint32_t h = hash32 (base ^ (uint32_t) notes[k].step * 13u ^ (uint32_t) k);
            if (mode == 3 || mode == 4)
            {
                const int accent = (notes[k].step % 8 == 0) ? 5 : ((notes[k].step % 4 == 0) ? 2 : -2);
                notes[k].velocity = juce::jlimit (25, 122, notes[k].velocity + accent);
            }
        }
    }

    removeDuplicateNotes (notes);
    cleanMelodyLine (notes);
    std::sort (notes.begin(), notes.end(), [] (const VisibleNote& a, const VisibleNote& b)
    {
        if (a.step != b.step) return a.step < b.step;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.note < b.note;
    });
    replaceVisibleNotes (notes);
}

int MidiForgeAudioProcessor::similarToSelected()
{
    if (isGenerating()) return 0;

    Section ref;
    {
        const juce::ScopedLock sl (variationsLock);
        if (variations.empty()) return 0;
        ref = variations[(size_t) juce::jlimit (0, (int) variations.size() - 1, selectedVariation)];
    }
    const std::vector<VisibleNote> refNotes = getVisibleNotes();   // includes the user's edits
    if (refNotes.empty()) return 0;
    ref.notes.clear();
    for (const auto& n : refNotes)
        ref.notes.push_back ({ n.step, n.length, n.note, n.velocity, n.channel, false });

    // Distance between two loops = share of notes that differ, compared note by note (step, pitch, part). This is stricter
    // and cheaper than the judge's similarity(), which happily calls two different mutations "the same loop".
    auto noteKeys = [] (const Section& sec)
    {
        std::vector<uint32_t> keys;
        keys.reserve (sec.notes.size());
        for (const auto& n : sec.notes)
            keys.push_back (((uint32_t) n.step << 16) | ((uint32_t) n.note << 8) | (uint32_t) n.channel);
        std::sort (keys.begin(), keys.end());
        return keys;
    };
    auto keyDistance = [] (const std::vector<uint32_t>& x, const std::vector<uint32_t>& y)
    {
        std::vector<uint32_t> common;
        std::set_intersection (x.begin(), x.end(), y.begin(), y.end(), std::back_inserter (common));
        const float biggest = (float) juce::jmax<size_t> (1, juce::jmax (x.size(), y.size()));
        return 1.0f - (float) common.size() / biggest;
    };

    struct Relative { Section sec; std::vector<uint32_t> keys; float distance = 0.0f; float score = 0.0f; };
    std::vector<Relative> pool;
    const auto refKeys = noteKeys (ref);
    const float refScore = loopForgeScore (ref);
    static constexpr float ladder[7] = { 0.16f, 0.26f, 0.36f, 0.46f, 0.58f, 0.70f, 0.85f };

    // Pass 0 keeps the strict gates; pass 1 only runs when too few relatives were found.
    for (int pass = 0; pass < 2 && pool.size() < 24; ++pass)
    {
        const float scoreTolerance = pass == 0 ? 0.06f : 0.14f;
        const float minChange = pass == 0 ? 0.08f : 0.03f;
        for (int t = 0; t < 72; ++t)
        {
            replaceVisibleNotes (refNotes);                        // work on a fresh copy of the reference
            mutateSelected (ladder[t % 7]);                        // one musical domain per call, new rolls every call
            Section m = ref;
            m.notes.clear();
            for (const auto& n : getVisibleNotes())
                m.notes.push_back ({ n.step, n.length, n.note, n.velocity, n.channel, false });
            auto keys = noteKeys (m);
            const float dist = keyDistance (refKeys, keys);
            if (dist < minChange) continue;                        // effectively the same loop
            const float score = loopForgeScore (m);
            if (score < refScore - scoreTolerance) continue;       // never trade musicality for novelty
            pool.push_back ({ std::move (m), std::move (keys), dist, score });
        }
    }

    // Greedy pick: good score, and clearly different from the relatives already picked (the bank must not be seven clones).
    // The spread requirement relaxes step by step only if seven relatives cannot be found otherwise.
    std::vector<Relative> picked;
    std::vector<bool> taken (pool.size(), false);
    for (const float minSpread : { 0.14f, 0.07f, 0.02f })
    {
        while (picked.size() < 7)
        {
            int best = -1;
            float bestValue = -1000.0f;
            for (size_t i = 0; i < pool.size(); ++i)
            {
                if (taken[i]) continue;
                float minDist = 1.0f;
                for (const auto& p : picked)
                    minDist = juce::jmin (minDist, keyDistance (pool[i].keys, p.keys));
                if (! picked.empty() && minDist < minSpread) continue;
                const float value = pool[i].score + 0.9f * (picked.empty() ? 0.0f : minDist);
                if (value > bestValue) { bestValue = value; best = (int) i; }
            }
            if (best < 0) break;
            taken[(size_t) best] = true;
            picked.push_back (pool[(size_t) best]);
        }
        if (picked.size() >= 7) break;
    }
    const int found = (int) picked.size();

    // Nearest first, so slot 2 is the closest relative and slot 8 the boldest.
    std::sort (picked.begin(), picked.end(), [] (const Relative& a, const Relative& b) { return a.distance < b.distance; });

    std::vector<Section> bank;
    bank.reserve (8);
    ref.name = "VARIATION 1 • SOURCE";
    bank.push_back (ref);
    for (const auto& r : picked)
    {
        Section s = r.sec;
        s.transformMode = 8;   // "SIMILAR" in the feedback log
        s.name = "VARIATION " + juce::String ((int) bank.size() + 1) + " • SIMILAR";
        bank.push_back (std::move (s));
    }
    while (bank.size() < 8)    // could not find seven relatives: repeat the source rather than shrink the bank
    {
        Section s = ref;
        s.name = "VARIATION " + juce::String ((int) bank.size() + 1) + " • SOURCE";
        bank.push_back (std::move (s));
    }

    {
        const juce::ScopedLock sl (variationsLock);
        variations = std::move (bank);
        selectedVariation = 0;
    }
    likeCounts.fill (0);
    dislikeCounts.fill (0);
    ++generationNonce;
    chooseVariation (0);
    return found;
}

void MidiForgeAudioProcessor::evolveSelected()
{
    // Evolve keeps the same phrase-aware Mutation 2.0 engine, but with a lighter touch.
    mutateSelected (0.22f);
}

void MidiForgeAudioProcessor::refreshHostBpm()
{
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                currentBpm.store (juce::jlimit (40.0, 240.0, *bpm));
        }
    }
}

void MidiForgeAudioProcessor::regenerateBlocking (int selectionAfter)
{
    realtimeSwing.store (swing);
    realtimeHumanize.store (humanize);
    realtimeHumanizeEnabled.store (humanizeEnabled);
    realtimeDrumMuteMask.store (drumMuteMask);
    refreshHostBpm();
    buildVariationBank();
    chooseVariation (selectionAfter);
}

void MidiForgeAudioProcessor::waitForGeneration()
{
    std::lock_guard<std::mutex> g (generationThreadMutex);
    if (generationThread.joinable())
        generationThread.join();
}

// 0.78: the ~1.5 s MAGIC search used to run on the UI thread and froze FL Studio. In the plugin it now runs on a
// worker thread. The editor blocks input while isGenerating() (the generator reads the live controls and temporarily
// adjusts some of them), a request that arrives during a job is coalesced into one follow-up run, and the finished
// bank is swapped in under variationsLock exactly as before. The headless QA build stays synchronous.
void MidiForgeAudioProcessor::startGeneration (int selectionAfter)
{
    if (! asyncGeneration.load())
    {
        regenerateBlocking (selectionAfter);
        return;
    }
    realtimeSwing.store (swing);
    realtimeHumanize.store (humanize);
    realtimeHumanizeEnabled.store (humanizeEnabled);
    realtimeDrumMuteMask.store (drumMuteMask);
    refreshHostBpm();   // needs the message thread (play head)
    {
        std::lock_guard<std::mutex> g (generationMutex);
        if (generating.load())
        {
            regenQueued = true;
            queuedSelection = selectionAfter;
            return;
        }
        generating.store (true);
    }
    std::lock_guard<std::mutex> tg (generationThreadMutex);
    if (generationThread.joinable())
        generationThread.join();   // previous job already finished: returns immediately
    generationThread = std::thread ([this, selectionAfter]
    {
        int sel = selectionAfter;
        for (;;)
        {
            buildVariationBank();
            chooseVariation (sel);
            generationDone.fetch_add (1);
            std::lock_guard<std::mutex> g (generationMutex);
            if (! regenQueued)
            {
                generating.store (false);
                return;
            }
            regenQueued = false;
            sel = queuedSelection;
        }
    });
}

void MidiForgeAudioProcessor::regenerate()
{
    startGeneration (0);
}

void MidiForgeAudioProcessor::regenerateVariations()
{
    int keep = 0;
    {
        const juce::ScopedLock sl (variationsLock);
        keep = selectedVariation;
    }
    startGeneration (keep);
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
int sampleOffset,int velocityBias,int stepStartOffset)
{
if(e.step<0)return;
if(e.channel==5){ const int drow=drumRowForNote(e.note); if(drow>=0 && (realtimeDrumMuteMask.load()&(1<<drow))!=0) return; }
int velocity=juce::jlimit(1,127,e.velocity+velocityBias);
const int midiCh=(e.channel==5)?10:e.channel;      // drums = GM channel 10
juce::ignoreUnused (midi);
const double stepSamples = sampleRate * 60.0 / juce::jmax (20.0, currentBpm.load()) / 4.0;
const juce::int64 onGlobal  = samplePosition + sampleOffset;
const int endStep = e.step + juce::jmax (1, e.length);
const double endSwing = (endStep & 1) != 0 ? (double) realtimeSwing.load() * stepSamples * 0.5 : 0.0;   // same rule as the note-on offset in processBlock
const juce::int64 offGlobal = juce::jmax (onGlobal + 1, (samplePosition + stepStartOffset) + (juce::int64) (juce::jmax (1, e.length) * stepSamples + endSwing));
// A still-pending note-off of the same pitch that would land AFTER this new note-on would cut the new note: pull it forward.
for (auto& p : pendingEvents)
    if (! p.on && p.channel == midiCh && p.note == e.note && p.globalSample >= onGlobal)
        p.globalSample = onGlobal;
pendingEvents.push_back ({ onGlobal,  midiCh, e.note, velocity, true  });
pendingEvents.push_back ({ offGlobal, midiCh, e.note, 0,        false });
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
            const double onTick = (double) n.step * ticksPerStep + swingTicks (n.step, (double) ticksPerStep);
            const double offTick = swungEndTick (n.step, juce::jmax (1, n.length), (double) ticksPerStep);
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
const double onTick = (double) e.step * ticksPerStep + swingTicks (e.step, (double) ticksPerStep);
double offTick = swungEndTick (e.step, juce::jmax (1, e.length), (double) ticksPerStep);
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
output.deleteFile();   // 0.45.1: FileOutputStream appends to an existing file
auto stream = output.createOutputStream();
if (stream == nullptr)
return false;
const bool exported = file.writeTo (*stream, 1);
if (exported) logFeedback (-1, "export");
return exported;
}
void MidiForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi)
{
audio.clear();
auto& out = outScratch;
out.clear();
for(const auto m:midi)out.addEvent(m.getMessage(),m.samplePosition);
if(auto* ph=getPlayHead()){
if(auto pos=ph->getPosition()){
const double bpmNow = pos->getBpm().orFallback (120.0);
currentBpm.store (bpmNow);
const double ppq=pos->getPpqPosition().orFallback(0.0);
if (! pos->getIsPlaying())
{
    // Stopped: emit nothing, and forget the last step so the downbeat sounds again when playback restarts.
    lastGlobalStep.store (-1);
}
else
{
// Sample-accurate steps: find the first 16th-note boundary at or after this block's start and, when it falls inside
// the block, place the notes at that exact sample. (Before, a step was only noticed by the first block that STARTED
// inside it, so every note was late by up to one full buffer and jittered with the buffer size.)
const double samplesPerPpq = sampleRate * 60.0 / juce::jmax (20.0, bpmNow);
const int blockLen = audio.getNumSamples();
const int globalStep = (int) std::ceil (ppq * 4.0 - 1e-6);
const double toBoundary = juce::jmax (0.0, ((double) globalStep * 0.25 - ppq) * samplesPerPpq);
if (toBoundary < (double) blockLen && globalStep != lastGlobalStep.load())
{
lastGlobalStep.store (globalStep);
const int stepStartOffset = (int) toBoundary;
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
    int offset = stepStartOffset;
    if ((local % 2) == 1)
        offset += (int) (realtimeSwing.load() * sampleRate * 60.0
                        / juce::jmax (20.0, bpmNow) / 8.0);
    const int velBias = realtimeHumanizeEnabled.load()
        ? (int) ((realtimeRng.nextFloat() * 2.0f - 1.0f) * 14.0f * realtimeHumanize.load())
        : 0;
    for (int n = 0; n < dueCount; ++n)
        emitNote (dueNotes[(size_t) n], out, offset, velBias, stepStartOffset);
}
}
}
}
}
const int numSamples = audio.getNumSamples();
const juce::int64 blockEnd = samplePosition + numSamples;
// pass 0 = note-offs, pass 1 = note-ons: at the same sample a note always ends before the next one starts
for (int pass = 0; pass < 2; ++pass)
{
    const bool wantOn = (pass == 1);
    for (size_t i = 0; i < pendingEvents.size(); )
    {
        auto& p = pendingEvents[i];
        if (p.on == wantOn && p.globalSample < blockEnd)
        {
            const int local = (int) juce::jlimit<juce::int64> (0, juce::jmax (0, numSamples - 1), p.globalSample - samplePosition);
            if (p.on) out.addEvent (juce::MidiMessage::noteOn (p.channel, p.note, (juce::uint8) p.velocity), local);
            else      out.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), local);
            p = pendingEvents.back();
            pendingEvents.pop_back();
        }
        else
        {
            ++i;
        }
    }
}
samplePosition += audio.getNumSamples();
midi.swapWith(out);
}
void MidiForgeAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
waitForGeneration();   // 0.78: never save controls the worker is temporarily adjusting
juce::MemoryOutputStream o(dest,false);

// 0.82 state envelope: every new project state is self-identifying and versioned.
o.writeInt(kStateMagic);
o.writeInt(kStateVersion);

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
// 0.46: persist newer UI/learning state while remaining backward-compatible.
o.writeBool(leadStyleSoundCloud);
o.writeBool(lockChordsLayer);o.writeBool(lockBassLayer);o.writeBool(lockMelodyLayer);o.writeBool(lockArpLayer);
o.writeBool(tasteEnabled);
// 0.62: Humanize is an opt-in performance layer; append the flag for backward compatibility.
o.writeBool(humanizeEnabled);
}
void MidiForgeAudioProcessor::setStateInformation(const void* data,int size)
{
if(!data||size<=0)return;
waitForGeneration();   // 0.78
juce::MemoryInputStream i(data,(size_t)size,false);

// V2+ states begin with an explicit magic/version envelope. Older projects are
// still accepted by rewinding and using the historical positional parser.
if (i.getNumBytesRemaining() < 4)
    return;

const int firstWord = i.readInt();
const bool versioned = firstWord == kStateMagic;

if (versioned)
{
    if (i.getNumBytesRemaining() < 4)
        return;

    const int stateVersion = i.readInt();
    if (stateVersion <= 0 || stateVersion > kStateVersion)
        return;
}
else
{
    i.setPosition (0);
}

rootPc=i.readInt();genre=i.readInt();scale=i.readInt();progression=i.readInt();
rhythm=i.readInt();bars=i.readInt();seed=i.readInt();octave=i.readInt();sectionMode=i.readInt();
chordDensity=i.readFloat();bassDensity=i.readFloat();melodyDensity=i.readFloat();arpDensity=i.readFloat();
swing=i.readFloat();humanize=i.readFloat();complexity=i.readFloat();
melodyLength=i.readFloat();pauseChance=i.readFloat();leapChance=i.readFloat();ghostChance=i.readFloat();
arpRate=i.readInt();voicingWidth=i.readFloat();chordExtensions=i.readBool();inversions=i.readBool();
motifStrength=i.readFloat();variationAmount=i.readFloat();fillAmount=i.readFloat();energy=i.readFloat();
chordsEnabled=i.readBool();bassEnabled=i.readBool();melodyEnabled=i.readBool();arpEnabled=i.readBool();hookMode=i.readBool();
int savedSelection=i.readInt();

// Reset every optional field to its historical default before reading appended
// bytes. A legacy preset can legitimately end before these fields; loading it
// into an already-used processor must not leak the previous UI state.
mood = NeutralMood;
melodyType = HookMelody;
era = 5;
soundTarget = 0;
articulation = 0;
autoNextOnDislike = true;
chordStyle = 0;
drumsEnabled = false;
drumMuteMask = 0;
drumPitchMode = 0;
leadStyleSoundCloud = false;
lockChordsLayer = lockBassLayer = lockMelodyLayer = lockArpLayer = false;
tasteEnabled = true;
humanizeEnabled = false;

if (i.getNumBytesRemaining() >= 12) { mood=i.readInt(); melodyType=i.readInt(); era=i.readInt(); }
if (i.getNumBytesRemaining() >= 4) soundTarget=juce::jlimit(0,7,i.readInt());
if (i.getNumBytesRemaining() >= 8) { articulation=juce::jlimit(0,2,i.readInt()); autoNextOnDislike=i.readInt()!=0; }
if (i.getNumBytesRemaining() >= 8) { chordStyle=juce::jlimit(0,2,i.readInt()); drumsEnabled=i.readInt()!=0; }
if (i.getNumBytesRemaining() >= 8) { drumMuteMask=i.readInt() & 0xFF; drumPitchMode=juce::jlimit(0,1,i.readInt()); }

// 0.46: older preset states simply stop before these optional bytes.
if (i.getNumBytesRemaining() >= 1) leadStyleSoundCloud = i.readBool();
if (i.getNumBytesRemaining() >= 4)
{
    lockChordsLayer = i.readBool(); lockBassLayer = i.readBool();
    lockMelodyLayer = i.readBool(); lockArpLayer = i.readBool();
}
if (i.getNumBytesRemaining() >= 1) tasteEnabled = i.readBool();
// Older states stop before this byte, so legacy presets remain Humanize OFF.
humanizeEnabled = false;
if (i.getNumBytesRemaining() >= 1) humanizeEnabled = i.readBool();

regenerateBlocking (juce::jlimit (0, 7, savedSelection));
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
            const double onTick = n.step * (double) ticksPerStep + swingTicks (n.step, (double) ticksPerStep);
            const double offTick = swungEndTick (n.step, juce::jmax (1, n.length), (double) ticksPerStep);
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
const double onTick  = n.step * (double) ticksPerStep + swingTicks (n.step, (double) ticksPerStep);
double offTick = swungEndTick (n.step, juce::jmax (1, n.length), (double) ticksPerStep);
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
file.deleteFile();   // 0.45.1: FileOutputStream appends to an existing file
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
bool MidiForgeAudioProcessor::exportMidiFileToChannel (const juce::File& file, int channel) const
{
auto midiFile = buildMidiFile (channel);
file.deleteFile();   // 0.45.1
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFile() const
{
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileTo (file);
logFeedback (-1, "drag_all");
return file;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForChannel (int channel) const
{
static const char* names[6] = { "All", "Chords", "Bass", "Melody", "Arp", "Drums" };
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (names[juce::jlimit (0, 5, channel)])
+ "_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileToChannel (file, channel);
logFeedback (-1, "drag_part");
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
