#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>

class MidiForgeAudioProcessor : public juce::AudioProcessor
{
public:
    enum Genre { Universal, Trap, House, Techno, BoomBap, Ambient, Cinematic };
    enum ScaleType { Major, Minor, Dorian, Phrygian, HarmonicMinor, MelodicMinor, Pentatonic };
    enum Progression { AutoProg, Pop, Dark, Emotional, CinematicProg, JazzLike, Looping };
    enum Rhythm { Straight, Syncopated, Broken, Euclidean };
    enum SectionMode { Loop, SongMode, SongExtended };

    MidiForgeAudioProcessor();
    ~MidiForgeAudioProcessor() override = default;

    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MIDI Forge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    bool isSynth() const override { return true; }
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
    void chooseVariation(int index);

    bool exportMidi(const juce::File& targetFile) const;

    // Main controls
    void setRoot(int); void setGenre(int); void setScale(int);
    void setProgression(int); void setRhythm(int); void setBars(int);
    void setSeed(int); void setOctave(int); void setSectionMode(int);

    // Musical controls
    void setChordDensity(float); void setBassDensity(float);
    void setMelodyDensity(float); void setArpDensity(float);
    void setSwing(float); void setHumanize(float); void setComplexity(float);
    void setMelodyLength(float); void setPauseChance(float);
    void setLeapChance(float); void setGhostChance(float);
    void setArpRate(int); void setVoicingWidth(float);
    void setChordExtensions(bool); void setInversions(bool);
    void setMotifStrength(float); void setVariationAmount(float);
    void setFillAmount(float); void setEnergy(float);
    void setChordsEnabled(bool); void setBassEnabled(bool);
    void setMelodyEnabled(bool); void setArpEnabled(bool); void setHookMode(bool);

    // Smart Lock
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
    int getVariationCount() const { return static_cast<int>(variations.size()); }
    int getSelectedVariation() const { return selectedVariation; }

    // MIDI export
    juce::MidiFile buildMidiFile(int channelFilter = 0) const;
    bool exportMidiFileTo(const juce::File& file) const;
    bool exportMidiFileToChannel(const juce::File& file, int channel) const;
    juce::File writeTemporaryMidiFile() const;
    juce::File writeTemporaryMidiFileForChannel(int channel) const;

    // For UI
    struct VisibleNote { int step; int length; int note; int velocity; int channel; };
    std::vector<VisibleNote> getVisibleNotes() const;
    int getVisibleBars() const;
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

    mutable juce::CriticalSection variationsLock;
    mutable juce::CriticalSection activeNotesLock;
    std::vector<Section> variations;
    int selectedVariation = 0;
    std::vector<NoteEvent> activeNotes;
    int activeBars = 4;

    double sampleRate = 44100.0;
    int rootPc = 0, genre = Universal, scale = Minor, progression = AutoProg;
    int rhythm = Straight, bars = 4, seed = 1337, octave = 4;
    int sectionMode = SongMode;
    float chordDensity = 0.9f, bassDensity = 0.8f, melodyDensity = 0.62f, arpDensity = 0.25f;
    float swing = 0.0f, humanize = 0.15f, complexity = 0.55f;
    float melodyLength = 0.35f, pauseChance = 0.10f, leapChance = 0.18f, ghostChance = 0.08f;
    float voicingWidth = 0.45f;
    float motifStrength = 0.78f, variationAmount = 0.40f, fillAmount = 0.18f, energy = 0.65f;
    int arpRate = 4;
    bool chordExtensions = true, inversions = true;
    bool chordsEnabled = true, bassEnabled = true, melodyEnabled = true, arpEnabled = false;
    bool hookMode = true;
    bool lockChordsLayer = false, lockBassLayer = false, lockMelodyLayer = false, lockArpLayer = false;

    std::atomic<int> lastGlobalStep { -1 };
    juce::Random realtimeRng { 0x51eed };
    std::atomic<int> uiCurrentStep { -1 };
    double currentBpm = 120.0;
    juce::int64 samplePosition = 0;

    struct PendingOff { juce::int64 globalSample; int channel; int note; };
    std::vector<PendingOff> pendingOffs;

    std::vector<int> scaleSemitones() const;
    std::vector<int> progressionDegrees() const;
    int degreeToPitch(int degree, int baseOctave) const;
    int snapToScale(int midi) const;
    bool rhythmHit(int stepInBar) const;
    void buildBaseSong(SongData& song, juce::Random& random, int variationSalt = 0);
    void buildSection(Section& section, int sectionIndex, const std::vector<int>& prog, juce::Random& random,
                      const std::vector<NoteEvent>* inheritedMotif = nullptr, int variationSalt = 0);
    void addChords(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
    void addBass(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
    void addMelody(Section&, int barOffset, float localEnergy, juce::Random&,
                   const std::vector<NoteEvent>* inheritedMotif, int variationSalt = 0);
    void addArp(Section&, int barOffset, int degree, float localEnergy, juce::Random&);
    void buildVariationBank();
    Section mergedSelectedSong() const;
    void emitNote(const NoteEvent&, juce::MidiBuffer&, int sampleOffset, int velocityBias);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiForgeAudioProcessor)
};
