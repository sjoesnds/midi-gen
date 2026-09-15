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
    // 0.18 Magic Composition Engine
    // Melody is composed as a small loop idea, not as a stream of locally
    // scored notes.  The important units are: archetype -> rhythm -> motif ->
    // pitch contour -> mutation.  This deliberately leaves space and avoids
    // the old "walk the scale" behaviour (1-2-1-2-1-2).
    juce::ignoreUnused(inherited, r);

    const bool soundCloud = leadStyleSoundCloud;
    const bool hook = hookMode;

    auto hash32 = [](uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    };

    // Generation identity is part of the musical seed.  Previously the melody
    // seed depended only on variationSalt/genre, so every GENERATE rebuilt the
    // exact same melody when the UI seed was unchanged.
    const uint32_t seed = hash32(generationSeed
                                 ^ (uint32_t) variationSalt * 0x9e3779b9u
                                 ^ (uint32_t) (barOffset + 1) * 0x85ebca6bu
                                 ^ (uint32_t) genre * 0xc2b2ae35u);
    const int loopBar = barOffset % juce::jmax(1, bars);
    const int cycle = loopBar % 4;
    const int phraseCell = (barOffset / 4) % 4;
    const int phraseIdentity = (int)(hash32(seed ^ (uint32_t)(phraseCell + 1) * 0x27d4eb2du) % 4u);
    const int archetype = (int) (hash32(generationSeed
                                        ^ (uint32_t) variationSalt * 0x27d4eb2du
                                        ^ (uint32_t) genre * 0x165667b1u) % 16u);

    // Rhythm is intentionally sparse.  These are positions, not mandatory
    // notes: later filtering creates breathing room and phrase punctuation.
    // 16 rhythm identities.  Even positions are the default 1/8 grid; a few
    // profiles deliberately use 16th-note syncopation.  The generator chooses
    // one identity per bar from the generation seed, so repeated GENERATE calls
    // do not collapse onto one groove.
    static const int rhythms[][10] =
    {
        { 0, 3, 7, 10, 14, -1, -1, -1, -1, -1 },
        { 0, 6, 8, 13, -1, -1, -1, -1, -1, -1 },
        { 0, 4, 7, 12, -1, -1, -1, -1, -1, -1 },
        { 1, 4, 8, 11, 14, -1, -1, -1, -1, -1 },
        { 0, 2, 6, 9, 12, -1, -1, -1, -1, -1 },
        { 0, 5, 9, 15, -1, -1, -1, -1, -1, -1 },
        { 2, 7, 10, 14, -1, -1, -1, -1, -1, -1 },
        { 0, 8, 11, -1, -1, -1, -1, -1, -1, -1 },
        { 0, 4, 8, 10, 14, -1, -1, -1, -1, -1 },
        { 0, 6, 10, 12, -1, -1, -1, -1, -1, -1 },
        { 0, 2, 8, 12, 14, -1, -1, -1, -1, -1 },
        { 0, 4, 6, 12, -1, -1, -1, -1, -1, -1 },
        { 0, 7, 8, 14, -1, -1, -1, -1, -1, -1 },
        { 2, 4, 10, 14, -1, -1, -1, -1, -1, -1 },
        { 0, 8, 12, 14, -1, -1, -1, -1, -1, -1 },
        { 0, 3, 6, 8, 13, 15, -1, -1, -1, -1 }
    };

    const int rhythmType = (int)(hash32(seed ^ (uint32_t)variationSalt * 0x9e3779b9u
                                         ^ (uint32_t)(barOffset + 1) * 0x85ebca6bu) % 16u);

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
    const bool allowsOffGrid = (rhythmType == 0 || rhythmType == 3 || rhythmType == 5);
    for (auto& x : positions)
    {
        const uint32_t h = hash32(seed ^ (uint32_t)(x + 17));
        if ((x & 1) != 0 && !allowsOffGrid)
            x = juce::jlimit(0, 15, x - 1);
    }

    // The second half of a 4-bar cell can breathe, but never introduce an
    // arbitrary 1-step shift into an otherwise straight groove.
    if (cycle == 1 || cycle == 3)
    {
        for (auto& x : positions)
        {
            const uint32_t h = hash32(seed ^ (uint32_t)(x + 17));
            if ((h % 100u) < 18u)
            {
                const int delta = allowsOffGrid ? ((h & 1u) ? 1 : -1) : ((h & 1u) ? 2 : -2);
                x = juce::jlimit(0, 15, x + delta);
            }
        }
    }

    // Always guarantee at least one real rest.  Unlike the previous generator,
    // density is not allowed to turn a melody into a continuous stream.
    const float density = juce::jlimit(0.28f, 0.68f,
        0.38f + 0.22f * melodyDensity + 0.12f * e);
    std::vector<int> chosen;
    for (int x : positions)
    {
        const uint32_t h = hash32(seed ^ (uint32_t)(x * 97 + 31));
        if ((float)(h % 1000u) / 1000.0f < density)
            chosen.push_back(x);
    }

    // Phrase punctuation: don't fill every bar.  Some loops enter late or leave
    // the last quarter empty, which makes the loop breathe when repeated.
    if ((archetype == 1 || archetype == 7) && cycle == 0 && !chosen.empty())
        chosen.erase(chosen.begin());
    if (cycle == 3 && (archetype == 2 || archetype == 5 || archetype == 7)
        && chosen.size() > 2)
        chosen.pop_back();

    if (chosen.empty())
        chosen.push_back(positions.front());

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

    const int motifType = (int)(hash32(seed ^ (uint32_t)(archetype * 0x51ed270bu)) % 16u);
    const int motifShift = (int)((seed >> 16) % (uint32_t)scaleCount);

    const int motifTransform = (int)(hash32(seed ^ 0x6d2b79f5u) % 4u);
    const int motifRotation = (int)(hash32(seed ^ 0x1b873593u) % 5u);

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
    std::vector<int> generated;
    generated.reserve(chosen.size());

    for (size_t i = 0; i < chosen.size(); ++i)
    {
        const int x = chosen[i];
        const int globalIndex = barOffset * 5 + (int)i;
        int d = motifDegree(globalIndex + cycle);

        // Each 4-bar cell has a role: A, A', B, A''.  B is the main contrast.
        if (cycle == 2)
        {
            if (archetype == 2 || archetype == 6 || phraseIdentity == 1) d += 2;
            else if (phraseIdentity == 2) d -= 1;
            else d += ((i & 1u) ? -2 : 2);
        }
        else if (cycle == 3)
        {
            // Return toward the motif without forcing a textbook cadence.
            d += (phraseIdentity == 3 ? (i == 0 ? 2 : 0) : (i == 0 ? 1 : -1));
        }

        int note = pitchForDegree(d, octave);
        if (((seed >> ((i * 7) & 23)) & 1u) != 0u && archetype >= 2)
            note += 12;

        // Keep a comfortable lead register and find the nearest useful octave.
        while (note < 52) note += 12;
        while (note > 91) note -= 12;
        note = juce::jlimit(48, 98, snapToScale(note));

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
                        candidate = juce::jlimit(48, 98, snapToScale(candidate));
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
            const bool wantsLeap = (archetype == 2 || archetype == 5 || archetype == 6)
                && ((globalIndex + (int)(seed & 7u)) % 5 == 2);
            if (wantsLeap && std::abs(interval) < 4)
            {
                const int dir = ((hash32(seed ^ (uint32_t)globalIndex) & 1u) ? 1 : -1);
                note = juce::jlimit(48, 98, snapToScale(last + dir * (5 + (int)(seed % 3u))));
            }

            // After a large leap, don't immediately walk back by one step.
            interval = note - last;
            if (generated.size() >= 2)
            {
                const int prior = last - generated[generated.size() - 2];
                if (std::abs(prior) >= 5 && std::abs(interval) <= 2)
                    note = juce::jlimit(48, 98, snapToScale(last + (prior > 0 ? -3 : 3)));
            }
        }

        // Composition profiles: each generation has a different balance of
        // anchor / contrast / register / repetition.  These are musical rules,
        // not random pitch noise, and they are intentionally subtle.
        const int profile = (int)(hash32(seed ^ 0x9e3779b9u) % 8u);
        if (profile == 1 && (i & 1u) == 0)
            note = juce::jlimit(48, 98, snapToScale(note + ((i % 3 == 0) ? 2 : -2)));
        else if (profile == 2 && i == chosen.size() / 2)
            note = juce::jlimit(48, 98, snapToScale(note + 5));
        else if (profile == 3 && (i % 3 == 1))
            note = juce::jlimit(48, 98, snapToScale(note - 5));
        else if (profile == 4 && (i == 0 || i + 1 == chosen.size()))
            note = juce::jlimit(48, 98, snapToScale(note + (i == 0 ? -3 : 3)));
        else if (profile == 5 && i % 4 == 2)
            note = juce::jlimit(48, 98, snapToScale(note + 7));
        else if (profile == 6 && i % 4 == 3)
            note = juce::jlimit(48, 98, snapToScale(note - 7));

        // Harmonic anchor on strong positions, but leave weak positions free.
        if ((x == 0 || x == 8) && !chordTone(note))
        {
            const int root = pitchForDegree(degree, octave);
            const int third = pitchForDegree(degree + 2, octave);
            if ((hash32(seed ^ (uint32_t)(x + 101)) % 100u) < 62u)
                note = (std::abs(root - previous) <= std::abs(third - previous)) ? root : third;
            while (note < 52) note += 12;
            while (note > 91) note -= 12;
            note = juce::jlimit(48, 98, snapToScale(note));
        }

        generated.push_back(note);
        previous = note;

        // Duration is part of the phrase identity.  Long notes create space;
        // short notes are reserved for the rhythmic hook.
        int len = 1;
        const uint32_t h = hash32(seed ^ (uint32_t)(globalIndex * 41 + 9));
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
        len = juce::jmin(len, 16 - x);

        int velocity = 70 + (x % 4 == 0 ? 8 : 0);
        if (cycle == 2) velocity += 5;
        if (hook && (x == 0 || x == 8)) velocity += 4;
        velocity += (int)(h % 7u) - 3;
        velocity = juce::jlimit(48, 112, velocity);

        s.notes.push_back({ barOffset * 16 + x, len, note, velocity, 3, false });
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
    // 0.16 MAGIC CANDIDATE ENGINE
    // We no longer accept the first eight generations as "variations".
    // Instead we explore a much larger space, score complete loops, and then
    // greedily select a diverse set of winners.  This makes MAGIC a search
    // process rather than a random-note button.
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
            float rhythmIdentity=0, motifIdentity=0, seam=0, stepPenalty=0, registerScore=0, surprise=0;
        };
        F f;
        std::vector<const NoteEvent*> m;
        for (const auto& n : sec.notes)
            if (n.channel == 3) m.push_back(&n);

        const int totalSteps = juce::jmax(1, sec.bars * 16);
        f.density = juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 6));
        f.space = 1.0f - juce::jlimit(0.0f, 1.0f, (float)m.size() / (float)juce::jmax(1, sec.bars * 5));

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
        const float densityTarget = hookMode ? 0.48f : 0.40f;
        const float densityFit = 1.0f - juce::jlimit(0.0f,1.0f,std::abs(f.density-densityTarget)/0.42f);
        f.hook = 0.65f*f.hook + 0.35f*densityFit;

        // Deterministic micro-jitter keeps ties from always favouring the same
        // candidate while remaining reproducible for a generation.
        const float jitter=(float)((hash32(identity)^0x55aa33u)%1000u)/100000.0f;
        f.hook=juce::jlimit(0.0f,1.0f,f.hook+jitter);
        juce::ignoreUnused(totalSteps);
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
                if(n.channel==3)
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
                        n.note = juce::jlimit(48, 98, snapToScale(n.note + (((h>>9)&1u) ? 12 : -12)));
                    if(mode >= 23u && mode < 27u && n.length > 2)
                        n.length = juce::jmax(2, n.length - (int)(h % 4u));
                    if(mode >= 27u && mode < 31u)
                        n.velocity = juce::jlimit(35,118,n.velocity - 10);
                }
                flat.notes.push_back(n);
            }
        }
        juce::ignoreUnused(local);
        return flat;
    };

    std::vector<Candidate> candidates;
    constexpr int candidateCount=96;
    candidates.reserve(candidateCount);

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
        quality += 0.22f*f.hook;
        quality += 0.10f*f.space;
        quality += 0.10f*f.repetition;
        quality += 0.10f*f.variety;
        quality += 0.08f*f.contour;
        quality += 0.08f*f.leap;
        quality += 0.08f*f.rhythmIdentity;
        quality += 0.08f*f.motifIdentity;
        quality += 0.07f*f.seam;
        quality += 0.05f*f.registerScore;
        quality += 0.05f*f.surprise;
        quality += 0.12f*(1.0f-juce::jlimit(0.0f,1.0f,std::abs(f.density-0.46f)/0.50f));
        quality -= 0.28f*f.stepPenalty;

        // Taste profile nudges the search without collapsing it into one style.
        if(likedN>0) quality += 0.10f*(1.0f-std::abs(f.density-likedD));
        if(disN>0) quality -= 0.08f*(1.0f-std::abs(f.density-disD));

        candidates.push_back({std::move(flat),quality,identity});
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
