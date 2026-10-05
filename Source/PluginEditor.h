#pragma once
#include <JuceHeader.h>
#include <memory>
#include <utility>
#include <optional>
#include <array>
#include <functional>
#include <algorithm>
#include "PluginProcessor.h"
class MidiForgeAudioProcessorEditor : public juce::AudioProcessorEditor,
public juce::DragAndDropContainer,
private juce::Timer
{
public:
explicit MidiForgeAudioProcessorEditor(MidiForgeAudioProcessor&);
~MidiForgeAudioProcessorEditor() override { regenerationPending = false; stopTimer(); }
void paint(juce::Graphics&) override;
void resized() override;
private:
void timerCallback() override;
void refreshTaste();
void scheduleRegeneration (bool preserveSelection);
juce::String lastTasteText;
MidiForgeAudioProcessor& processor;
// 0.78: MAGIC runs on a worker thread; this overlay blocks input while it works (the generator reads the live controls).
struct BusyOverlay : public juce::Component, private juce::Timer
{
    explicit BusyOverlay (MidiForgeAudioProcessor& p) : proc (p)
    {
        setInterceptsMouseClicks (true, true);
        setVisible (false);
        startTimerHz (30);
    }
    ~BusyOverlay() override { stopTimer(); }
    void sync()
    {
        const bool busy = proc.isGenerating();
        if (busy != isVisible())
        {
            setVisible (busy);
            if (busy) toFront (false);
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0x99000000));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (26.0f));
        g.drawFittedText ("GENERATING...", getLocalBounds(), juce::Justification::centred, 1);
    }
    void timerCallback() override { sync(); }
    MidiForgeAudioProcessor& proc;
};
BusyOverlay busyOverlay { processor };
uint32_t lastGenerationDone = 0;
juce::Label title, sectionLabel, variationInfoLabel, versionLabel, tempoLabel;
juce::int64 regenerationDueMs = 0;
uint32_t scheduledGenerationNonce = 0;
bool regenerationPending = false;
bool preserveSelectionOnRegenerate = false;
// Queue one MAGIC request while the background generator is busy.
// The processor already coalesces normal regeneration requests; this flag keeps MAGIC clicks from being lost.
bool magicPending = false;
 juce::ComboBox root, scale, progression, rhythm, mode, bars, octave, arpRate, variationBox, moodBox, melodyTypeBox, soundBox, articBox, chordBox;
 juce::Slider chordDensity,bassDensity,melodyDensity,arpDensity;
 juce::Slider swing,humanize,complexity,motifStrength,variationAmount,fillAmount,energy;
 juce::Slider melodyLength,pauseChance,leapChance,ghostChance;
 juce::ToggleButton chords,bass,melody,arp,drums,extensions,inversions,hookModeButton,soundCloudButton,humanizeModeButton;
 juce::ToggleButton lockChordsBtn{"Lock Chords"}, lockBassBtn{"Lock Bass"}, lockMelodyBtn{"Lock Melody"}, lockArpBtn{"Lock Arp"};
 juce::TextButton generate,newSeed,applyVariation,exportMidi;
 juce::ComboBox pianoGridBox;
 juce::TextButton quantizeButton{"QUANTIZE"}, resetViewButton{"RESET VIEW"};
 juce::TextButton phraseButton{"PHRASE"}, barButton{"BAR"}, transposeDownButton{"-12"}, transposeUpButton{"+12"},
                  snapScaleButton{"SCALE"}, humanizeSelectionButton{"HUMANIZE"};
 juce::TextButton duplicateButton{"DUP"}, reverseButton{"REV"}, doubleTimeButton{"2X"},
                  halfTimeButton{"HALF"}, rotateButton{"ROT"}, normalizeVelocityButton{"VEL 100"},
                  frameSelectionButton{"FRAME"};
 juce::Label selectionLabel;
 juce::TextButton mutateButton{"MUTATE"}, evolveButton{"EVOLVE"}, similarButton{"SIMILAR"};
 // --- Learning: лайк/дизлайк текущей вариации + счётчик профиля вкуса ---
 juce::TextButton likeBtn, dislikeBtn;
 juce::Label tasteLabel;
 juce::TextButton resetTasteBtn;
 juce::ToggleButton autoNextBtn;
 juce::ToggleButton tasteToggleBtn;
  // --- P2: edit history -------------------------------------------------
 juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" }, clearBtn { "CLEAR" };
 juce::Label historyLabel;
 std::unique_ptr<juce::FileChooser> fileChooser;
 // Небольшой компонент-"ручка": тащишь мышкой прямо в плейлист/пиано-ролл FL Studio,
 // плагин пишет временный .mid файл и запускает системный drag-and-drop.
 struct DragHandle : public juce::Component
 {
     explicit DragHandle (MidiForgeAudioProcessorEditor& o) : owner (o) {}
     void paint (juce::Graphics& g) override;
     void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
     void mouseDrag (const juce::MouseEvent&) override;
     void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
     MidiForgeAudioProcessorEditor& owner;
     bool dragStarted = false;
     juce::Time lastDragStart;   // защита от двойного старта drag (anti-double-start)
     juce::File activeDragFile;  // временный .mid, живёт до следующего drag
 };
 DragHandle dragHandle { *this };
 // Раздельный drag-and-drop по партиям: тащишь только бас, только аккорды и т.д.
 struct LayerDragHandle : public juce::Component
 {
     LayerDragHandle (MidiForgeAudioProcessorEditor& o, int ch, juce::String lbl)
         : owner (o), channel (ch), label (std::move (lbl)) {}
     void paint (juce::Graphics& g) override
     {
         g.setColour (juce::Colour (0xff3a3a3a));
         g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
         g.setColour (juce::Colours::white);
         g.setFont (11.0f);
         g.drawFittedText (label, getLocalBounds(), juce::Justification::centred, 1);
     }
     void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
     void mouseDrag (const juce::MouseEvent& e) override
     {
         if (dragStarted) return;
         if (e.getDistanceFromDragStart() < 6) return;
         dragStarted = true;
         auto file = owner.processor.writeTemporaryMidiFileForChannel (channel);
         if (!file.existsAsFile()) { dragStarted = false; return; }
         owner.performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
     }
     void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
     MidiForgeAudioProcessorEditor& owner;
     int channel;
     juce::String label;
     bool dragStarted = false;
 };
 LayerDragHandle dragChords { *this, 1, "Drag Chords" };
 LayerDragHandle dragBass   { *this, 2, "Drag Bass" };
 LayerDragHandle dragMelody { *this, 3, "Drag Melody" };
 LayerDragHandle dragArp    { *this, 4, "Drag Arp" };
 LayerDragHandle dragDrums  { *this, 5, "Drag Drums" };
 // 0.45 Drum section: one instrument (row 0-7) or all drums as separate tracks (row -1)
 struct DrumRowDragHandle : public juce::Component
 {
     DrumRowDragHandle (MidiForgeAudioProcessorEditor& o, int r, juce::String lbl)
         : owner (o), row (r), label (std::move (lbl)) {}
     void paint (juce::Graphics& g) override
     {
         g.setColour (juce::Colour (0xff3a3a3a));
         g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
         g.setColour (juce::Colours::white);
         g.setFont (10.0f);
         g.drawFittedText (label, getLocalBounds(), juce::Justification::centred, 1);
     }
     void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
     void mouseDrag (const juce::MouseEvent& e) override
     {
         if (dragStarted) return;
         if (e.getDistanceFromDragStart() < 6) return;
         dragStarted = true;
         auto file = owner.processor.writeTemporaryMidiFileForDrumRow (row);
         if (!file.existsAsFile()) { dragStarted = false; return; }
         owner.performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
     }
     void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
     MidiForgeAudioProcessorEditor& owner;
     int row;
     juce::String label;
     bool dragStarted = false;
 };
 // Мини пиано-ролл: показывает текущий выбранный вариант и бегущую полоску
 // воспроизведения. Цвет ноты = канал (аккорды/бас/мелодия/арпеджио).
 struct PianoRoll : public juce::Component, private juce::Timer
 {
     PianoRoll (MidiForgeAudioProcessor& proc, juce::TextButton& undoButton, juce::TextButton& redoButton, juce::Label& historyText)
          : processor (proc), undoBtn (undoButton), redoBtn (redoButton), historyLabel (historyText)
     {
         setWantsKeyboardFocus (true);
         startTimerHz (30);
     }
     ~PianoRoll() override { stopTimer(); }

     void paint (juce::Graphics& g) override
     {
         g.setColour (juce::Colour (0xff10131a));
         g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

         const auto notes = processor.getVisibleNotes();
         const int bars = juce::jmax (1, processor.getVisibleBars());
         const int totalSteps = juce::jmax (16, bars * 16);
         const int minVisibleNote = viewLowNote;
         const int maxVisibleNote = juce::jmin (127, viewLowNote + noteSpan);
         const float pianoWidth = 34.0f;
         const float contentX = pianoWidth;
         const float contentW = juce::jmax (1.0f, (float) getWidth() - contentX);
         const int visibleSteps = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
         const float stepW = contentW / (float) visibleSteps;
         const int startStep = juce::jlimit (0, juce::jmax (0, totalSteps - visibleSteps), viewStartStep);

         // Piano keys / pitch labels.
         for (int pitch = minVisibleNote; pitch <= maxVisibleNote; ++pitch)
         {
             const float y = pitchToY (pitch, minVisibleNote, noteSpan);
             const bool black = isBlackKey (pitch);
             g.setColour (black ? juce::Colour (0xff252a31) : juce::Colour (0xffcfd3da));
             g.fillRect (0.0f, y, pianoWidth - 1.0f, pitchRowHeight (noteSpan));
             g.setColour (black ? juce::Colours::white.withAlpha (0.75f) : juce::Colours::black.withAlpha (0.7f));
             if (pitch % 12 == 0 || pitch == maxVisibleNote)
                 g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3),
                             1, (int) y, (int) pianoWidth - 2, juce::jmax (8, (int) pitchRowHeight (noteSpan)),
                             juce::Justification::centred, false);
         }

         // Horizontal grid.
         for (int s = startStep; s <= startStep + visibleSteps; ++s)
         {
             const float x = contentX + (float) (s - startStep) * stepW;
             const bool bar = (s % 16) == 0;
             const bool grid = (s % juce::jmax (1, gridSteps)) == 0;
             const bool beat = (s % 4) == 0;
             g.setColour (bar ? juce::Colour (0xff596170).withAlpha (0.9f)
                              : juce::Colour (0xff303640).withAlpha (grid ? 0.72f : 0.24f));
             g.fillRect (x, 0.0f, bar ? 1.5f : (grid ? 1.0f : 0.5f), (float) getHeight());
             if (bar)
                 g.drawText ("BAR " + juce::String (s / 16 + 1), (int) x + 3, 2, 60, 16,
                             juce::Justification::left, false);
         }

         // Vertical pitch grid.
         for (int pitch = minVisibleNote; pitch <= maxVisibleNote; ++pitch)
         {
             const float y = pitchToY (pitch, minVisibleNote, noteSpan);
             g.setColour (isBlackKey (pitch) ? juce::Colour (0xff191c22) : juce::Colour (0xff242933));
             g.fillRect (contentX, y + pitchRowHeight (noteSpan) - 0.5f, contentW, 1.0f);
         }

         static const juce::uint32 channelColours[6] =
             { 0xff888888, 0xff4a90d9, 0xffe0a23c, 0xff5cc78e, 0xffb279e0, 0xffd9645c };

         // Notes. The hovered/selected note gets a bright outline.
         for (int i = 0; i < (int) notes.size(); ++i)
         {
             const auto& n = notes[(size_t) i];
             if (n.channel == 5) continue;            // drums have their own section (DRUM VIEW)
             const float x = contentX + (float) (n.step - startStep) * stepW + 1.0f;
             const float noteW = juce::jmax (4.0f, (float) n.length * stepW - 2.0f);
             const float y = pitchToY (n.note, minVisibleNote, noteSpan) + 1.0f;
             if (x + noteW < contentX || x > contentX + contentW)
                 continue;

             const auto base = juce::Colour (channelColours[juce::jlimit (0, 5, n.channel)]);
             const float alpha = 0.55f + 0.45f * ((float) n.velocity / 127.0f);
             g.setColour (base.withAlpha (alpha));
             g.fillRoundedRectangle (x, y, noteW, juce::jmax (3.0f, pitchRowHeight (noteSpan) - 2.0f), 2.0f);

             if (isSelectedIndex (i))
             {
                 g.setColour (juce::Colours::white.withAlpha (0.92f));
                 g.drawRoundedRectangle (x, y, noteW, juce::jmax (3.0f, pitchRowHeight (noteSpan) - 2.0f), 2.0f,
                                         i == selectedNote ? 1.8f : 1.1f);
             }
         }
