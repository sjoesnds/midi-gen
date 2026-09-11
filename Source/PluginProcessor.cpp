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
void MidiForgeAudioProcessor::setSectionMode(int v){sectionMode=juce::jlimit(0,2,v);regenerate();}
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
likedN=(int)o->getProperty("likedN",0); disN=(int)o->getProperty("disN",0);
likedD=(float)o->getProperty("likedD",0.0); likedE=(float)o->getProperty("likedE",0.0); likedC=(float)o->getProperty("likedC",0.0);
disD=(float)o->getProperty("disD",0.0); disE=(float)o->getProperty("disE",0.0); disC=(float)o->getProperty("disC",0.0);
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
// Hook-oriented generator + flagship SoundCloud techniques:
// repetition + rhythmic identity + chord-tone gravity + controlled variation,
// call-response, chromatic approaches, passing tones, 9/11 colors, octave shimmer,
// per-bar velocity curves.
std::vector<int> preferred;
switch (genre)
{
case Trap:      preferred = {0,1,2,3,4,6,8,10,11,12,14,15}; break;
case House:     preferred = {0,2,4,6,8,10,12,14}; break;
case Techno:    preferred = {0,2,4,6,8,10,12,14,15}; break;
case BoomBap:   preferred = {0,3,4,7,10,12,14,15}; break;
case Ambient:   preferred = {0,4,8,12}; break;
case Cinematic: preferred = {0,2,4,7,8,11,12,14}; break;
default:        preferred = {0,2,4,6,8,10,12,14}; break;
}
std::vector<int> candidates;
for (int x : preferred)
if (rhythmHit(x))
candidates.push_back(x);
for (int x : {0,4,8,12})
if (std::find(candidates.begin(), candidates.end(), x) == candidates.end())
candidates.push_back(x);
std::sort(candidates.begin(), candidates.end());
const bool hook = hookMode;
int targetCount = hook
? 9 + (int)std::round(4.0f * melodyDensity) + (int)std::round(2.0f * e)
: 6 + (int)std::round(5.0f * melodyDensity) + (int)std::round(2.0f * e);
if (genre == Trap || genre == Techno) targetCount += 1;
if (genre == Ambient && !hook) targetCount -= 2;
targetCount = juce::jlimit(5, (int)candidates.size(), targetCount);
// Previous bar melody supplies the motif (moved up: needed for call-response).
std::vector<NoteEvent> prevBar;
const int prevStart = (barOffset - 1) * 16;
if (barOffset > 0)
{
for (const auto& ev : s.notes)
if (ev.channel == 3 && ev.step >= prevStart && ev.step < prevStart + 16)
prevBar.push_back(ev);
}
// FLAGSHIP: call-response — нечётный такт отвечает на терцию/квинту ниже.
int responseShift = 0;
if (hook && (barOffset % 2) == 1 && !prevBar.empty() && r.nextFloat() < motifStrength * 0.5f)
responseShift = r.nextBool() ? -3 : -5;
std::vector<int> chosen;
std::array<bool,16> used{};
used.fill(false);
auto addStep = [&](int x, bool forced)
{
if (x < 0 || x >= 16 || used[(size_t)x])
return;
if (!forced && !hook && (x % 4) != 0 &&
r.nextFloat() < pauseChance)
return;
used[(size_t)x] = true;
chosen.push_back(x);
};
for (int x : {0,4,8,12})
{
if ((int)chosen.size() >= targetCount) break;
addStep(x, true);
}
if (hook)
{
static const int skeletons[4][6] = {
{0,2,4,7,8,11},
{0,3,4,6,8,12},
{0,2,4,6,10,12},
{0,3,4,7,8,11}
};
const auto& row = skeletons[(barOffset + variationSalt) % 4];
for (int x : row)
{
if ((int)chosen.size() >= targetCount) break;
if (rhythmHit(x)) addStep(x, true);
}
}
std::vector<int> shuffled = candidates;
for (int i = static_cast<int>(shuffled.size()) - 1; i > 0; --i)
{
const int j = r.nextInt(i + 1);
std::swap(shuffled[static_cast<size_t>(i)],
shuffled[static_cast<size_t>(j)]);
}
for (int x : shuffled)
{
if ((int)chosen.size() >= targetCount) break;
addStep(x, false);
}
for (int x : candidates)
{
if ((int)chosen.size() >= targetCount) break;
if (!used[(size_t)x])
{
used[(size_t)x] = true;
chosen.push_back(x);
}
}
std::sort(chosen.begin(), chosen.end());
std::vector<NoteEvent> inheritedMelody;
if (inherited != nullptr)
for (const auto& ev : *inherited)
if (ev.channel == 3)
inheritedMelody.push_back(ev);
const auto prog = progressionDegrees();
const int degree = prog[(size_t)(barOffset % (int)prog.size())];
const std::array<int,4> chordTones = {
degreeToPitch(degree,     octave),
degreeToPitch(degree + 2, octave),
degreeToPitch(degree + 4, octave),
degreeToPitch(degree + 6, octave)
};
// FLAGSHIP: per-bar velocity curve (crescendo / diminuendo) for human phrasing.
const float curveDir = (((variationSalt + barOffset) % 2) == 0) ? 1.f : -1.f;
const float curveAmt = 0.25f * e;
int previous = juce::jlimit(52,92,degreeToPitch(3,octave));
if (!prevBar.empty() && r.nextFloat() < (hook ? 0.72f : motifStrength))
previous = prevBar.front().note + responseShift;
else if (!inheritedMelody.empty() && r.nextFloat() < (hook ? 0.68f : motifStrength))
previous = inheritedMelody.front().note;
for (int index = 0; index < (int)chosen.size(); ++index)
{
const int x = chosen[(size_t)index];
const bool accent = (x % 4) == 0;
const bool phraseEnd = x >= 14 || index == (int)chosen.size() - 1;
int target = previous;
if (!prevBar.empty() &&
r.nextFloat() < (hook ? 0.52f : 0.45f) * motifStrength)
{
const auto it = std::min_element(
prevBar.begin(), prevBar.end(),
[x](const NoteEvent& a, const NoteEvent& b)
{
return std::abs((a.step % 16) - x) <
std::abs((b.step % 16) - x);
});
target = (it != prevBar.end()) ? it->note + responseShift : previous;
if (r.nextFloat() < variationAmount)
target += r.nextInt(juce::Range<int>(-3,4));
}
else if (hook && !inheritedMelody.empty() &&
r.nextFloat() < 0.40f * motifStrength)
{
target = inheritedMelody[(size_t)(x % (int)inheritedMelody.size())].note;
if (r.nextFloat() < variationAmount)
target += r.nextInt(juce::Range<int>(-2,3));
}
else
{
const bool leap =
r.nextFloat() < (0.05f + 0.22f * leapChance + 0.10f * complexity);
const int contourBias = ((variationSalt % 2) == 0) ? 1 : -1;
target += leap
? contourBias * r.nextInt(juce::Range<int>(4,9))
: r.nextInt(juce::Range<int>(-3,4));
}
float chordBias = hook
? (accent ? 0.82f : 0.48f)
: (accent ? 0.72f : 0.32f);
if (phraseEnd)
chordBias = 0.95f;
if (r.nextFloat() < chordBias)
{
int nearest = chordTones[0];
int bestDist = std::abs(target - nearest);
for (int chordTone : chordTones)
{
for (int o = -1; o <= 1; ++o)
{
const int candidate = chordTone + 12 * o;
const int d = std::abs(target - candidate);
if (d < bestDist)
{
bestDist = d;
nearest = candidate;
}
}
}
// FLAGSHIP: 9th/11th color targets when extensions on.
if (chordExtensions && r.nextFloat() < 0.15f * complexity)
{
const int ext9  = degreeToPitch(degree + 8,  octave);
const int ext11 = degreeToPitch(degree + 10, octave);
const int d9  = std::abs(target - ext9);
const int d11 = std::abs(target - ext11);
if (d9  < bestDist) { bestDist = d9;  nearest = ext9;  }
if (d11 < bestDist) { bestDist = d11; nearest = ext11; }
}
target = nearest;
}
if (phraseEnd)
target = r.nextBool() ? chordTones[0] : chordTones[2];
target = juce::jlimit(48,98,target);
int note = snapToScale(target);
while (note - previous > 9)  note -= 12;
while (previous - note > 9)  note += 12;
note = juce::jlimit(48,98,note);
int len = 1;
if (phraseEnd)
len = r.nextFloat() < (0.55f + 0.25f * melodyLength) ? 2 : 1;
else if (accent && r.nextFloat() < (0.34f + 0.30f * melodyLength))
len = 2;
else if (r.nextFloat() < (hook ? 0.08f : 0.12f) * melodyLength)
len = 4;
len = juce::jmin(len, 16 - x);
const bool ghost = !accent && !hook && r.nextFloat() < ghostChance;
int velocity = 78 + (accent ? 8 : 0) - (ghost ? 20 : 0);
if (phraseEnd) velocity += 5;
// FLAGSHIP: velocity curve inside the bar.
velocity = (int)(velocity * (1.f + curveAmt * curveDir * ((float)x / 15.f - 0.5f) * 2.f));
velocity = juce::jlimit(45,118,velocity);
// FLAGSHIP: chromatic approach note into strong targets.
if ((accent || phraseEnd) && x > 0 && r.nextFloat() < 0.22f)
s.notes.push_back({barOffset*16+x-1,1,juce::jlimit(0,127,note-1),55,3,true});
// FLAGSHIP: passing tone split on big leaps.
bool passed = false;
if (std::abs(note-previous) >= 5 && x+1 < 16 && !used[(size_t)(x+1)] && r.nextFloat() < 0.35f)
{
const int mid = snapToScale((note+previous)/2);
s.notes.push_back({barOffset*16+x,1,mid,62,3,false});
used[(size_t)(x+1)] = true;
s.notes.push_back({barOffset*16+x+1,juce::jmax(1,len-1),note,velocity,3,ghost});
passed = true;
}
if (!passed)
s.notes.push_back({barOffset*16+x,len,note,velocity,3,ghost});
// FLAGSHIP: octave shimmer double (bell/pluck colour).
if (r.nextFloat() < 0.10f * complexity && note+12 <= 98)
s.notes.push_back({barOffset*16+x,1,note+12,juce::jlimit(1,127,(int)(velocity*0.55f)),3,true});
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
section.name=names[std::min(sectionIndex,6)];
section.bars=bars;
float targetEnergy=energy;
if(sectionIndex==0)targetEnergy*=0.55f;
if(sectionIndex==2)targetEnergy=juce::jmin(1.f,targetEnergy+0.12f);
if(sectionIndex==3||sectionIndex==5)targetEnergy=juce::jmin(1.f,targetEnergy+0.28f);
if(sectionIndex==4)targetEnergy*=0.45f;
if(sectionIndex==6)targetEnergy*=0.40f;
section.energy=targetEnergy;
section.densityMultiplier=0.55f+0.65f*targetEnergy;
for(int bar=0;bar<bars;++bar){
int deg=prog[(size_t)((bar+sectionIndex)%prog.size())];
if(chordsEnabled && !(sectionIndex==0&&bar>0&&r.nextFloat()<.35f))
addChords(section,bar,deg,targetEnergy,r);
if(bassEnabled && sectionIndex!=4)
addBass(section,bar,deg,targetEnergy,r);
if(melodyEnabled && sectionIndex!=0)
addMelody(section,bar,targetEnergy,r,inherited,variationSalt);
if(arpEnabled && (sectionIndex>=2 || genre==Ambient))
addArp(section,bar,deg,targetEnergy,r);
if(fillAmount>0.01f && bar==bars-1 && r.nextFloat()<fillAmount){
for(int x=12;x<16;++x){
int n=snapToScale(72+r.nextInt (juce::Range<int> (-6, 7)));
section.notes.push_back({bar*16+x,1,n,70+x-12,3,false});
}
}
}
if(sectionIndex==3||sectionIndex==5){
for(auto& e:section.notes){
if(e.channel==3) e.velocity=juce::jlimit(1,127,e.velocity+10);
}
}
if(sectionIndex==4){
for(auto& e:section.notes) e.velocity=juce::jlimit(1,127,e.velocity-18);
}
}
void MidiForgeAudioProcessor::buildBaseSong(SongData& song,juce::Random& r, int variationSalt)
{
song.sections.clear();
const auto prog=progressionDegrees();
int sectionCount=sectionMode==Loop?1:(sectionMode == SongMode?5:7);
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
const double stepSamples = sampleRate * 60.0 / juce::jmax (20.0, currentBpm) / 4.0;
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
const int microsecondsPerQuarterNote = juce::roundToInt (60000000.0 / juce::jmax (20.0, currentBpm));
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
currentBpm = pos->getBpm().orFallback (120.0);
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
offset=(int)(swing*sampleRate*60.0/juce::jmax(20.0,currentBpm)/8.0);
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
if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
return midiFile.writeTo (*stream);
return false;
}
bool MidiForgeAudioProcessor::exportMidiFileToChannel (const juce::File& file, int channel) const
{
auto midiFile = buildMidiFile (channel);
if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
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
juce::AudioProcessorEditor* MidiForgeAudioProcessor::createEditor(){return new MidiForgeAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MidiForgeAudioProcessor();}
