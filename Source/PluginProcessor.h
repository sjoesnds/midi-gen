#pragma once
#include <JuceHeader.h>
#include "TasteModel.h"
#include <array>
#include <vector>
#include <atomic>
#include <memory>
class MidiForgeAudioProcessor : public juce::AudioProcessor
{
public:
enum Genre {
    Universal, Trap, House, Techno, BoomBap, Ambient, Cinematic,
    RnB, GenrePop, Drill, DnB, Jersey, Afro, Hyperpop, Experimental, Lofi
};
enum ScaleType { Major, Minor, Dorian, Phrygian, HarmonicMinor, MelodicMinor, Pentatonic };
enum Progression { AutoProg, Pop, Dark, Emotional, CinematicProg, JazzLike, Looping };
enum Rhythm { Straight, Syncopated, Broken, Euclidean };
enum SectionMode { Loop, SongMode, SongExtended };
enum Mood { NeutralMood, DarkMood, MelancholicMood, EuphoricMood, AggressiveMood, DreamyMood, NostalgicMood, MysteriousMood, EnergeticMood };
enum MelodyType { HookMelody, VocalLikeMelody, RiffMelody, OstinatoMelody, ArpMelody, CounterMelody, SparseLeadMelody, PhraseMelody };
MidiForgeAudioProcessor();
~MidiForgeAudioProcessor() override = default;
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
int getNumPrograms() override { return 1; }
int getCurrentProgram() override { return 0; }
void setCurrentProgram(int) override {}
const juce::String getProgramName(int) override { return {}; }
void changeProgramName(int, const juce::String&) override {}
void getStateInformation(juce::MemoryBlock&) override;
void setStateInformation(const void*, int) override;
void regenerate();
void regenerateVariations();
// Magic Overhaul: explore the whole musical state coherently.
void magicRandomize();
void rerollSameDNA();
void mutateSelected(float amount = 0.45f);
void evolveSelected();
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
void setSwing(float); void setHumanize(float); void setComplexity(float, bool regenerateNow = true);
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
void setDrumMuteMask (int m) { drumMuteMask = m & 0xFF; }
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
int getVariationCount() const { return static_cast<int>(variations.size()); }
int getSelectedVariation() const { return selectedVariation; }
uint32_t getGenerationNonce() const { return generationNonce; }
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
juce::String getTasteSummary() const;
bool getTasteEnabled() const { return tasteEnabled; }
void setTasteEnabled (bool on) { tasteEnabled = on; }
void resetTaste();
void trainTaste (int varIndex, float likeTarget, float weight);
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
    std::vector<NoteEvent> notes;
};
struct SongData {
    std::vector<Section> sections;
};
// 0.42 Articulation is decided from the melody line itself when a MIDI file is written,
// so it never has to survive piano-roll edits.
struct ArtInfo { bool slide = false; int slideToStep = -1; bool vib = false; };
std::vector<ArtInfo> articulationFor (const std::vector<NoteEvent>& notes) const;
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
uint32_t generationNonce = 0;
uint32_t generationSeed = 0;
// Magic DNA 2.0: coherent latent targets used by the candidate judge.
float dnaMelody = 0.50f, dnaRhythm = 0.50f, dnaHarmony = 0.50f, dnaMotif = 0.50f;
float dnaRegister = 0.50f, dnaGroove = 0.50f, dnaEnergy = 0.50f, dnaSurprise = 0.35f;
uint32_t magicDnaSeed = 0xC0FFEEu;
int sectionMode = Loop;
float chordDensity = 0.9f, bassDensity = 0.8f, melodyDensity = 0.62f, arpDensity = 0.25f;
float swing = 0.0f, humanize = 0.15f, complexity = 0.55f;
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
// Глобальный счётчик сэмплов и очередь отложенных note-off — раньше note-off
// пытались влезть в текущий блок и обрезали длинные ноты (аккорды/бас) почти до нуля.
juce::int64 samplePosition = 0;
// 0.45.1: one queue for BOTH note-on and note-off. A note-on delayed by swing can land beyond the current
// block (illegal for VST3), so it waits here; a stale note-off can never cut a retriggered note.
struct PendingMidi { juce::int64 globalSample; int channel; int note; int velocity; bool on; };
std::vector<PendingMidi> pendingEvents;
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
taste::Model tasteModel;
taste::Vec tasteMean {};
taste::Vec tasteStd = [] { taste::Vec v; v.fill (1.0f); return v; }();
bool tasteEnabled = true;
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
void buildBaseSong(SongData& song, juce::Random& random, int variationSalt = 0);
void buildSection(Section& section, int sectionIndex, const std::vector<int>& prog, juce::Random& random,
                  const std::vector<NoteEvent>* inheritedMotif = nullptr, int variationSalt = 0);
void addChords(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
void addBass(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
void add808(Section&, int barOffset, float localEnergy, juce::Random&, int variationSalt);
void addDrums(Section&, int barOffset, float localEnergy, juce::Random&, int variationSalt);
void addMelody(Section&, int barOffset, float localEnergy, juce::Random&,
               const std::vector<NoteEvent>* inheritedMotif, int variationSalt = 0);
void addArp(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
void buildVariationBank();
Section mergedSelectedSong() const;
void emitNote(const NoteEvent&, juce::MidiBuffer&, int sampleOffset, int velocityBias);
JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiForgeAudioProcessor)
};
