#pragma once
#include <JuceHeader.h>
#include "TasteModel.h"
#include "RhythmGrammar.h"
#include "ComposerGrammar.h"
#include "CreativeRange.h"
#include "MelodyIntent.h"
#include "MotifSemantics.h"
#include "LoopClosure.h"
#include "ComposerJudge.h"
#include <array>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#ifndef MIDIFORGE_ENGINE_VERSION
#define MIDIFORGE_ENGINE_VERSION "0.85.6"
#endif
inline constexpr const char* kMidiForgeEngineVersion = MIDIFORGE_ENGINE_VERSION;

class MidiForgeAudioProcessor : public juce::AudioProcessor
{
public:
enum ScaleType { Major, Minor, Dorian, Phrygian, HarmonicMinor, MelodicMinor, Pentatonic, Lydian, Mixolydian, Locrian, HarmonicMajor, Blues };
enum Progression { AutoProg, Pop, Dark, Emotional, CinematicProg, JazzLike, Looping };
enum Rhythm { Straight, Syncopated, Broken, Euclidean };
enum SectionMode { Loop, SongMode, SongExtended };
enum Mood { NeutralMood, DarkMood, MelancholicMood, EuphoricMood, AggressiveMood, DreamyMood, NostalgicMood, MysteriousMood, EnergeticMood };
enum MelodyType { HookMelody, VocalLikeMelody, RiffMelody, OstinatoMelody, ArpMelody, CounterMelody, SparseLeadMelody, PhraseMelody };
MidiForgeAudioProcessor();
~MidiForgeAudioProcessor() override;
void prepareToPlay(double, int) override;
void releaseResources() override
{
    const juce::ScopedLock sl (activeNotesLock);
    activeNotes.clear();
    activeBars = 4;
    pendingEvents.clear();
    samplePosition = 0;
    lastGlobalStep.store (-1);
    uiCurrentStep.store (-1);
    clearActiveSnapshot();
}
bool isBusesLayoutSupported(const BusesLayout&) const override;
void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
juce::AudioProcessorEditor* createEditor() override;
bool hasEditor() const override { return true; }
const juce::String getName() const override { return "MIDI Forge"; }
bool acceptsMidi() const override { return true; }
bool producesMidi() const override { return true; }
bool isMidiEffect() const override { return false; }
double getTailLengthSeconds() const override { return 0.0; }
double getHostBpm() const { return currentBpm.load(); }
int getNumPrograms() override { return 1; }
int getCurrentProgram() override { return 0; }
void setCurrentProgram(int) override {}
const juce::String getProgramName(int) override { return {}; }
void changeProgramName(int, const juce::String&) override {}
void getStateInformation(juce::MemoryBlock&) override;
void setStateInformation(const void*, int) override;
void regenerate();
void regenerateVariations();
// 0.78: MAGIC / regenerate run on a worker thread in the plugin (synchronously in the headless QA build).
// While isGenerating() is true the editor blocks input, because the generator reads the live controls.
bool isGenerating() const { return generating.load(); }
uint32_t getGenerationDoneCounter() const { return generationDone.load(); }
void waitForGeneration();
// 0.79 feedback log: one CSV row per LIKE / DISLIKE / export / drag, so the real like-rate of every archetype, transform,
// archetype and sound can be measured (see tools/analyze_feedback.py). Local file only, nothing is sent anywhere.
void setFeedbackLogFile (const juce::File& f) { feedbackFile = f; }
// Tests point this at a temporary folder BEFORE creating any processor, so taste.json / feedback.csv of the real user are never read or written.
static void setSettingsDirectoryOverride (const juce::File& dir);
juce::File getFeedbackLogFile() const { return feedbackFile; }
void logFeedback (int variationIndex, const char* verdict) const;
void setAsyncGeneration (bool on) { asyncGeneration.store (on); }   // QA hook; headless builds default to synchronous
// Magic Overhaul: explore the whole musical state coherently.
void magicRandomize();
void rerollSameDNA();
void mutateSelected(float amount = 0.45f);
void evolveSelected();
// 0.80 SIMILAR ("more like this"): replaces the bank with the selected loop (slot 1, untouched) and seven close relatives of it,
// ordered from nearest to furthest. Returns how many relatives passed the musicality and diversity gates.
int similarToSelected();
void chooseVariation(int index);
bool exportMidi(const juce::File& targetFile) const;
// Main controls
void setRoot(int); void setScale(int);
void setMood(int); void setMelodyType(int);
void setProgression(int); void setRhythm(int); void setBars(int);
void setSeed(int); void setOctave(int); void setSectionMode(int);
void setSoundTarget(int);
// Musical controls
void setChordDensity(float, bool regenerateNow = true); void setBassDensity(float, bool regenerateNow = true);
void setMelodyDensity(float, bool regenerateNow = true); void setArpDensity(float, bool regenerateNow = true);
void setSwing(float); void setHumanize(float); void setHumanizeEnabled(bool); void setComplexity(float, bool regenerateNow = true);
void setMelodyLength(float, bool regenerateNow = true); void setPauseChance(float, bool regenerateNow = true);
void setLeapChance(float, bool regenerateNow = true); void setGhostChance(float, bool regenerateNow = true);
void setArpRate(int); void setVoicingWidth(float);
void setChordExtensions(bool); void setInversions(bool);
void setMotifStrength(float, bool regenerateNow = true); void setVariationAmount(float, bool regenerateNow = true);
void setFillAmount(float, bool regenerateNow = true); void setEnergy(float, bool regenerateNow = true);
void setChordsEnabled(bool); void setBassEnabled(bool);
void setMelodyEnabled(bool); void setArpEnabled(bool); void setHookMode(bool);
void setLeadStyleSoundCloud(bool v) { leadStyleSoundCloud = v; }
// --- Smart Lock: заморозка отдельной партии при регенерации ---
void setLockChords(bool v) { lockChordsLayer = v; }
void setLockBass(bool v)   { lockBassLayer = v; }
void setLockMelody(bool v) { lockMelodyLayer = v; }
void setLockArp(bool v)    { lockArpLayer = v; }
bool getLockChords() const { return lockChordsLayer; }
bool getLockBass() const   { return lockBassLayer; }
bool getLockMelody() const { return lockMelodyLayer; }
bool getLockArp() const    { return lockArpLayer; }
int getRoot() const { return rootPc; }
int snapPitchToScale (int midi) const { return snapToScale (midi); }
int getScale() const { return scale; }
int getProgression() const { return progression; }
int getRhythm() const { return rhythm; }
int getBars() const { return bars; }
int getSeed() const { return seed; }
int getOctave() const { return octave; }
int getSectionMode() const { return sectionMode; }
int getMood() const { return mood; }
int getMelodyType() const { return melodyType; }
int getSoundTarget() const { return soundTarget; }
// 0.42 Articulation (0 off, 1 slides, 2 slides + vibrato) - applies to profiles that support it (Synth Lead, 808)
int getArticulation() const { return articulation; }
// 0.44 Chord style (0 Auto, 1 Held, 2 Comping) and the optional Drums layer (MIDI channel 10)
int getChordStyle() const { return chordStyle; }
void setChordStyle (int v);
bool isDrumsEnabled() const { return drumsEnabled; }
// 0.45 Drum section: every instrument is its own row (own track, own drag, own mute)
static constexpr int kDrumRows = 8;                       // Kick, Snare, Clap, Hat, Open Hat, Toms, Crash, Shaker
static const char* drumRowName (int row);
static int drumRowForNote (int gmNote);                   // -1 if not a drum pitch
static int drumRowNote (int row);                         // GM pitch used when a hit is added by hand
int getDrumMuteMask() const { return drumMuteMask; }
void setDrumMuteMask (int m) { drumMuteMask = m & 0xFF; realtimeDrumMuteMask.store (drumMuteMask); }
int getDrumPitchMode() const { return drumPitchMode; }    // 0 = every hit on C5 (one sample per channel), 1 = General MIDI pitches
void setDrumPitchMode (int m) { drumPitchMode = juce::jlimit (0, 1, m); }
bool toggleDrumHit (int step, int row);                   // true = the hit is now on
juce::File writeTemporaryMidiFileForDrumRow (int row) const;   // row -1 = all drums, one track per instrument
void setDrumsEnabled (bool on);
void setArticulation (int v);
bool getAutoNext() const { return autoNextOnDislike; }
void setAutoNext (bool on) { autoNextOnDislike = on; }
void dislikeAndAdvance();   // DISLIKE the selected loop and move on to the next one
float getChordDensity() const { return chordDensity; }
float getBassDensity() const { return bassDensity; }
float getMelodyDensity() const { return melodyDensity; }
float getArpDensity() const { return arpDensity; }
float getSwing() const { return swing; }
float getHumanize() const { return humanize; }
bool isHumanizeEnabled() const { return humanizeEnabled; }
float getComplexity() const { return complexity; }
float getMelodyLength() const { return melodyLength; }
float getPauseChance() const { return pauseChance; }
float getLeapChance() const { return leapChance; }
float getGhostChance() const { return ghostChance; }
int getArpRate() const { return arpRate; }
float getVoicingWidth() const { return voicingWidth; }
bool getChordExtensions() const { return chordExtensions; }
bool getInversions() const { return inversions; }
float getMotifStrength() const { return motifStrength; }
float getVariationAmount() const { return variationAmount; }
float getFillAmount() const { return fillAmount; }
float getEnergy() const { return energy; }
bool isChordsEnabled() const { return chordsEnabled; }
bool isBassEnabled() const { return bassEnabled; }
bool isMelodyEnabled() const { return melodyEnabled; }
bool isArpEnabled() const { return arpEnabled; }
bool getHookMode() const { return hookMode; }
bool getLeadStyleSoundCloud() const { return leadStyleSoundCloud; }
int getVariationCount() const { const juce::ScopedLock sl (variationsLock); return static_cast<int>(variations.size()); }
int getVariationMelodyCharacter (int index) const;
int getVariationMelodyComplexityClass (int index) const;
int getSelectedVariation() const { const juce::ScopedLock sl (variationsLock); return selectedVariation; }
uint32_t getGenerationNonce() const { return generationNonce.load(); }
// --- Learning: лайк/дизлайк текущей вариации, профиль вкуса влияет на следующий GENERATE ---
void likeVariation(int varIndex);
void dislikeVariation(int varIndex);
int getLikeCount(int varIndex) const;
int getDislikeCount(int varIndex) const;
int getVariationScore(int varIndex) const;
int getTasteLikes() const { return likedN; }
int getTasteDislikes() const { return disN; }
// 0.40 Taste ML (online logistic regression, see TasteModel.h)
float getTasteConfidence() const { return tasteModel.confidence(); }
float getTasteSamples() const { return tasteModel.samples(); }
juce::String getTasteSummary() const;
bool getTasteEnabled() const { return tasteEnabled; }
void setTasteEnabled (bool on) { tasteEnabled = on; }
void resetTaste();
void trainTaste (int varIndex, float likeTarget, float weight);
// Explicit LIKE / DISLIKE are the only Taste ML training signals; drag/export remain telemetry-only.
// --- MIDI export: рендерит текущий выбранный вариант в стандартный .mid файл ---
// channelFilter: 0 = все партии, 1..4 = только Chords/Bass/Melody/Arp
juce::MidiFile buildMidiFile (int channelFilter = 0, int drumRow = -1) const;
int drumOutPitch (int row, int gmNote) const;
bool exportMidiFileTo (const juce::File& file) const;
bool exportMidiFileToChannel (const juce::File& file, int channel) const;
// Пишет во временную папку — используется для drag-and-drop прямо в FL Studio
juce::File writeTemporaryMidiFile() const;
juce::File writeTemporaryMidiFileForChannel (int channel) const;
// --- Для визуализации в UI (мини пиано-ролл) ---
struct VisibleNote { int step; int length; int note; int velocity; int channel; };
std::vector<VisibleNote> getVisibleNotes() const;
int getVisibleBars() const;
// --- Piano Roll editing -------------------------------------------------
bool addVisibleNote (int step, int note, int length, int velocity, int channel);
bool editVisibleNote (int index, int step, int note, int length, int velocity);
bool deleteVisibleNote (int index);
void replaceVisibleNotes (const std::vector<VisibleNote>& notes);
void quantizeVisibleNotes (int gridSteps);
// Текущий шаг воспроизведения внутри паттерна (0..bars*16-1), -1 если не играет.
int getVisiblePlayheadStep() const { return uiCurrentStep.load(); }
private:
struct NoteEvent {
int step, length, note, velocity, channel;
bool ghost = false;
};
struct Section {
    juce::String name;
    int bars = 4;
    float energy = 0.5f;
    float densityMultiplier = 1.0f;
    int transpose = 0;
    int sourceArchetype = -1;   // 0.79: MAGIC archetype the loop came from (for the feedback log)
    int transformMode = -1;     // 0.79: final transformation slot (ORIGINAL, TIGHT, ...)
    int melodyCharacter = -1;   // 0.85.5: latent melodic behavior family; not exposed as a UI label
    int melodyComplexityClass = -1; // 0.86: 0=simple, 1=medium, 2=complex; one intent for the whole loop
    midiforge::MelodyIntent melodyIntent {};
    bool hasMelodyIntent = false; // The chosen generation intent is reused by later passes and judges.
    std::vector<NoteEvent> notes;
};
struct SongData {
    std::vector<Section> sections;
};

// 0.47 Human Phrase Engine: an explicit four-bar motif keeps the
// generator's "human" identity stable across A / A' / B / A'' roles.
// Timing remains grid-safe; contour, recurrence, contrast, and cadence
// become first-class musical decisions.
struct MelodyFeatures
{
    float density=0, space=0, leap=0, repetition=0, contour=0, variety=0, harmony=0, hook=0;
    float rhythmIdentity=0, motifIdentity=0, phraseMemory=0, seam=0, phraseArc=0, tensionArc=0,
          stepPenalty=0, registerScore=0, registerCenter=0.5f, weakSpot=0.5f,
          simplicity=0.5f, surprise=0, context=0.5f, velocity=0.5f,
          noteLength=0.5f, loopQuality=0.0f, grooveQuality=0.0f;
};

struct IdeaFingerprint
{
    std::array<int, 7> onsets {};
    std::array<int, 7> relativePitches {};
    std::array<int, 6> intervals {};
    int count = 0;
    int intervalCount = 0;
    int family = 0;
};

struct Candidate
{
    Section section;
    float quality = 0.0f;
    uint32_t identity = 0;
    int archetype = 0;
    float density = 0.5f;
    float space = 0.5f;
    float rhythm = 0.5f;
    float motif = 0.5f;
    float leap = 0.3f;
    float reg = 0.5f;
    float surprise = 0.3f;
    float context = 0.5f;
    float loop = 0.5f;
    float groove = 0.5f;
    float memory = 0.5f;
    float phraseArc = 0.5f;
    float tension = 0.5f;
    float development = 0.5f;
    float characterFit = 0.5f;   // 0.85.6: how closely the candidate expresses its latent character
    IdeaFingerprint idea {};
};

struct AdaptiveProfile
{
    bool ready = false;
    float density = 0.50f;
    float space = 0.50f;
    float rhythm = 0.50f;