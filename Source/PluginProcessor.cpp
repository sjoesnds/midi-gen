#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

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


void MidiForgeAudioProcessor::updateTasteModel(const std::array<float,8>& features, float label)
{
    // Online logistic regression: one example at a time, local-only.
    // The first eight weights correspond to musical features; the ninth is bias.
    float z = tasteModelWeights[8];
    for (int i = 0; i < 8; ++i)
        z += tasteModelWeights[(size_t)i] * juce::jlimit(0.0f, 1.0f, features[(size_t)i]);

    z = juce::jlimit(-12.0f, 12.0f, z);
    const float p = 1.0f / (1.0f + std::exp(-z));
    const float error = label - p;

    for (int i = 0; i < 8; ++i)
    {
        const float x = juce::jlimit(0.0f, 1.0f, features[(size_t)i]);
        // Small L2 regularisation keeps the model from exploding after a few clicks.
        tasteModelWeights[(size_t)i] += tasteModelLearningRate * (error * x - 0.001f * tasteModelWeights[(size_t)i]);
        tasteModelWeights[(size_t)i] = juce::jlimit(-3.0f, 3.0f, tasteModelWeights[(size_t)i]);
    }

    tasteModelWeights[8] += tasteModelLearningRate * error;
    tasteModelWeights[8] = juce::jlimit(-2.0f, 2.0f, tasteModelWeights[8]);
    ++tasteModelSamples;
}

float MidiForgeAudioProcessor::tasteModelProbability(const std::array<float,8>& features) const
{
    if (tasteModelSamples <= 0)
        return 0.5f;

    float z = tasteModelWeights[8];
    for (int i = 0; i < 8; ++i)
        z += tasteModelWeights[(size_t)i] * juce::jlimit(0.0f, 1.0f, features[(size_t)i]);

    z = juce::jlimit(-12.0f, 12.0f, z);
    return 1.0f / (1.0f + std::exp(-z));
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

    // Taste ML 0.37: online logistic regression predicts whether this
    // musical fingerprint resembles what the user has liked.
    const std::array<float,8> modelFeatures =
        { density, velocity, leap, rhythm, repetition, variety, registerScore, noteLength };
    if (tasteModelSamples > 0)
    {
        const float p = tasteModelProbability(modelFeatures);
        const float confidence = juce::jlimit(0.0f, 1.0f, (float)tasteModelSamples / 20.0f);
        quality += 0.24f * confidence * (p - 0.5f) * 2.0f;
    }
}

void MidiForgeAudioProcessor::likeVariation(int vi)
{
    if (vi < 0 || vi > 7) return;
    likeCounts[(size_t)vi]++;
    float d, e, c; sampleVariationFeatures(vi, d, e, c);
    std::array<float,8> f; sampleVariationTaste(vi, f);
    updateTasteModel(f, 1.0f);
    likedD = (likedD * likedN + d) / (likedN + 1);
    likedE = (likedE * likedN + e) / (likedN + 1);
    likedC = (likedC * likedN + c) / (likedN + 1);
    ++likedN;
    for (int i = 0; i < 8; ++i)
        likedFeatures[(size_t)i] = (likedFeatures[(size_t)i] * (float)(likedFeatureN) + f[(size_t)i])
                                   / (float)(likedFeatureN + 1);
    ++likedFeatureN;
    savePreferences();
    applyLearnedWeights();
}

void MidiForgeAudioProcessor::dislikeVariation(int vi)
{
    if (vi < 0 || vi > 7) return;
    dislikeCounts[(size_t)vi]++;
    float d, e, c; sampleVariationFeatures(vi, d, e, c);
    std::array<float,8> f; sampleVariationTaste(vi, f);
    updateTasteModel(f, 0.0f);
    disD = (disD * disN + d) / (disN + 1);
    disE = (disE * disN + e) / (disN + 1);
    disC = (disC * disN + c) / (disN + 1);
    ++disN;
    for (int i = 0; i < 8; ++i)
        dislikedFeatures[(size_t)i] = (dislikedFeatures[(size_t)i] * (float)(dislikedFeatureN) + f[(size_t)i])
                                      / (float)(dislikedFeatureN + 1);
    ++dislikedFeatureN;
    savePreferences();
    applyLearnedWeights();
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
    o->setProperty("likedFeatures", lf); o->setProperty("dislikedFeatures", df);
    o->setProperty("tasteModelSamples", tasteModelSamples);
    o->setProperty("tasteModelLearningRate", (double)tasteModelLearningRate);
    juce::Array<juce::var> mw;
    for (int i = 0; i < 9; ++i)
        mw.add((double)tasteModelWeights[(size_t)i]);
    o->setProperty("tasteModelWeights", mw);
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

        tasteModelSamples = (int)o->getProperty("tasteModelSamples");
        if (tasteModelSamples < 0) tasteModelSamples = 0;
        if (o->hasProperty("tasteModelLearningRate"))
            tasteModelLearningRate = juce::jlimit(0.01f, 0.20f, (float)o->getProperty("tasteModelLearningRate"));
        if (auto* mw = o->getProperty("tasteModelWeights").getArray())
            for (int i = 0; i < 9 && i < mw->size(); ++i)
                tasteModelWeights[(size_t)i] = juce::jlimit(-3.0f, 3.0f, (float)(*mw)[i]);
    }
}

// --- Generation ---------------------------------------------------------
void MidiForgeAudioProcessor::addChords(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
    // Harmony Engine 2.0: build diatonic stacks first, then choose a coherent
    // voicing. No fixed semitone triads: every chord stays inside the active scale.
    std::vector<int> chordDegrees={degree,degree+2,degree+4};
    const float hDNA = juce::jlimit(0.0f,1.0f,dnaHarmony);
    const bool addSeventh = chordExtensions && (complexity>0.40f || hDNA>0.48f) && r.nextFloat() < (0.28f+0.52f*hDNA);
    const bool addNinth = chordExtensions && hDNA>0.62f && complexity>0.58f && r.nextFloat() < (0.12f+0.28f*hDNA);
    if(addSeventh) chordDegrees.push_back(degree+6);
    if(addNinth) chordDegrees.push_back(degree+8);

    const int root=degreeToPitch(degree,octave-1);
    const int transpose = (voicingWidth > 0.55f && (hash32(generationSeed ^ (uint32_t)(barOffset*41+degree*17))%100u)<22u) ? (r.nextBool()?12:-12) : 0;
    const int inversion = (int)(hash32(generationSeed ^ (uint32_t)(barOffset*97+degree*31)) % (uint32_t)chordDegrees.size());
    const int spread = juce::jlimit(0,2,(int)std::round(voicingWidth*2.0f));

    for(int i=0;i<(int)chordDegrees.size();++i)
    {
        if(r.nextFloat()>juce::jlimit(0.f,1.f,chordDensity*(0.65f+0.35f*e))) continue;
        int degreeIndex=i;
        if(chordDegrees.size()>=3)
            degreeIndex=(i+inversion)%(int)chordDegrees.size();
        int note=degreeToPitch(chordDegrees[(size_t)degreeIndex],octave-1);

        // Rotate chord tones into different octaves. The result is still scale-safe,
        // but avoids the block-chord / school-exercise sound.
        int octaveLift=0;
        if(i>0 && degreeIndex<inversion) octaveLift=12;
        if(spread>=1 && i%2==1) octaveLift+=12;
        if(spread>=2 && i==2) octaveLift+=12;
        if(inversions && i==0 && barOffset>0 && (hash32(generationSeed+barOffset*13u)%100u)<55u)
            octaveLift+=12;

        note=juce::jlimit(24,108,note+octaveLift+transpose);
        const int vel=juce::jlimit(45,90,76-i*5+(i==0?5:0));
        s.notes.push_back({barOffset*16,16,note,vel,1,false});
    }

    // Genre-aware rhythmic chord punctuation, still scale-safe.
    if((genre==House||genre==Techno||genre==Jersey) && r.nextFloat()<(0.35f+0.45f*e))
        s.notes.push_back({barOffset*16+8,4,juce::jlimit(24,108,root+12),63,1,false});
    if(chordExtensions && hDNA>0.55f && r.nextFloat()<(0.08f+0.20f*hDNA))
        s.notes.push_back({barOffset*16+8,8,juce::jlimit(24,108,degreeToPitch(degree+8,octave)),60,1,false});
}
void MidiForgeAudioProcessor::addBass(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
int root=juce::jlimit(18,55,degreeToPitch(degree,octave-2));
std::vector<int> steps;
if(genre==Trap || genre==Drill)steps={0,3,6,10,14};
else if(genre==House || genre==Techno || genre==DnB)steps={0,4,8,12};
else if(genre==Jersey || genre==Afro)steps={0,3,8,11,14};
else if(genre==RnB || genre==Lofi)steps={0,8,12};
else steps={0,8,12};
for(int x:steps){
if(!rhythmHit(x)||r.nextFloat()>bassDensity*e)continue;
int note=root;
if(complexity>0.5f&&r.nextFloat()<0.22f)note+=r.nextBool()?7:-5;
note=juce::jlimit(18,60,note);
bool ghost=r.nextFloat()<ghostChance*.5f&&x!=0;
int len=(genre==House||genre==Techno||genre==DnB)?3:
        ((genre==RnB||genre==Lofi)?6:(x==0?7:3));
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

    // 0.26 Melody Engine 3.0: give every 4-bar phrase a compositional grammar.
    // A = statement, A' = variation, B = contrast/peak, A'' = return/cadence.
    // The grammar changes the destination of notes rather than merely adding
    // random pitch offsets, so a loop develops an audible arc.
    const int phraseStyle = (int)(hash32(seed ^ 0x4f1bbcd3u) % 6u);
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
        {1, 6, 8, 13, -1,-1,-1,-1,-1,-1}
    };

    int rhythmType = (int)(hash32(seed ^ (uint32_t)variationSalt * 0x9e3779b9u
                                         ^ (uint32_t)(barOffset + 1) * 0x85ebca6bu) % 24u);
    // Genre DNA nudges the rhythmic family without hard-locking it.
    rhythmType = (rhythmType + dnaRhythmBias + (int)(dnaSync * 3.0f)) % 24;

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
    const float density = juce::jlimit(0.16f, 0.78f,
        0.34f + 0.22f * melodyDensity + 0.12f * e
        + 0.16f * (dnaDensity - 0.50f) - 0.10f * (dnaSpace - 0.50f));
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

    const int motifType = (int)(hash32(seed ^ (uint32_t)(archetype * 0x51ed270bu)
                                           ^ (uint32_t)(dnaMotif * 1000.0f)) % 16u);
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
            if (dnaLeap > 0.55f && ((i + phraseCell) % 4 == 2))
                d += (i & 1) ? 2 : -2;
            else if (dnaSpace > 0.70f && (i % 3 == 1))
                d = motifDegree(globalIndex + cycle);
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
                note = juce::jlimit(48, 98, snapToScale(blended));
            }
        }

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
        const int accent = (int)std::round((float)((int)(h % 9u) - 4) * (2.0f + 7.0f * human));
        if (x % 4 == 0) velocity += 2;
        if (cycle == 2 && (x % 8) == 4) velocity += 3;
        velocity += accent;
        if (human > 0.12f && (h % 100u) < (uint32_t)(18.0f * human))
            len = juce::jmin(4, len + 1);
        if (human > 0.18f && (h % 100u) > 88u)
            len = juce::jmax(1, len - 1);
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
        const float densityTarget = hookMode ? 0.48f : 0.40f;
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
                    // Advanced Magic: rare, scale-safe happy accidents.
                    if(mode >= 31u && mode < 35u)
                    {
                        const int accident = (int)((h >> 14) % 5u);
                        if(accident == 0)
                            n.length = juce::jlimit(1, 16, n.length + 2);
                        else if(accident == 1)
                            n.length = juce::jmax(1, n.length - 2);
                        else if(accident == 2)
                            n.note = juce::jlimit(48, 98, snapToScale(n.note + 12));
                        else if(accident == 3)
                            n.note = juce::jlimit(48, 98, snapToScale(n.note - 12));
                        else
                            n.velocity = juce::jlimit(35, 118, n.velocity + 9);
                    }
                }
                flat.notes.push_back(n);
            }
        }
        juce::ignoreUnused(local);
        return flat;
    };

    std::vector<Candidate> candidates;
    constexpr int candidateCount=1000;
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
        const float genreDensityTarget = juce::jlimit(0.0f,1.0f,dnaDensity);
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
        quality += 0.08f*(1.0f-juce::jlimit(0.0f,1.0f,std::abs(f.density-0.46f)/0.50f));
        quality -= 0.28f*f.stepPenalty;

        // Taste profile nudges the search without collapsing it into one style.
        if(likedN>0) quality += 0.10f*(1.0f-std::abs(f.density-likedD));
        if(disN>0) quality -= 0.08f*(1.0f-std::abs(f.density-disD));

        // Taste Learning 2.0: use a broader musical fingerprint. The effect is
        // deliberately capped so the learned profile guides rather than dictates.
        applyTasteToCandidate(quality, f.density, f.velocity, f.leap, f.rhythmIdentity,
                              f.repetition, f.variety, f.registerScore, f.noteLength);

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

void MidiForgeAudioProcessor::magicRandomize()
{
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
        scale = pick(7);
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
    octave = 3 + pick(4);
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

    const uint32_t base = hash32(generationSeed ^ 0xA17E5EEDu ^ (uint32_t)(amount*1000.0f));
    for(size_t i=0;i<notes.size();++i)
    {
        auto& n=notes[i];
        const uint32_t h=hash32(base ^ (uint32_t)(i*0x9e3779b9u));
        const float roll=(float)(h%1000u)/1000.0f;

        // Each layer has its own mutation grammar. Locked layers are untouched.
        const bool locked=(n.channel==1&&lockChordsLayer)||(n.channel==2&&lockBassLayer)||
                           (n.channel==3&&lockMelodyLayer)||(n.channel==4&&lockArpLayer);
        if(locked) continue;

        const float chance=0.10f+0.48f*amount;
        if(roll<chance)
        {
            if(n.channel==3)
            {
                // Melody: scale-safe pitch mutation, occasional direction flip.
                const int step=((h>>8)&1u)?2:-2;
                n.note=juce::jlimit(48,98,snapToScale(n.note+step));
            }
            else if(n.channel==2)
            {
                // Bass: mostly octave/scale-degree movement, never chromatic.
                const int move=((h>>9)&3u)==0 ? 12 : (((h>>9)&1u)?2:-2);
                n.note=juce::jlimit(18,60,snapToScale(n.note+move));
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
