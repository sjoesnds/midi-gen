#pragma once
#include <JuceHeader.h>
#include <memory>
#include <utility>
#include "PluginProcessor.h"
class MidiForgeAudioProcessorEditor : public juce::AudioProcessorEditor,
public juce::DragAndDropContainer,
private juce::Timer
{
public:
explicit MidiForgeAudioProcessorEditor(MidiForgeAudioProcessor&);
~MidiForgeAudioProcessorEditor() override { stopTimer(); }
void paint(juce::Graphics&) override;
void resized() override;
private:
void timerCallback() override;
void refreshTaste();
juce::String lastTasteText;
MidiForgeAudioProcessor& processor;
juce::Label title, sectionLabel;
 juce::ComboBox root, genre, scale, progression, rhythm, mode, bars, octave, arpRate, variationBox;
 juce::Slider chordDensity,bassDensity,melodyDensity,arpDensity;
 juce::Slider swing,humanize,complexity,motifStrength,variationAmount,fillAmount,energy;
 juce::Slider melodyLength,pauseChance,leapChance,ghostChance;
 juce::ToggleButton chords,bass,melody,arp,extensions,inversions,hookModeButton;
 juce::ToggleButton lockChordsBtn{"Lock Chords"}, lockBassBtn{"Lock Bass"}, lockMelodyBtn{"Lock Melody"}, lockArpBtn{"Lock Arp"};
 juce::TextButton generate,newSeed,applyVariation,exportMidi;
 // --- Learning: лайк/дизлайк текущей вариации + счётчик профиля вкуса ---
 juce::TextButton likeBtn, dislikeBtn;
 juce::Label tasteLabel;
 // --- Экспорт MIDI ---
 juce::TextButton exportButton { "Export MIDI..." };
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
         owner.performExternalDragDropOfFiles ({ file.getFullPathName() }, false);
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
 // Мини пиано-ролл: показывает текущий выбранный вариант и бегущую полоску
 // воспроизведения. Цвет ноты = канал (аккорды/бас/мелодия/арпеджио).
 struct PianoRoll : public juce::Component, private juce::Timer
 {
     explicit PianoRoll (MidiForgeAudioProcessor& proc) : processor (proc)
     {
         startTimerHz (30);
     }
     ~PianoRoll() override { stopTimer(); }
     void paint (juce::Graphics& g) override
     {
         g.setColour (juce::Colour (0xff15181f));
         g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
         const auto notes = processor.getVisibleNotes();
         const int bars = juce::jmax (1, processor.getVisibleBars());
         const int totalSteps = bars * 16;
         if (notes.empty()) return;
         const float w = (float) getWidth() / (float) totalSteps;
         int lowNote = 127, highNote = 0;
         for (const auto& n : notes)
         {
             lowNote = juce::jmin (lowNote, n.note);
             highNote = juce::jmax (highNote, n.note);
         }
         if (highNote <= lowNote) { lowNote -= 3; highNote += 3; }
         const int span = juce::jmax (1, highNote - lowNote);
         const float h = (float) getHeight() / (float) (span + 2);
         static const juce::uint32 channelColours[5] =
             { 0xff888888, 0xff4a90d9, 0xffe0a23c, 0xff5cc78e, 0xffb279e0 };
         for (const auto& n : notes)
         {
             const float x = (float) n.step * w;
             const float noteW = juce::jmax (1.5f, (float) n.length * w - 1.0f);
             const float y = (float) getHeight() - ((float) (n.note - lowNote) + 1.0f) * h;
             g.setColour (juce::Colour (channelColours[juce::jlimit (0, 4, n.channel)]));
             g.fillRoundedRectangle (x, y, noteW, juce::jmax (2.0f, h - 1.0f), 1.5f);
         }
         const int step = processor.getVisiblePlayheadStep();
         if (step >= 0 && step < totalSteps)
         {
             const float px = (float) step * w;
             g.setColour (juce::Colours::white.withAlpha (0.8f));
             g.fillRect (px, 0.0f, juce::jmax (1.5f, w * 0.3f), (float) getHeight());
         }
     }
 private:
     void timerCallback() override { repaint(); }
     MidiForgeAudioProcessor& processor;
 };
 PianoRoll pianoRoll { processor };
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiForgeAudioProcessorEditor)
};
