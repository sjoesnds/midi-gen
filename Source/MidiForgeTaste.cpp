#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

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
    tasteModel.update(z, soundTarget, likeTarget, weight);
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

void MidiForgeAudioProcessor::logFeedback (int vi, const char* verdict) const
{
    static constexpr const char* archetypes[8] = { "HOOK", "GROOVE", "HARMONY", "MOTIF", "MINIMAL", "WEIRD", "EMOTIONAL", "WILDCARD" };
    static constexpr const char* transforms[9] = { "ORIGINAL", "CLOSE", "RHYTHMIC", "CONTRAST", "REGISTER", "MOTIF", "EXPERIMENTAL", "WILDCARD", "SIMILAR" };
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
        out << "time_utc,engine,verdict,slot,transform,archetype,mood,melody_type,sound,era,scale,progression,bars,bpm,complexity,energy,melody_density,melody_notes,total_notes,generation_seed,dna_seed\n";
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
