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
    // Expressive phrase-first melody engine.
    // The generator deliberately scores candidates in context instead of
    // choosing every note independently.  The priorities are:
    // motif identity -> contour -> harmony -> tension/release -> rhythm ->
    // register -> controlled variation.
    const bool hook = hookMode;
    const bool soundCloud = leadStyleSoundCloud;

    // ---- Rhythm / phrase shape ------------------------------------------------
    // Four-bar phrases use a recurring rhythmic "DNA".  Later bars inherit
    // the previous bar with small mutations instead of becoming new random
    // patterns.
    static const int rhythmPatterns[][8] =
    {
        {0,2,4,7,8,10,12,14},
        {0,3,4,6,8,11,12,15},
        {0,2,5,6,8,10,12,14},
        {0,3,4,7,8,12,13,15},
        {0,2,4,6,9,10,12,15},
        {0,4,6,8,11,12,14,15}
    };

    std::vector<int> chosen;
    chosen.reserve(12);

    std::vector<int> previousSteps;
    for (const auto& ev : s.notes)
    {
        if (ev.channel == 3 && ev.step >= (barOffset - 1) * 16
            && ev.step < barOffset * 16)
            previousSteps.push_back(ev.step % 16);
    }

    const int phrasePos = barOffset % 4; // 0..3 inside a four-bar phrase
    if (barOffset > 0 && !previousSteps.empty())
    {
        // Keep rhythmic identity high.  Every second/third bar can mutate it.
        for (int x : previousSteps)
        {
            if (x < 0 || x >= 16) continue;
            bool keep = true;
            if (!soundCloud && phrasePos == 2 && r.nextFloat() < 0.20f)
                keep = false;
            if (soundCloud && r.nextFloat() < 0.18f)
                keep = false;
            if (keep)
                chosen.push_back(x);
        }

        // A small answer/extension is more musical than filling empty slots.
        if (!soundCloud && phrasePos == 1 && r.nextFloat() < 0.45f)
        {
            const int extra[] = { 6, 10, 14 };
            const int x = extra[(barOffset + variationSalt) % 3];
            if (std::find(chosen.begin(), chosen.end(), x) == chosen.end())
                chosen.push_back(x);
        }
        if (phrasePos == 3 && r.nextFloat() < 0.65f)
        {
            const int x = 14;
            if (std::find(chosen.begin(), chosen.end(), x) == chosen.end())
                chosen.push_back(x);
        }
    }
    else
    {
        const int patternIndex = (variationSalt + genre * 3) % 6;
        for (int x : rhythmPatterns[patternIndex])
        {
            if (rhythmHit(x))
                chosen.push_back(x);
        }
    }

    // Always anchor the phrase on useful metric positions.
    if (chosen.empty()) chosen.push_back(0);
    auto addUnique = [&](int x)
    {
        if (x >= 0 && x < 16
            && std::find(chosen.begin(), chosen.end(), x) == chosen.end())
            chosen.push_back(x);
    };

    if (!soundCloud)
    {
        addUnique(0);
        if (e > 0.38f) addUnique(8);
        if (hook || phrasePos == 3) addUnique(12);
    }

    std::sort(chosen.begin(), chosen.end());

    int targetCount = hook
        ? 7 + (int)std::round(3.0f * melodyDensity) + (int)std::round(2.0f * e)
        : 6 + (int)std::round(3.0f * melodyDensity) + (int)std::round(2.0f * e);

    if (soundCloud)
        targetCount = 3 + (int)std::round(2.0f * melodyDensity);

    if (genre == Ambient) targetCount -= 2;
    targetCount = juce::jlimit(soundCloud ? 3 : 5, 11, targetCount);

    while ((int)chosen.size() > targetCount)
    {
        // Remove weak off-beat events first, preserving phrase anchors.
        auto it = std::find_if(chosen.rbegin(), chosen.rend(),
            [](int x) { return x != 0 && x != 8 && x != 12 && (x % 4) != 0; });
        if (it == chosen.rend())
            chosen.pop_back();
        else
            chosen.erase(std::next(it).base());
    }

    // ---- Harmony ----------------------------------------------------------------
    const auto prog = progressionDegrees();
    const int degree = prog[(size_t)(barOffset % (int)prog.size())];

    const std::array<int, 4> chordTones =
    {
        degreeToPitch(degree,     octave),
        degreeToPitch(degree + 2, octave),
        degreeToPitch(degree + 4, octave),
        degreeToPitch(degree + 6, octave)
    };

    std::vector<NoteEvent> prevBar;
    const int prevStart = (barOffset - 1) * 16;
    if (barOffset > 0)
    {
        for (const auto& ev : s.notes)
            if (ev.channel == 3 && ev.step >= prevStart && ev.step < prevStart + 16)
                prevBar.push_back(ev);
    }

    std::vector<NoteEvent> inheritedMelody;
    if (inherited != nullptr)
        for (const auto& ev : *inherited)
            if (ev.channel == 3)
                inheritedMelody.push_back(ev);

    // Phrase endpoints are intentionally stable.  Middle bars are allowed
    // more tension so the final bar actually feels like a resolution.
    const bool isPhraseEnd = (phrasePos == 3);
    const bool isPhraseStart = (phrasePos == 0);

    int previous = juce::jlimit(50, 88, degreeToPitch(degree, octave));
    if (!prevBar.empty())
        previous = prevBar.back().note;
    else if (!inheritedMelody.empty())
        previous = inheritedMelody.back().note;

    // Choose a phrase peak once per four-bar phrase.  The peak is not necessarily
    // the highest note in the song; it is simply the emotional high point.
    const int peakBar = 1 + ((variationSalt + genre) & 1);
    const int desiredPeak = juce::jlimit(60, 94,
        degreeToPitch(degree + (hook ? 6 : 4), octave) + 12);

    // ---- Candidate scoring ------------------------------------------------------
    auto scoreCandidate = [&](int note, int x, int index) -> float
    {
        note = snapToScale(note);
        float score = 0.0f;

        const bool accent = (x % 4) == 0;
        const bool strong = (x % 8) == 0;
        const bool ending = isPhraseEnd && (x >= 12 || index == (int)chosen.size() - 1);

        // Scale legality is mandatory; chord tones are preferred contextually.
        bool isChord = false;
        int chordDistance = 99;
        for (int ct : chordTones)
        {
            for (int o = -2; o <= 2; ++o)
            {
                const int c = ct + 12 * o;
                const int d = std::abs(note - c);
                if (d < chordDistance) chordDistance = d;
                if (d == 0) isChord = true;
            }
        }

        if (isChord)
            score += (strong ? 4.5f : accent ? 3.2f : 1.35f);

        // Non-chord scale tones are useful in the middle of a phrase, but
        // should usually resolve on the next strong position.
        if (!isChord)
            score += (phrasePos == 1 || phrasePos == 2) ? 0.65f : -0.20f;

        if (ending)
        {
            if (isChord) score += 4.0f;
            const int root = snapToScale(chordTones[0]);
            const int third = snapToScale(chordTones[2]);
            if (std::abs(note - root) <= 1) score += 2.8f;
            if (std::abs(note - third) <= 1) score += 1.5f;
        }

        // Stepwise motion is the default.  Larger leaps need a reason.
        const int interval = note - previous;
        const int absInterval = std::abs(interval);
        if (absInterval == 0) score -= 0.9f;
        else if (absInterval == 1 || absInterval == 2) score += 2.4f;
        else if (absInterval == 3 || absInterval == 4) score += 1.5f;
        else if (absInterval == 5 || absInterval == 7) score += 0.25f;
        else score -= 0.45f + 0.10f * (float)(absInterval - 7);

        if (soundCloud && absInterval > 5)
            score -= 2.0f;

        // After a leap, favour a direction change so the line "balances".
        if (absInterval >= 5)
        {
            if (index > 0)
                score += (interval > 0 ? -0.35f : 0.35f);
        }

        // Phrase contour: rise toward the peak, then descend toward the end.
        float contour = 0.0f;
        if (barOffset % 4 == peakBar)
            contour = (x < 10 ? 0.22f : -0.10f);
        else if (phrasePos == 2)
            contour = (x < 8 ? 0.10f : -0.16f);
        else if (isPhraseEnd)
            contour = -0.18f;
        else if (isPhraseStart)
            contour = 0.08f;

        score += contour * (float)(note - previous);

        if (barOffset % 4 == peakBar)
            score += 1.8f - 0.035f * std::abs(note - desiredPeak);

        // Keep the singer/register coherent.
        if (note >= 58 && note <= 88) score += 1.0f;
        score -= 0.025f * std::abs(note - 74);

        // Rhythmic emphasis: don't put the most unstable notes on strong beats.
        if (strong && !isChord)
            score -= 1.4f;

        // Motif identity.  Map this bar's onset to the closest onset in the
        // previous bar and reward a recognizable pitch relationship.
        if (!prevBar.empty())
        {
            const auto it = std::min_element(prevBar.begin(), prevBar.end(),
                [x](const NoteEvent& a, const NoteEvent& b)
                {
                    return std::abs((a.step % 16) - x)
                         < std::abs((b.step % 16) - x);
                });

            if (it != prevBar.end())
            {
                const int prevPitch = it->note;
                const int same = snapToScale(prevPitch);
                if (std::abs(note - same) <= 1)
                    score += 2.7f * motifStrength;
                else
                {
                    const int intervalNow = note - previous;
                    const int intervalPrev = prevPitch - (prevBar.size() > 1
                        ? prevBar.front().note : prevPitch);
                    if ((intervalNow > 0) == (intervalPrev > 0)
                        && std::abs(intervalNow) <= 5)
                        score += 0.8f * motifStrength;
                }
            }
        }

        // Controlled novelty: occasionally favour a scale tone that is not
        // identical to the previous motif.  This prevents sterile copying.
        if (variationAmount > 0.01f && !isChord && phrasePos == 2)
            score += 0.35f * variationAmount;

        return score;
    };

    // ---- Generate the phrase notes ---------------------------------------------
    for (int index = 0; index < (int)chosen.size(); ++index)
    {
        const int x = chosen[(size_t)index];
        const bool accent = (x % 4) == 0;
        const bool ending = isPhraseEnd && (x >= 12 || index == (int)chosen.size() - 1);
        const bool peakPosition = (barOffset % 4 == peakBar && x >= 6 && x <= 10);

        std::vector<int> candidates;
        candidates.reserve(32);

        // Candidate pool around the previous pitch.
        for (int d = -9; d <= 9; ++d)
            candidates.push_back(previous + d);

        // Add chord tones and useful scale degrees explicitly.
        for (int ct : chordTones)
            for (int o = -1; o <= 1; ++o)
                candidates.push_back(ct + 12 * o);

        for (int d : { 0, 1, 2, 3, 4, 5, 6 })
            candidates.push_back(degreeToPitch(degree + d, octave));

        // Add previous motif pitches to make A/A' behaviour possible.
        for (const auto& ev : prevBar)
            candidates.push_back(ev.note);

        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        float bestScore = -100000.0f;
        int bestNote = snapToScale(previous);

        for (int raw : candidates)
        {
            int note = juce::jlimit(48, 98, snapToScale(raw));

            // Keep consecutive notes reasonably singable.
            if (std::abs(note - previous) > (soundCloud ? 7 : 10))
                continue;

            float score = scoreCandidate(note, x, index);

            // Final cadence: strongly favour stable chord tones.
            if (ending)
            {
                bool rootOrThird = false;
                for (int o = -1; o <= 1; ++o)
                    if (note == snapToScale(chordTones[0] + 12 * o)
                        || note == snapToScale(chordTones[2] + 12 * o))
                        rootOrThird = true;
                if (rootOrThird) score += 3.5f;
            }

            // One "character" note per phrase: allow a tasteful non-chord
            // scale tone around the middle, but never force chromaticism.
            if (!soundCloud && phrasePos == 2 && x >= 6 && x <= 10
                && !ending && !accent)
            {
                if ((note % 12) != (chordTones[0] % 12))
                    score += 0.25f;
            }

            // Small stochastic tie-break keeps variations alive without
            // destroying the musical scoring.
            score += r.nextFloat() * 0.22f;

            if (score > bestScore)
            {
                bestScore = score;
                bestNote = note;
            }
        }

        int note = juce::jlimit(48, 98, snapToScale(bestNote));

        // A phrase ending should actually resolve rather than merely be "close".
        if (ending)
        {
            const int root = snapToScale(chordTones[0]);
            const int third = snapToScale(chordTones[2]);
            note = (r.nextFloat() < 0.68f) ? root : third;

            // Keep the cadence in the current register.
            while (note - previous > 9) note -= 12;
            while (previous - note > 9) note += 12;
            note = juce::jlimit(48, 98, snapToScale(note));
        }

        // ---- Duration / articulation -------------------------------------------
        int len = 1;
        if (ending)
            len = (r.nextFloat() < 0.72f) ? 3 : 2;
        else if (peakPosition)
            len = (r.nextFloat() < 0.55f) ? 2 : 1;
        else if (accent && r.nextFloat() < 0.45f * melodyLength)
            len = 2;
        else if (r.nextFloat() < 0.08f * melodyLength)
            len = 3;

        if (soundCloud)
        {
            len = (ending || r.nextFloat() < 0.62f) ? 2 : 1;
            if (peakPosition && r.nextFloat() < 0.65f)
                len = 4;
        }

        len = juce::jmin(len, 16 - x);

        // Leave intentional breathing room on weak offbeats.
        const bool ghost = !accent && !hook && !soundCloud
            && r.nextFloat() < ghostChance * 0.55f;

        // Expressive dynamics: phrase peak is the strongest point, cadence
        // relaxes slightly after the peak.
        int velocity = 72;
        if (accent) velocity += 7;
        if (peakPosition) velocity += 10;
        if (ending) velocity += 3;
        if (ghost) velocity -= 18;
        if (phrasePos == 3 && !peakPosition) velocity -= 4;
        velocity += r.nextInt(7) - 3;
        velocity = juce::jlimit(45, 116, velocity);

        s.notes.push_back(
            { barOffset * 16 + x, len, note, velocity, 3, ghost });

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
