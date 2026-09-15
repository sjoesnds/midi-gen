#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
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
void MidiForgeAudioProcessor::setGenre(int v){genre=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setScale(int v){scale=juce::jlimit(0,6,v);regenerate();}
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
std::vector<int> MidiForgeAudioProcessor::scaleSemitones() const
{
switch(scale){
case Major:return{0,2,4,5,7,9,11};
case Minor:return{0,2,3,5,7,8,10};
case Dorian:return{0,2,3,5,7,9,10};
case Phrygian:return{0,1,3,5,7,8,10};
case HarmonicMinor:return{0,2,3,5,7,8,11};
case MelodicMinor:return{0,2,3,5,7,9,11};
default:return{0,2,4,7,9};
}
}
std::vector<int> MidiForgeAudioProcessor::progressionDegrees() const
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
default:return{0,5,3,4};
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
int best=midi,bestDist=999;
for(int o=1;o<=9;++o)for(int p:scaleSemitones()){
int n=12*o+rootPc+p,d=std::abs(n-midi);
if(d<bestDist){bestDist=d;best=n;}
}
return juce::jlimit(0,127,best);
}
bool MidiForgeAudioProcessor::rhythmHit(int x) const
{
x=((x%16)+16)%16;
if(rhythm==Straight)return true;
if(rhythm==Syncopated)return(x%4==0)||(x%4==3)||(x==6)||(x==14);
if(rhythm==Broken)return(x%8==0)||x==3||x==6||x==10||x==13;
return((x*7)%16)<7;
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
void MidiForgeAudioProcessor::likeVariation(int vi)
{
if(vi<0||vi>7)return;
likeCounts[(size_t)vi]++;
float d,e,c; sampleVariationFeatures(vi,d,e,c);
likedD=(likedD*likedN+d)/(likedN+1);
likedE=(likedE*likedN+e)/(likedN+1);
likedC=(likedC*likedN+c)/(likedN+1);
likedN++;
savePreferences();
applyLearnedWeights();
}
void MidiForgeAudioProcessor::dislikeVariation(int vi)
{
if(vi<0||vi>7)return;
dislikeCounts[(size_t)vi]++;
float d,e,c; sampleVariationFeatures(vi,d,e,c);
disD=(disD*disN+d)/(disN+1);
disE=(disE*disN+e)/(disN+1);
disC=(disC*disN+c)/(disN+1);
disN++;
savePreferences();
applyLearnedWeights();
}
int MidiForgeAudioProcessor::getLikeCount(int vi) const { return (vi>=0&&vi<8)?likeCounts[(size_t)vi]:0; }
int MidiForgeAudioProcessor::getDislikeCount(int vi) const { return (vi>=0&&vi<8)?dislikeCounts[(size_t)vi]:0; }
int MidiForgeAudioProcessor::getVariationScore(int vi) const { return getLikeCount(vi)-getDislikeCount(vi); }
void MidiForgeAudioProcessor::applyLearnedWeights()
{
// Направление = что вкусу нравится против того, что он отвергает.
float dirD=0,dirE=0,dirC=0; int w=0;
if(likedN>0&&disN>0){ dirD=likedD-disD; dirE=likedE-disE; dirC=likedC-disC; w=juce::jmin(likedN,disN); }
else if(likedN>0){ dirD=likedD-melodyDensity; dirE=likedE-energy; dirC=likedC-complexity; w=likedN; }
else if(disN>0){ dirD=melodyDensity-disD; dirE=energy-disE; dirC=complexity-disC; w=disN; }
if(w<=0)return;
const float g=0.15f*juce::jlimit(0.f,1.f,(float)w/6.f);
melodyDensity=juce::jlimit(0.f,1.f,melodyDensity+dirD*g);
energy=juce::jlimit(0.f,1.f,energy+dirE*g);
complexity=juce::jlimit(0.f,1.f,complexity+dirC*g);
}
void MidiForgeAudioProcessor::savePreferences()
{
juce::DynamicObject* o=new juce::DynamicObject();
o->setProperty("likedN",likedN); o->setProperty("likedD",(double)likedD);
o->setProperty("likedE",(double)likedE); o->setProperty("likedC",(double)likedC);
o->setProperty("disN",disN); o->setProperty("disD",(double)disD);
o->setProperty("disE",(double)disE); o->setProperty("disC",(double)disC);
juce::Array<juce::var> lk,dk;
for(int i=0;i<8;++i){ lk.add(likeCounts[(size_t)i]); dk.add(dislikeCounts[(size_t)i]); }
o->setProperty("likes",lk); o->setProperty("dislikes",dk);
preferencesFile.getParentDirectory().createDirectory();
preferencesFile.replaceWithText(juce::JSON::toString(juce::var(o)));
}
void MidiForgeAudioProcessor::loadPreferences()
{
if(!preferencesFile.existsAsFile())return;
juce::var v=juce::JSON::parse(preferencesFile.loadFileAsString());
if(auto* o=v.getDynamicObject()){
likedN=(int)o->getProperty("likedN"); disN=(int)o->getProperty("disN");
likedD=(float)o->getProperty("likedD"); likedE=(float)o->getProperty("likedE"); likedC=(float)o->getProperty("likedC");
disD=(float)o->getProperty("disD"); disE=(float)o->getProperty("disE"); disC=(float)o->getProperty("disC");
if(auto* la=o->getProperty("likes").getArray())
for(int i=0;i<8&&i<la->size();++i) likeCounts[(size_t)i]=(int)(*la)[i];
if(auto* da=o->getProperty("dislikes").getArray())
for(int i=0;i<8&&i<da->size();++i) dislikeCounts[(size_t)i]=(int)(*da)[i];
}
}
// --- Generation ---------------------------------------------------------
void MidiForgeAudioProcessor::addChords(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
std::vector<int> chordDegrees={degree,degree+2,degree+4};
if(chordExtensions && (complexity>0.45f || r.nextFloat()<0.4f)) chordDegrees.push_back(degree+6);
if(chordExtensions && complexity>0.75f && r.nextFloat()<0.45f) chordDegrees.push_back(degree+8);
int root=degreeToPitch(degree,octave-1);
int transpose=r.nextFloat()<voicingWidth*.25f?(r.nextBool()?12:-12):0;
for(int i=0;i<(int)chordDegrees.size();++i){
if(r.nextFloat()>juce::jlimit(0.f,1.f,chordDensity*(0.65f+0.35f*e)))continue;
int note=juce::jlimit(24,108,degreeToPitch(chordDegrees[(size_t)i],octave-1)+transpose);
if(inversions && barOffset>0 && r.nextFloat()<0.45f){
note=juce::jlimit(24,108,note+(r.nextBool()?12:-12));
}
s.notes.push_back({barOffset*16,16,note,72-i*4,1,false});
}
if((genre==House||genre==Techno) && r.nextFloat()<e)
s.notes.push_back({barOffset*16+8,4,juce::jlimit(24,108,root+12),63,1,false});
// FLAGSHIP: add9 sparkle сверху для современного колорита.
if(chordExtensions && r.nextFloat()<0.18f)
s.notes.push_back({barOffset*16+8,8,juce::jlimit(24,108,degreeToPitch(degree+8,octave)),60,1,false});
}
void MidiForgeAudioProcessor::addBass(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
int root=juce::jlimit(18,55,degreeToPitch(degree,octave-2));
std::vector<int> steps;
if(genre==Trap)steps={0,3,6,10,14};
else if(genre==House||genre==Techno)steps={0,4,8,12};
else steps={0,8,12};
for(int x:steps){
if(!rhythmHit(x)||r.nextFloat()>bassDensity*e)continue;
int note=root;
if(complexity>0.5f&&r.nextFloat()<0.22f)note+=r.nextBool()?7:-5;
note=juce::jlimit(18,60,note);
bool ghost=r.nextFloat()<ghostChance*.5f&&x!=0;
int len=(genre==House||genre==Techno)?3:(x==0?7:3);
int vel=juce::jlimit(1,127,92+(x==0?7:0)-(ghost?20:0));
s.notes.push_back({barOffset*16+x,len,note,vel,2,ghost});
}
// FLAGSHIP: sub drop октавой вниз на сильную долю для веса (trap/cinematic).
if((genre==Trap||genre==Cinematic) && e>0.5f && r.nextFloat()<0.35f)
s.notes.push_back({barOffset*16,8,juce::jlimit(18,60,root-12),100,2,false});
}
void MidiForgeAudioProcessor::addMelody(
Section& s, int barOffset, float e, juce::Random& r,
const std::vector<NoteEvent>* inherited, int variationSalt)
{
    // 0.10 Phrase Composer:
    // compose a four-bar musical sentence instead of making each bar independently.
    // Roles: statement -> answer -> development/peak -> cadence.
    const bool hook = hookMode;
    const bool soundCloud = leadStyleSoundCloud;
    const int phrasePos = barOffset % 4;
    const bool phraseStart = phrasePos == 0;
    const bool phraseEnd = phrasePos == 3;
    const bool development = phrasePos == 2;

    // 0.11 Russian Vocal DNA:
    // In contemporary Russian vocal/rap toplines a very common device is not
    // "many notes", but a speech-like anchor followed by a short descending
    // minor-scale gesture (often 5-4-3).  Treat it as a phrase-level accent,
    // never as a rule for the whole song.
    const bool minorLike = (scale == Minor || scale == Dorian
                            || scale == Phrygian || scale == HarmonicMinor
                            || scale == MelodicMinor);
    // Pick the vocal DNA once per 4-bar phrase, not once per bar.
    // This is important: the characteristic "speech anchor -> short descent"
    // should feel like one intentional topline sentence.
    const int phraseIndex = juce::jmax(0, barOffset / 4);
    const uint32_t phraseHash = (uint32_t)(variationSalt * 2654435761u)
        ^ (uint32_t)((phraseIndex + 1) * 2246822519u)
        ^ (uint32_t)((genre + 3) * 3266489917u);
    const float phraseRoll = (float)(phraseHash % 1000u) / 1000.0f;
    const bool russianVocalGenre = (genre == Universal || genre == Trap || genre == BoomBap);
    const bool vocalDNA = minorLike && !soundCloud && russianVocalGenre
        && phraseRoll < (hook ? 0.82f : 0.72f);

    // Rotate through related gestures so the generator learns the vocabulary
    // without stamping the exact same 5-4-3 phrase everywhere.
    const int vocalGesture = (int)((phraseHash / 1000u + (uint32_t)genre) % 5u);
    // 0 = 5-4-3, 1 = 5-5-4-3, 2 = 4-5-4-3,
    // 3 = 5-4-2-3, 4 = 3-5-4-3.

    static const int rhythmPatterns[][8] =
    {
        {0,2,4,7,8,10,12,14},
        {0,3,4,6,8,11,12,15},
        {0,2,5,6,8,10,12,14},
        {0,3,4,7,8,12,13,15},
        {0,2,4,6,9,10,12,15},
        {0,4,6,8,11,12,14,15}
    };

    // --- Rhythm: establish a motif, then reuse it with controlled mutation. ---
    std::vector<int> chosen;
    chosen.reserve(12);

    std::vector<int> prevSteps;
    for (const auto& ev : s.notes)
        if (ev.channel == 3 && ev.step >= (barOffset - 1) * 16 && ev.step < barOffset * 16)
            prevSteps.push_back(ev.step % 16);

    // First bar of the phrase establishes the rhythmic DNA. Later bars answer it.
    int phraseStartBar = barOffset - phrasePos;
    std::vector<NoteEvent> motifBar;
    if (phraseStartBar >= 0)
    {
        const int start = phraseStartBar * 16;
        for (const auto& ev : s.notes)
            if (ev.channel == 3 && ev.step >= start && ev.step < start + 16)
                motifBar.push_back(ev);
    }

    if (phraseStart || motifBar.empty())
    {
        const int patternIndex = juce::jlimit(0, 5, (variationSalt + genre * 3 + barOffset / 4) % 6);
        for (int x : rhythmPatterns[patternIndex])
            if (rhythmHit(x)) chosen.push_back(x);
    }
    else
    {
        for (const auto& ev : motifBar)
        {
            const int x = ev.step % 16;
            bool keep = true;
            if (phrasePos == 1 && r.nextFloat() < (soundCloud ? 0.10f : 0.14f)) keep = false;
            if (phrasePos == 2 && r.nextFloat() < (soundCloud ? 0.18f : 0.24f)) keep = false;
            if (phrasePos == 3 && r.nextFloat() < 0.10f) keep = false;
            if (keep) chosen.push_back(x);
        }

        // Answer/development bars get one or two intentional rhythmic changes.
        if (phrasePos == 1 && !soundCloud && r.nextFloat() < 0.55f)
            chosen.push_back(6 + ((variationSalt + barOffset) & 1) * 4);
        if (phrasePos == 2 && !soundCloud && r.nextFloat() < 0.70f)
            chosen.push_back(10);
        if (phraseEnd)
            chosen.push_back(14);
    }

    auto addUnique = [&](int x)
    {
        if (x >= 0 && x < 16 && std::find(chosen.begin(), chosen.end(), x) == chosen.end())
            chosen.push_back(x);
    };

    if (!soundCloud)
    {
        addUnique(0);
        if (e > 0.40f) addUnique(8);
        if (hook || phraseEnd) addUnique(12);
    }

    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

    int targetCount = hook
        ? 7 + (int)std::round(3.0f * melodyDensity) + (int)std::round(2.0f * e)
        : 6 + (int)std::round(3.0f * melodyDensity) + (int)std::round(2.0f * e);
    if (soundCloud) targetCount = 3 + (int)std::round(2.0f * melodyDensity);
    if (genre == Ambient) targetCount -= 2;
    targetCount = juce::jlimit(soundCloud ? 3 : 5, 11, targetCount);

    while ((int)chosen.size() > targetCount)
    {
        auto it = std::find_if(chosen.rbegin(), chosen.rend(),
            [](int x) { return x != 0 && x != 8 && x != 12 && (x % 4) != 0; });
        if (it == chosen.rend()) chosen.pop_back();
        else chosen.erase(std::next(it).base());
    }

    // --- Harmony / phrase memory ------------------------------------------------
    const auto prog = progressionDegrees();
    const int degree = prog[(size_t)(barOffset % (int)prog.size())];
    const std::array<int, 4> chordTones =
    {
        degreeToPitch(degree, octave), degreeToPitch(degree + 2, octave),
        degreeToPitch(degree + 4, octave), degreeToPitch(degree + 6, octave)
    };

    std::vector<NoteEvent> prevBar;
    const int prevStart = (barOffset - 1) * 16;
    if (barOffset > 0)
        for (const auto& ev : s.notes)
            if (ev.channel == 3 && ev.step >= prevStart && ev.step < prevStart + 16)
                prevBar.push_back(ev);

    std::vector<NoteEvent> inheritedMelody;
    if (inherited != nullptr)
        for (const auto& ev : *inherited)
            if (ev.channel == 3) inheritedMelody.push_back(ev);

    int previous = juce::jlimit(50, 88, degreeToPitch(degree, octave));
    if (!prevBar.empty()) previous = prevBar.back().note;
    else if (!inheritedMelody.empty()) previous = inheritedMelody.back().note;

    // The emotional peak is placed in the development half of the sentence.
    const int peakBar = 1 + ((variationSalt + genre) & 1);
    const int desiredPeak = juce::jlimit(62, 94,
        degreeToPitch(degree + (hook ? 6 : 4), octave) + 12);

    // Speech-like anchor: most of the topline lives around one scale degree.
    // In A minor this is commonly E (5th), with occasional D (4th) or C (3rd).
    // The exact anchor is phrase-level so it remains stable across the sentence.
    const int anchorDegree = 4 - (int)(phraseHash % 3u); // 5, 4 or 3
    int vocalAnchor = degreeToPitch(anchorDegree, octave);
    vocalAnchor = juce::jlimit(55, 88, snapToScale(vocalAnchor));

    // Phrase-start motif pitches, used as an A/A' reference for bars 1-3.
    auto motifPitchForX = [&](int x) -> int
    {
        if (motifBar.empty()) return previous;
        const auto it = std::min_element(motifBar.begin(), motifBar.end(),
            [x](const NoteEvent& a, const NoteEvent& b)
            {
                return std::abs((a.step % 16) - x) < std::abs((b.step % 16) - x);
            });
        return it != motifBar.end() ? it->note : previous;
    };

    std::vector<int> generated;
    generated.reserve(chosen.size());

    auto chordTone = [&](int note) -> bool
    {
        for (int ct : chordTones)
            for (int o = -2; o <= 2; ++o)
                if (note == ct + 12 * o) return true;
        return false;
    };

    auto vocalTargetPitch = [&](int x, int index) -> int
    {
        if (!vocalDNA || phrasePos != 3)
            return -1;

        // Aim the recognizable gesture late in the fourth bar.  The final
        // landing itself is still handled by the harmonic cadence below.
        if (x < 8)
            return -1;

        const int count = (int)chosen.size();
        const int tailIndex = count - index;
        int degree = -1;

        if (tailIndex <= 1)
        {
            // Let the cadence choose the final note.
            degree = -1;
        }
        else if (tailIndex == 2)
        {
            degree = (vocalGesture == 3) ? 1 : 2; // 4th/3rd-scale area
        }
        else if (tailIndex == 3)
        {
            degree = (vocalGesture == 2 || vocalGesture == 4) ? 3 : 4; // 4 or 5
        }
        else if (tailIndex == 4)
        {
            degree = (vocalGesture == 1) ? 4 : 3; // 5 or 4
        }

        if (degree < 0)
            return -1;

        int target = degreeToPitch(degree, octave);
        // Keep the gesture in the same vocal register as the phrase.
        while (target - previous > 7) target -= 12;
        while (previous - target > 7) target += 12;
        return juce::jlimit(48, 98, snapToScale(target));
    };

    auto scoreCandidate = [&](int note, int x, int index) -> float
    {
        note = snapToScale(note);
        const bool accent = (x % 4) == 0;
        const bool strong = (x % 8) == 0;
        const bool ending = phraseEnd && (x >= 12 || index == (int)chosen.size() - 1);
        const bool isChord = chordTone(note);
        const int interval = note - previous;
        const int absInterval = std::abs(interval);
        float score = 0.0f;

        // Harmony: strong beats want stability, weak beats can carry tension.
        if (isChord) score += strong ? 5.0f : accent ? 3.2f : 1.15f;
        else score += development ? 0.85f : (phrasePos == 1 ? 0.45f : -0.20f);
        if (strong && !isChord) score -= 1.65f;

        // Russian-vocal contour: the body behaves more like speech than
        // like a continuously changing synth melody. Keep most notes close
        // to a phrase anchor, then reserve real movement for the tail.
        if (vocalDNA)
        {
            if (phrasePos <= 2)
            {
                const float anchorWeight = (phrasePos == 0 ? 3.8f
                    : phrasePos == 1 ? 3.0f : 2.0f);
                score += anchorWeight - 0.85f * std::abs(note - vocalAnchor);
                if (std::abs(note - vocalAnchor) <= 2) score += 1.4f;
            }
            // Do not let chord-tone preference completely erase the vocal
            // anchor. A small amount of harmonic tension is intentional here.

            const int target = vocalTargetPitch(x, index);
            if (target >= 0)
                score += 6.5f - 0.85f * std::abs(note - target);

            // Keep the body of the phrase conversational: repeated anchors
            // are allowed, but reward a small departure before returning.
            if (phrasePos <= 1 && index > 0 && std::abs(note - previous) <= 2)
                score += 0.55f;
        }

        // Cadence has the strongest harmonic gravity.
        if (vocalDNA && phrasePos <= 2 && !ending)
        {
            // Strongly favor the anchor without hard-quantizing every note.
            // This preserves human variation while making the "read -> move"
            // topology clearly audible.
            const float bodyAnchorRoll = (phrasePos == 0 ? 0.68f
                : phrasePos == 1 ? 0.54f : 0.38f);
            if (r.nextFloat() < bodyAnchorRoll)
            {
                int anchored = vocalAnchor;
                while (anchored - previous > 7) anchored -= 12;
                while (previous - anchored > 7) anchored += 12;
                if (std::abs(anchored - previous) <= 7)
                    note = anchored;
            }
        }

        if (ending)
        {
            const int root = snapToScale(chordTones[0]);
            const int third = snapToScale(chordTones[1]);
            if (std::abs(note - root) <= 1) score += 7.0f;
            if (std::abs(note - third) <= 1) score += 4.0f;
        }
        else if (phraseEnd && x >= 10)
        {
            // Pre-cadential note: a nearby scale tone/chord tone sets up the final landing.
            const int root = snapToScale(chordTones[0]);
            const int distance = std::abs(note - root);
            if (distance == 1 || distance == 2) score += 1.6f;
        }

        // Mostly singable motion, with occasional purposeful leaps.
        if (absInterval == 0) score -= 1.35f;
        else if (absInterval <= 2) score += 2.35f;
        else if (absInterval <= 4) score += 1.45f;
        else if (absInterval == 5 || absInterval == 7) score += development ? 0.75f : 0.15f;
        else score -= 0.35f + 0.10f * (float)(absInterval - 7);
        if (soundCloud && absInterval > 5) score -= 2.0f;

        // Leap recovery: after a large jump, reverse direction.
        if (!generated.empty())
        {
            const int prevInterval = previous - generated.back();
            if (std::abs(prevInterval) >= 5 && absInterval >= 1)
                score += ((prevInterval > 0) != (interval > 0)) ? 1.6f : -1.0f;
        }

        // A real contour: lift into the peak, then come down for the cadence.
        if (barOffset % 4 == peakBar)
            score += 0.34f * (float)(note - previous) + 1.5f - 0.045f * std::abs(note - desiredPeak);
        else if (phrasePos == 3)
            score -= 0.22f * (float)(note - previous);
        else if (phrasePos == 0)
            score += 0.08f * (float)(note - previous);

        // Motif identity: bars 1-3 remember the opening phrase without copying it blindly.
        if (!phraseStart && !motifBar.empty())
        {
            const int motifPitch = motifPitchForX(x);
            const int motifDelta = note - motifPitch;
            const bool sameRegisterShape = std::abs(motifDelta) <= 2;
            if (sameRegisterShape) score += 2.1f * motifStrength;
            else if (std::abs(motifDelta) <= 7) score += 0.65f * motifStrength;

            // Bar 1 = answer: preserve contour but allow transposition.
            if (phrasePos == 1 && !generated.empty())
            {
                const int motifPrev = motifPitchForX(chosen[(size_t)juce::jmax(0, index - 1)]);
                const int motifMove = motifPitch - motifPrev;
                if ((interval > 0) == (motifMove > 0)) score += 1.25f * motifStrength;
            }
        }

        // Development earns a little novelty and a controlled tension budget.
        if (development && !isChord && !accent) score += 0.55f * variationAmount;
        if (development && absInterval >= 5) score += 0.35f * complexity;

        // Avoid robotic repetition and one-direction scales.
        if (!generated.empty() && note == generated.back()) score -= 2.4f;
        if (generated.size() >= 2)
        {
            const int d1 = generated[generated.size() - 1] - generated[generated.size() - 2];
            const int d2 = note - generated.back();
            if (d1 != 0 && d2 != 0 && ((d1 > 0) == (d2 > 0))) score -= 0.38f;
        }

        // Register coherence.
        if (note >= 58 && note <= 88) score += 1.0f;
        score -= 0.022f * std::abs(note - 74);

        // Hook needs memorable accents; SoundCloud needs space and repetition.
        if (hook && (x == 0 || x == 8 || x == 12)) score += 0.45f;
        if (soundCloud && phrasePos != 2 && !isChord) score -= 0.35f;

        return score;
    };

    // --- Pitch composition ------------------------------------------------------
    for (int index = 0; index < (int)chosen.size(); ++index)
    {
        const int x = chosen[(size_t)index];
        const bool accent = (x % 4) == 0;
        const bool ending = phraseEnd && (x >= 12 || index == (int)chosen.size() - 1);
        const bool peakPosition = (barOffset % 4 == peakBar && x >= 6 && x <= 10);

        std::vector<int> candidates;
        candidates.reserve(48);
        for (int d = -10; d <= 10; ++d) candidates.push_back(previous + d);
        for (int ct : chordTones)
            for (int o = -1; o <= 1; ++o) candidates.push_back(ct + 12 * o);
        for (int d = 0; d <= 8; ++d) candidates.push_back(degreeToPitch(degree + d, octave));
        if (vocalDNA)
        {
            // Explicitly put the scale-degree vocabulary into the candidate set:
            // 3, 4, 5 are the core "speech anchor -> descent" area.
            for (int gd : { 2, 3, 4 })
            {
                int vp = degreeToPitch(gd, octave);
                for (int o = -1; o <= 1; ++o)
                    candidates.push_back(vp + 12 * o);
            }
            for (int o = -1; o <= 1; ++o)
                candidates.push_back(vocalAnchor + 12 * o);
        }
        for (const auto& ev : prevBar) candidates.push_back(ev.note);
        if (!phraseStart)
        {
            const int mp = motifPitchForX(x);
            for (int o = -1; o <= 1; ++o) candidates.push_back(mp + 12 * o);
        }

        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        float bestScore = -100000.0f;
        int bestNote = snapToScale(previous);
        for (int raw : candidates)
        {
            const int note = juce::jlimit(48, 98, snapToScale(raw));
            if (std::abs(note - previous) > (soundCloud ? 7 : (development ? 11 : 10))) continue;
            float score = scoreCandidate(note, x, index);

            // Peak note should be a memorable, sustained chord/scale tone.
            if (peakPosition)
            {
                if (note >= desiredPeak - 3 && note <= desiredPeak + 3) score += 2.0f;
                if (chordTone(note)) score += 1.2f;
            }
            score += r.nextFloat() * 0.18f;
            if (score > bestScore)
            {
                bestScore = score;
                bestNote = note;
            }
        }

        int note = juce::jlimit(48, 98, snapToScale(bestNote));

        // When the phrase selected the Russian-vocal DNA, preserve the
        // recognizable 5-4-3 (or close variant) tail unless this is the final
        // harmonic landing.  This is deliberately soft: harmony still wins.
        const int vocalTarget = vocalTargetPitch(x, index);
        if (vocalTarget >= 0 && !ending
            && std::abs(note - vocalTarget) <= 4)
            note = vocalTarget;

        if (vocalDNA && phrasePos <= 2 && !ending)
        {
            // Strongly favor the anchor without hard-quantizing every note.
            // This preserves human variation while making the "read -> move"
            // topology clearly audible.
            const float bodyAnchorRoll = (phrasePos == 0 ? 0.68f
                : phrasePos == 1 ? 0.54f : 0.38f);
            if (r.nextFloat() < bodyAnchorRoll)
            {
                int anchored = vocalAnchor;
                while (anchored - previous > 7) anchored -= 12;
                while (previous - anchored > 7) anchored += 12;
                if (std::abs(anchored - previous) <= 7)
                    note = anchored;
            }
        }

        if (ending)
        {
            const int root = snapToScale(chordTones[0]);
            const int third = snapToScale(chordTones[1]);
            note = (r.nextFloat() < 0.72f) ? root : third;
            while (note - previous > 9) note -= 12;
            while (previous - note > 9) note += 12;
            note = juce::jlimit(48, 98, snapToScale(note));
        }

        int len = 1;
        if (ending) len = (r.nextFloat() < 0.82f) ? 4 : 3;
        else if (peakPosition) len = (r.nextFloat() < 0.72f) ? 2 : 1;
        else if (accent && r.nextFloat() < 0.52f * melodyLength) len = 2;
        else if (r.nextFloat() < 0.08f * melodyLength) len = 3;
        if (soundCloud)
        {
            len = (ending || r.nextFloat() < 0.62f) ? 2 : 1;
            if (peakPosition && r.nextFloat() < 0.70f) len = 4;
        }
        len = juce::jmin(len, 16 - x);

        const bool ghost = !accent && !hook && !soundCloud && r.nextFloat() < ghostChance * 0.55f;
        int velocity = 72;
        if (accent) velocity += 7;
        if (peakPosition) velocity += 11;
        if (ending) velocity += 4;
        if (vocalDNA && phrasePos == 3 && x >= 8)
            velocity += (x >= 12 ? 3 : 0);
        if (ghost) velocity -= 18;
        if (phraseEnd && !peakPosition) velocity -= 4;
        velocity += r.nextInt(7) - 3;
        velocity = juce::jlimit(45, 116, velocity);

        s.notes.push_back({barOffset * 16 + x, len, note, velocity, 3, ghost});
        generated.push_back(note);
        previous = note;
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
if(chordsEnabled)
addChords(section,bar,deg,targetEnergy,r);
if(bassEnabled)
addBass(section,bar,deg,targetEnergy,r);
if(melodyEnabled)
addMelody(section,bar,targetEnergy,r,inherited,variationSalt);
if(arpEnabled)
addArp(section,bar,deg,targetEnergy,r);
if(fillAmount>0.01f && bar==bars-1 && r.nextFloat()<fillAmount){
for(int x=12;x<16;++x){
int n=snapToScale(72+r.nextInt (juce::Range<int> (-6, 7)));
section.notes.push_back({bar*16+x,1,n,70+x-12,3,false});
}
}
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
Section previousSelected;
{
const juce::ScopedLock sl (variationsLock);
if (!variations.empty())
previousSelected = variations[(size_t) selectedVariation];
}
std::vector<Section> result;
result.reserve (8);
for(int v=0;v<8;++v){
juce::Random local((juce::int64)seed + 7919LL*(v+1));
SongData s;
float oldVariation=variationAmount;
variationAmount=juce::jlimit(0.f,1.f,oldVariation + (v-3.5f)*0.08f);
buildBaseSong(s,local,v);
variationAmount=oldVariation;
Section flat;
flat.name="VARIATION "+juce::String(v+1);
flat.bars=0;
for(const auto& sec:s.sections){
flat.bars+=sec.bars;
const int sectionBarsBefore=flat.bars-sec.bars;
for(auto n:sec.notes){
n.step+=sectionBarsBefore*16;
if(v%4==1 && n.channel==3 && local.nextFloat()<0.20f)n.note+=12;
if(v%4==2 && n.channel==3 && local.nextFloat()<0.20f)n.note-=12;
if(v%4==3 && n.channel==2 && local.nextFloat()<0.20f)n.velocity-=10;
flat.notes.push_back(n);
}
}
auto applyLock = [&](int channel, bool locked)
{
if (!locked) return;
flat.notes.erase(std::remove_if(flat.notes.begin(), flat.notes.end(),
[channel](const NoteEvent& n){ return n.channel == channel; }), flat.notes.end());
for (const auto& n : previousSelected.notes)
if (n.channel == channel && n.step < flat.bars * 16)
flat.notes.push_back(n);
};
applyLock(1, lockChordsLayer);
applyLock(2, lockBassLayer);
applyLock(3, lockMelodyLayer);
applyLock(4, lockArpLayer);
result.push_back(std::move(flat));
}
const juce::ScopedLock sl (variationsLock);
variations = std::move (result);
// Контент новый — счётчики лайков по слотам сбрасываем (профиль вкуса живёт отдельно).
likeCounts.fill(0);
dislikeCounts.fill(0);
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
int velocity=juce::jlimit(1,127,e.velocity+velocityBias);
midi.addEvent(juce::MidiMessage::noteOn(e.channel,e.note,(juce::uint8)velocity),sampleOffset);
const double stepSamples = sampleRate * 60.0 / juce::jmax (20.0, currentBpm.load()) / 4.0;
const juce::int64 offGlobal = samplePosition + sampleOffset
+ (juce::int64) juce::jmax (1.0, e.length * stepSamples);
pendingOffs.push_back ({ offGlobal, e.channel, e.note });
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
conductor.addEvent(juce::MidiMessage::endOfTrack(), endTick + ppq);
file.addTrack(conductor);
for (int channel = 1; channel <= 4; ++channel)
{
juce::MidiMessageSequence track;
for (const auto& e : song.notes)
{
if (e.channel != channel)
continue;
const double onTick = (double) e.step * ticksPerStep;
const double offTick = onTick + (double) juce::jmax(1, e.length) * ticksPerStep;
const int velocity = juce::jlimit(1, 127, e.velocity);
track.addEvent(juce::MidiMessage::noteOn(channel, e.note, (juce::uint8) velocity), onTick);
track.addEvent(juce::MidiMessage::noteOff(channel, e.note), offTick);
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
const juce::ScopedLock sl(activeNotesLock);
if(!activeNotes.empty()){
const int period=juce::jmax(16,activeBars*16);
int local=globalStep%period;
if(local<0)local+=period;
uiCurrentStep.store (local);
int offset=0;
if((local%2)==1)
offset=(int)(swing*sampleRate*60.0/juce::jmax(20.0,currentBpm.load())/8.0);
int velBias=(int)((realtimeRng.nextFloat()*2.f-1.f)*14.f*humanize);
for(const auto& e:activeNotes){
if(e.step==local) emitNote(e,out,offset,velBias);
}
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
regenerate();
chooseVariation (savedSelection);
}
// --- MIDI export --------------------------------------------------------
juce::MidiFile MidiForgeAudioProcessor::buildMidiFile (int channelFilter) const
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
static const char* trackNames[5] = { "", "Chords", "Bass", "Melody", "Arp" };
for (int channel = 1; channel <= 4; ++channel)
{
if (channelFilter != 0 && channel != channelFilter) continue;
juce::MidiMessageSequence track;
track.addEvent (juce::MidiMessage::textMetaEvent (3, trackNames[channel]), 0.0);
for (const auto& n : pattern.notes)
{
if (n.channel != channel) continue;
const double onTick  = n.step * (double) ticksPerStep;
const double offTick = onTick + juce::jmax (1, n.length) * (double) ticksPerStep;
auto on = juce::MidiMessage::noteOn (channel, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity));
auto off = juce::MidiMessage::noteOff (channel, n.note);
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
static const char* names[5] = { "All", "Chords", "Bass", "Melody", "Arp" };
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (names[juce::jlimit (0, 4, channel)])
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
                          juce::jlimit (1, 4, channel) };
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
            const auto channel = juce::jlimit (1, 4, n.channel);
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

juce::AudioProcessorEditor* MidiForgeAudioProcessor::createEditor(){return new MidiForgeAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MidiForgeAudioProcessor();}
