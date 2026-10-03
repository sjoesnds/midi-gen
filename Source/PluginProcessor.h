#pragma once
#include <JuceHeader.h>
#include "TasteModel.h"
#include "RhythmGrammar.h"
#include "ComposerGrammar.h"
#include "CreativeRange.h"
#include "MotifSemantics.h"
#include "LoopClosure.h"
#include "ComposerJudge.h"
#include <array>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
inline constexpr const char* kMidiForgeEngineVersion = "0.80.0";

class MidiForgeAudioProcessor : public juce::AudioProcessor
{
public:
enum Genre {
    Universal, Trap, House, Techno, BoomBap, Ambient, Cinematic,
    RnB, GenrePop, Drill, DnB, Jersey, Afro, Hyperpop, Experimental, Lofi
};
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
// genre and sound can be measured (see tools/analyze_feedback.py). Local file only, nothing is sent anywhere.
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
void setRoot(int); void setGenre(int); void setScale(int);
void setMood(int); void setMelodyType(int); void setEra(int);
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
int getGenre() const { return genre; }
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
// 0.44 Chord style (0 Auto by genre, 1 Held, 2 Comping) and the optional Drums layer (MIDI channel 10)
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
int getEra() const { return era; }
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
// Implicit taste signal: dragging / exporting a loop to the DAW is a weak positive sample (weight 0.5). Counted once per
// loop, only if the loop was not rated explicitly, and only while Taste learning is on. Returns true if a sample was added.
bool noteKeptVariation();
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
          stepPenalty=0, registerScore=0, surprise=0, context=0.5f, velocity=0.5f,
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
    IdeaFingerprint idea {};
};

struct AdaptiveProfile
{
    bool ready = false;
    float density = 0.50f;
    float space = 0.50f;
    float rhythm = 0.50f;
    float motif = 0.50f;
    float leap = 0.30f;
    float reg = 0.50f;
    float surprise = 0.30f;
    float loop = 0.50f;
    float groove = 0.50f;
    float memory = 0.50f;
};

struct PhraseMotif
{
    std::vector<int> relativePitches;
    std::vector<int> relativeSteps;
    std::vector<int> lengths;
};
// 0.42 Articulation is decided from the melody line itself when a MIDI file is written,
// so it never has to survive piano-roll edits.
struct ArtInfo { bool slide = false; int slideToStep = -1; bool vib = false; };
std::vector<ArtInfo> articulationFor (const std::vector<NoteEvent>& notes) const;
void applyHumanPerformance (Section& section) const;
void applyMotifDevelopment (Section& section, int phraseStartBar, int variationSalt) const;
void applyMotifSemantics (Section& section, int phraseStartBar, int variationSalt) const;
float motifSemanticsScore (const Section& section, uint32_t identity) const;
void applyLoopClosure (Section& section, uint32_t identity) const;
float loopClosureScore (const Section& section, uint32_t identity) const;
struct ComposerJudgeInputs
{
    MelodyFeatures features {};
    float motifMemory = 0.5f;
    float grooveQuality = 0.5f;
    float rhythmGrammar = 0.5f;
    float melodyExpression = 0.5f;
    float harmonicIntelligence = 0.5f;
    float composerGrammar = 0.5f;
    float melodicProsody = 0.5f;
    float motifSemantics = 0.5f;
    float loopClosure = 0.5f;
    float development = 0.5f;
};
float composerJudgeScore (const Section& section, uint32_t identity,
                          const ComposerJudgeInputs& inputs) const;
void addArticulation (juce::MidiMessageSequence& track, const ArtInfo& a, int channel,
                      double onTick, double& offTick, double ticksPerStep) const;
// variations/selectedVariation читаются в audio-потоке (processBlock) и пишутся
// из GUI-потока (регенерация, выбор варианта) — доступ защищён этим локом.
mutable juce::CriticalSection variationsLock;
mutable juce::CriticalSection activeNotesLock;
std::vector<Section> variations;
int selectedVariation = 0;
std::vector<NoteEvent> activeNotes;
void syncEditedNotesToSelectedVariation (const std::vector<VisibleNote>& notes);
int activeBars = 4;
double sampleRate = 44100.0;
int rootPc = 0, genre = Universal, scale = Minor, progression = AutoProg;
int mood = NeutralMood, melodyType = HookMelody, era = 5;
int soundTarget = 0; // 0 Piano, 1 Pluck, 2 Synth Lead, 3 Bell, 4 Pad/Strings, 5 Brass, 6 808/Sub Lead, 7 Guitar
int chordStyle = 0;
bool drumsEnabled = false;
int drumMuteMask = 0;
int drumPitchMode = 0;
int articulation = 0;   // Off by default: overlapping notes sound like dyads on a polyphonic patch
bool autoNextOnDislike = true;
int rhythm = Straight, bars = 4, seed = 1337, octave = 4;
std::atomic<uint32_t> generationNonce { 0 };
// 0.78 background generation
#ifdef MIDIFORGE_HEADLESS
std::atomic<bool> asyncGeneration { false };
#else
std::atomic<bool> asyncGeneration { true };
#endif
std::thread generationThread;
std::mutex generationThreadMutex;   // guards the std::thread object itself
std::mutex generationMutex;         // guards generating / regenQueued / queuedSelection
std::atomic<bool> generating { false };
std::atomic<uint32_t> generationDone { 0 };
bool regenQueued = false;
int queuedSelection = 0;
uint32_t generationSeed = 0;
// Magic DNA 2.0: coherent latent targets used by the candidate judge.
float dnaMelody = 0.50f, dnaRhythm = 0.50f, dnaHarmony = 0.50f, dnaMotif = 0.50f;
float dnaRegister = 0.50f, dnaGroove = 0.50f, dnaEnergy = 0.50f, dnaSurprise = 0.35f;
uint32_t magicDnaSeed = 0xC0FFEEu;
int sectionMode = Loop;
float chordDensity = 0.9f, bassDensity = 0.8f, melodyDensity = 0.62f, arpDensity = 0.25f;
float swing = 0.0f, humanize = 0.15f, complexity = 0.55f;
bool humanizeEnabled = false;
float melodyLength = 0.35f, pauseChance = 0.10f, leapChance = 0.18f, ghostChance = 0.08f;
float voicingWidth = 0.45f;
float motifStrength = 0.78f, variationAmount = 0.40f, fillAmount = 0.18f, energy = 0.65f;
int arpRate = 4;
bool chordExtensions = true, inversions = true;
bool chordsEnabled = true, bassEnabled = true, melodyEnabled = true, arpEnabled = false;
bool hookMode = true;
// "SoundCloud"-лид: реже, разреженнее, меньше украшений, больше "чант"-повторов
// одной-двух нот — характерный меланхоличный pluck-стиль вместо занятого хука.
bool leadStyleSoundCloud = false;
bool lockChordsLayer = false, lockBassLayer = false, lockMelodyLayer = false, lockArpLayer = false;
std::atomic<int> lastGlobalStep { -1 };
juce::Random realtimeRng { 0x51eed };
// Текущий шаг воспроизведения (для метра/пиано-ролла в UI), обновляется в processBlock.
std::atomic<int> uiCurrentStep { -1 };
// Реальный темп хоста (BPM) — берётся из PlayHead каждый блок, раньше был захардкожен на 120.
std::atomic<double> currentBpm { 120.0 };
// Realtime snapshots: MAGIC/search mutates musical controls on the message thread,
// while processBlock reads these values on the audio thread.
std::atomic<float> realtimeSwing { 0.0f };
std::atomic<float> realtimeHumanize { 0.15f };
std::atomic<bool> realtimeHumanizeEnabled { false };
std::atomic<int> realtimeDrumMuteMask { 0 };
// Глобальный счётчик сэмплов и очередь отложенных note-off — раньше note-off
// пытались влезть в текущий блок и обрезали длинные ноты (аккорды/бас) почти до нуля.
juce::int64 samplePosition = 0;
// 0.45.1: one queue for BOTH note-on and note-off. A note-on delayed by swing can land beyond the current
// block (illegal for VST3), so it waits here; a stale note-off can never cut a retriggered note.
struct PendingMidi { juce::int64 globalSample; int channel; int note; int velocity; bool on; };
std::vector<PendingMidi> pendingEvents;
juce::MidiBuffer outScratch;   // 0.79.x: reused every block instead of allocating a MidiBuffer in the audio thread
// 0.45.1: swing in MIDI ticks for exported / dragged files (same rule as the live output: odd 16ths move by swing/2 of a step)
double swingTicks (int step, double ticksPerStep) const { return (step & 1) != 0 ? (double) swing * ticksPerStep * 0.5 : 0.0; }
double swungEndTick (int step, int len, double ticksPerStep) const { return (double) (step + len) * ticksPerStep + swingTicks (step + len, ticksPerStep); }
uint32_t mutationCounter = 0;   // 0.45.1: every MUTATE press gets its own random rolls
// --- Профиль вкуса: бегущие средние признаков лайкнутых/дизлайкнутых вариаций ---
float likedD = 0, likedE = 0, likedC = 0; int likedN = 0;
float disD = 0, disE = 0, disC = 0; int disN = 0;
std::array<int,8> likeCounts {}; std::array<int,8> dislikeCounts {};
// Taste Learning 2.0: persistent musical feature preferences.
// [density, velocity, leap, rhythm, repetition, variety, register, noteLength]
std::array<float,8> likedFeatures {};
std::array<float,8> dislikedFeatures {};
int likedFeatureN = 0, dislikedFeatureN = 0;
juce::File preferencesFile;
juce::File feedbackFile;
mutable juce::CriticalSection feedbackLock;
taste::Model tasteModel;
taste::Vec tasteMean {};
taste::Vec tasteStd = [] { taste::Vec v; v.fill (1.0f); return v; }();
bool tasteEnabled = true;
uint32_t keptNonce = 0;
unsigned keptMask = 0;
void sampleVariationFeatures(int varIndex, float& d, float& e, float& c) const;
void applyLearnedWeights();
void loadPreferences();
void savePreferences();
void sampleVariationTaste(int varIndex, std::array<float,8>& features) const;
void applyTasteToCandidate(float& quality, float density, float velocity, float leap,
                           float rhythm, float repetition, float variety,
                           float registerScore, float noteLength) const;
std::vector<int> scaleSemitones() const;
std::vector<int> progressionDegrees() const;
std::vector<int> progressionDegreesRaw() const;
int degreeToPitch(int degree, int baseOctave) const;
int snapToScale(int midi) const;
// 0.38 Register lanes: 0 = chords, 1 = bass, 2 = melody (inclusive MIDI range).
void registerLane (int part, int& lo, int& hi) const;
bool rhythmHit(int stepInBar) const;
void applyRhythmGrammar (Section& section, uint32_t identity) const;
float rhythmGrammarScore (const Section& section) const;
void applyMelodyFoundation (Section& section, uint32_t identity) const;
void applyMelodyExpression (Section& section, uint32_t identity) const;
float melodyExpressionScore (const Section& section) const;
void applyHarmonicIntelligence (Section& section, uint32_t identity) const;
float harmonicIntelligenceScore (const Section& section) const;
void applyPhraseMemory4 (Section& section, uint32_t identity) const;
float phraseMemory4Score (const Section& section) const;
float composerGrammarScore (const Section& section) const;
void applyMelodicProsody (Section& section, uint32_t identity) const;
float melodicProsodyScore (const Section& section) const;
float creativeRangeScore (const Section& section, uint32_t identity) const;
void buildBaseSong(SongData& song, juce::Random& random, int variationSalt = 0);
void buildSection(Section& section, int sectionIndex, const std::vector<int>& prog, juce::Random& random,
                  const std::vector<NoteEvent>* inheritedMotif = nullptr, int variationSalt = 0);
void addChords(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
void addBass(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
void add808(Section&, int barOffset, float localEnergy, juce::Random&, int variationSalt);
void addDrums(Section&, int barOffset, float localEnergy, juce::Random&, int variationSalt);
void addMelody(Section&, int barOffset, float localEnergy, juce::Random&,
               const std::vector<NoteEvent>* inheritedMotif, int variationSalt = 0);
PhraseMotif extractPhraseMotif (const Section&, int phraseStartBar) const;
void applyHumanPhraseRole (Section&, int barOffset, const PhraseMotif&) const;
void addArp(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
MelodyFeatures melodyFeatures (const Section& sec, uint32_t identity) const;
IdeaFingerprint makeIdeaFingerprint (const Section& sec) const;
float ideaSimilarity (const IdeaFingerprint& a, const IdeaFingerprint& b) const;
float motifMemoryScore (const Section& sec) const;
void applyGrooveEngine (Section& sec, uint32_t identity) const;
float grooveQualityScore (const Section& sec) const;
int rootAtBar (int bar) const;
void applyMagicArchetype (Section& flat, int archetype, uint32_t identity) const;
float similarity (const Section& a, const Section& b) const;
static float behaviorDistance (const Candidate& a, const Candidate& b);
Section flatten (const SongData& song, int candidateIndex, juce::Random& local, int mLo, int mHi) const;
void buildAdaptiveProfile (const std::vector<Candidate>& candidates, int firstPassCandidates, AdaptiveProfile& adaptive) const;
float minDiversityToSelected (const Candidate& candidate, const std::vector<Candidate>& selected) const;
Section transformLoop (Section source, int mode, uint32_t identity) const;
float loopForgeScore (const Section& sec) const;
void finalizeLoop (Section& sec) const;
void buildVariationBank();
void startGeneration (int selectionAfter);
void regenerateBlocking (int selectionAfter);
void refreshHostBpm();
Section mergedSelectedSong() const;
void emitNote(const NoteEvent&, juce::MidiBuffer&, int sampleOffset, int velocityBias, int stepStartOffset);
JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiForgeAudioProcessor)
};
