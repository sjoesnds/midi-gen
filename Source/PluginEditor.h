#pragma once
#include <JuceHeader.h>
#include <memory>
#include <utility>
#include <optional>
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
 juce::ToggleButton chords,bass,melody,arp,extensions,inversions,hookModeButton,soundCloudButton;
 juce::ToggleButton lockChordsBtn{"Lock Chords"}, lockBassBtn{"Lock Bass"}, lockMelodyBtn{"Lock Melody"}, lockArpBtn{"Lock Arp"};
 juce::TextButton generate,newSeed,applyVariation,exportMidi;
 // --- Learning: лайк/дизлайк текущей вариации + счётчик профиля вкуса ---
 juce::TextButton likeBtn, dislikeBtn;
 juce::Label tasteLabel;
 // --- Экспорт MIDI ---
 juce::TextButton exportButton { "Export MIDI..." };
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
             const bool half = (s % 4) == 0;
             g.setColour (bar ? juce::Colour (0xff596170).withAlpha (0.9f)
                              : juce::Colour (0xff303640).withAlpha (half ? 0.8f : 0.42f));
             g.fillRect (x, 0.0f, bar ? 1.5f : 1.0f, (float) getHeight());
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

         static const juce::uint32 channelColours[5] =
             { 0xff888888, 0xff4a90d9, 0xffe0a23c, 0xff5cc78e, 0xffb279e0 };

         // Notes. The hovered/selected note gets a bright outline.
         for (int i = 0; i < (int) notes.size(); ++i)
         {
             const auto& n = notes[(size_t) i];
             const float x = contentX + (float) (n.step - startStep) * stepW + 1.0f;
             const float noteW = juce::jmax (4.0f, (float) n.length * stepW - 2.0f);
             const float y = pitchToY (n.note, minVisibleNote, noteSpan) + 1.0f;
             if (x + noteW < contentX || x > contentX + contentW)
                 continue;

             const auto base = juce::Colour (channelColours[juce::jlimit (0, 4, n.channel)]);
             const float alpha = 0.55f + 0.45f * ((float) n.velocity / 127.0f);
             g.setColour (base.withAlpha (alpha));
             g.fillRoundedRectangle (x, y, noteW, juce::jmax (3.0f, pitchRowHeight (noteSpan) - 2.0f), 2.0f);

             if (i == selectedNote)
             {
                 g.setColour (juce::Colours::white.withAlpha (0.92f));
                 g.drawRoundedRectangle (x, y, noteW, juce::jmax (3.0f, pitchRowHeight (noteSpan) - 2.0f), 2.0f, 1.2f);
             }
         }

         const int playStep = processor.getVisiblePlayheadStep();
         if (playStep >= startStep && playStep < startStep + visibleSteps)
         {
             const float px = contentX + (float) (playStep - startStep) * stepW;
             g.setColour (juce::Colours::white.withAlpha (0.78f));
             g.fillRect (px, 0.0f, juce::jmax (1.5f, stepW * 0.25f), (float) getHeight());
         }

         g.setColour (juce::Colours::white.withAlpha (0.65f));
         g.setFont (11.0f);
         const juce::String help = "LMB add/move   drag right edge = length   RMB delete   1-4 layer   Ctrl+C/V   Ctrl+Z/Y   wheel=scroll   Ctrl+wheel=zoom";
         g.drawFittedText (help, (int) pianoWidth + 6, getHeight() - 18, getWidth() - (int) pianoWidth - 12, 16,
                           juce::Justification::centredLeft, 1);

         const juce::String layer = "ADD: " + channelName (selectedChannel);
         g.setColour (juce::Colours::black.withAlpha (0.65f));
         g.fillRoundedRectangle (contentX + 6.0f, 6.0f, 100.0f, 20.0f, 5.0f);
         g.setColour (juce::Colours::white.withAlpha (0.9f));
         g.drawText (layer, (int) contentX + 10, 8, 92, 16, juce::Justification::centred, false);
     }

     void mouseDown (const juce::MouseEvent& e) override
     {
         grabKeyboardFocus();
         if (e.mods.isRightButtonDown())
         {
             const int hit = hitTestNote (e.position);
             if (hit >= 0)
             {
                 beginEditHistory();
                 processor.deleteVisibleNote (hit);
                 commitEditHistory();
                 selectedNote = -1;
                 repaint();
             }
             return;
         }

         if (! e.mods.isLeftButtonDown())
             return;

         const int hit = hitTestNote (e.position);
         beginEditHistory();
         if (hit >= 0)
         {
             selectedNote = hit;
             const auto notes = processor.getVisibleNotes();
             if (hit < (int) notes.size())
             {
                 dragStartNote = notes[(size_t) hit];
                 dragMode = isResizeHit (e.position, dragStartNote) ? DragMode::resize : DragMode::move;
             }
         }
         else
         {
             const int step = snapStep (xToStep (e.position.x));
             const int note = yToPitch (e.position.y);
                 processor.addVisibleNote (step, note, defaultLengthSteps, 100, selectedChannel);
             selectedNote = (int) processor.getVisibleNotes().size() - 1;
             dragMode = DragMode::newNote;
             dragStartNote = { step, defaultLengthSteps, note, 100, selectedChannel };
         }
         dragOrigin = e.position;
         repaint();
     }

     void mouseDrag (const juce::MouseEvent& e) override
     {
         if (selectedNote < 0)
             return;
         const auto notes = processor.getVisibleNotes();
         if (selectedNote >= (int) notes.size())
             return;

         const auto& current = notes[(size_t) selectedNote];
         int step = current.step;
         int note = current.note;
         int length = current.length;
         int velocity = current.velocity;

         const float dx = e.position.x - dragOrigin.x;
         const float dy = e.position.y - dragOrigin.y;
         const int pitchDelta = juce::roundToInt (-dy / pitchRowHeight (noteSpan));

         if (dragMode == DragMode::resize)
         {
             length = juce::jmax (1, snapStep (juce::jmax (1.0f, dragStartNote.length + dx / stepWidthAtCursor())));
         }
         else if (e.mods.isAltDown())
         {
             velocity = juce::jlimit (1, 127, dragStartNote.velocity + juce::roundToInt (-dy));
         }
         else
         {
             step = snapStep (dragStartNote.step + dx / juce::jmax (1.0f, stepWidthAtCursor()));
             note = juce::jlimit (0, 127, dragStartNote.note + pitchDelta);
         }

         step = juce::jmax (0, step);
         const int maxStep = juce::jmax (0, processor.getVisibleBars() * 16 - length);
         step = juce::jlimit (0, maxStep, step);
         processor.editVisibleNote (selectedNote, step, note, length, velocity);
         repaint();
     }

     void mouseUp (const juce::MouseEvent&) override
     {
         if (dragMode != DragMode::none)
             commitEditHistory();
         dragMode = DragMode::none;
         repaint();
     }

     bool keyPressed (const juce::KeyPress& key) override
     {
         if (key.getKeyCode() >= '1' && key.getKeyCode() <= '4')
         {
             selectedChannel = key.getKeyCode() - '0';
             repaint();
             return true;
         }

         if (key == juce::KeyPress ('c', juce::ModifierKeys::ctrlModifier, 0))
         {
             copySelection();
             return true;
         }
         if (key == juce::KeyPress ('v', juce::ModifierKeys::ctrlModifier, 0))
         {
             pasteSelection();
             return true;
         }
         if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
         {
             undo();
             return true;
         }
         if (key == juce::KeyPress ('y', juce::ModifierKeys::ctrlModifier, 0)
             || key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
         {
             redo();
             return true;
         }
         if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
         {
             if (selectedNote >= 0)
             {
                 beginEditHistory();
                 processor.deleteVisibleNote (selectedNote);
                 commitEditHistory();
                 selectedNote = -1;
                 repaint();
             }
             return true;
         }
         if (key.getTextCharacter() == 'q' || key.getTextCharacter() == 'Q')
         {
             beginEditHistory();
             processor.quantizeVisibleNotes (2);
             commitEditHistory();
             repaint();
             return true;
         }
         return false;
     }

     void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
     {
         const float wheelY = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
         if (juce::ModifierKeys::getCurrentModifiers().isCtrlDown())
         {
             const float centerRatio = juce::jlimit (0.0f, 1.0f, lastMouseX / (float) juce::jmax (1, getWidth()));
             const float oldZoom = zoom;
             zoom = juce::jlimit (0.5f, 4.0f, zoom + wheelY * 0.35f);
             if (oldZoom != zoom)
             {
                 const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
                 const int oldVisible = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / oldZoom));
                 const int newVisible = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
                 const int cursorStep = viewStartStep + juce::roundToInt (centerRatio * (float) oldVisible);
                 viewStartStep = juce::jlimit (0, juce::jmax (0, totalSteps - newVisible), cursorStep - juce::roundToInt (centerRatio * (float) newVisible));
             }
         }
         else if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
         {
             const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
             const int visible = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
             viewStartStep += wheelY > 0 ? -8 : 8;
             viewStartStep = juce::jlimit (0, juce::jmax (0, totalSteps - visible), viewStartStep);
         }
         else
         {
             viewLowNote = juce::jlimit (0, 127 - noteSpan, viewLowNote + (wheelY > 0 ? 4 : -4));
         }
         repaint();
     }

 private:
     enum class DragMode { none, move, resize, newNote };

     static bool isBlackKey (int midiNote)
     {
         switch (midiNote % 12)
         {
             case 1: case 3: case 6: case 8: case 10: return true;
             default: return false;
         }
     }

     static juce::String channelName (int channel)
     {
         static const char* names[5] = { "", "CHORDS", "BASS", "MELODY", "ARP" };
         return names[juce::jlimit (1, 4, channel)];
     }

     float pitchRowHeight (int span) const
     {
         return juce::jmax (2.0f, ((float) getHeight() - 22.0f) / (float) (span + 1));
     }

     float pitchToY (int pitch, int low, int span) const
     {
         return (float) getHeight() - 22.0f - ((float) (pitch - low + 1)) * pitchRowHeight (span);
     }

     int yToPitch (float y) const
     {
         const float bodyH = (float) getHeight() - 22.0f;
         const float units = pitchRowHeight (noteSpan);
         const int pitch = viewLowNote + juce::roundToInt ((bodyH - y) / units) - 1;
         return juce::jlimit (0, 127, pitch);
     }

     int xToStep (float x) const
     {
         const float contentX = 34.0f;
         const float contentW = juce::jmax (1.0f, (float) getWidth() - contentX);
         const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
         const int visibleSteps = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
         const float stepW = contentW / (float) visibleSteps;
         return viewStartStep + juce::roundToInt ((x - contentX) / juce::jmax (1.0f, stepW));
     }

     int snapStep (float step) const
     {
         const int value = juce::jmax (0, juce::roundToInt (step));
         return value; // One grid unit = one 1/16 step.
     }

     int hitTestNote (juce::Point<float> p) const
     {
         const auto notes = processor.getVisibleNotes();
         const float contentX = 34.0f;
         const float contentW = juce::jmax (1.0f, (float) getWidth() - contentX);
         const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
         const int visibleSteps = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
         const float stepW = contentW / (float) visibleSteps;
         const int pitch = yToPitch (p.y);
         for (int i = (int) notes.size() - 1; i >= 0; --i)
         {
             const auto& n = notes[(size_t) i];
             const float x = contentX + (float) (n.step - viewStartStep) * stepW;
             const float w = juce::jmax (4.0f, (float) n.length * stepW - 2.0f);
             const float y = pitchToY (n.note, viewLowNote, noteSpan);
             if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + pitchRowHeight (noteSpan))
                 return i;
         }
         return -1;
     }

     bool isResizeHit (juce::Point<float> p, const MidiForgeAudioProcessor::VisibleNote& n) const
     {
         const float contentX = 34.0f;
         const float contentW = juce::jmax (1.0f, (float) getWidth() - contentX);
         const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
         const int visibleSteps = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
         const float stepW = contentW / (float) visibleSteps;
         const float right = contentX + (float) (n.step + n.length - viewStartStep) * stepW;
         return p.x >= right - 7.0f && p.x <= right + 3.0f;
     }

     float stepWidthAtCursor() const
     {
         const float contentW = juce::jmax (1.0f, (float) getWidth() - 34.0f);
         const int totalSteps = juce::jmax (16, processor.getVisibleBars() * 16);
         const int visibleSteps = juce::jlimit (1, totalSteps, juce::roundToInt ((float) totalSteps / zoom));
         return contentW / (float) visibleSteps;
     }

     static bool sameNotes (const std::vector<MidiForgeAudioProcessor::VisibleNote>& a,
                            const std::vector<MidiForgeAudioProcessor::VisibleNote>& b)
     {
         if (a.size() != b.size()) return false;
         for (size_t i = 0; i < a.size(); ++i)
         {
             if (a[i].step != b[i].step || a[i].length != b[i].length ||
                 a[i].note != b[i].note || a[i].velocity != b[i].velocity ||
                 a[i].channel != b[i].channel) return false;
         }
         return true;
     }

     void resetEditHistory()
     {
         history.clear();
         historyCursor = -1;
         updateHistoryButtons();
     }

     void updateHistoryButtons()
     {
         const bool canUndo = historyCursor > 0 && historyCursor < (int) history.size();
         const bool canRedo = historyCursor >= 0 && historyCursor + 1 < (int) history.size();
         undoBtn.setEnabled (canUndo);
         redoBtn.setEnabled (canRedo);
         historyLabel.setText ("EDIT: " + juce::String (canUndo ? historyCursor : 0) + " undo / "
                               + juce::String (canRedo ? (int) history.size() - historyCursor - 1 : 0) + " redo",
                               juce::dontSendNotification);
     }

     void beginEditHistory()
     {
         if (history.empty())
         {
             history.push_back (processor.getVisibleNotes());
             historyCursor = 0;
         }
         else if (historyCursor < (int) history.size() - 1)
         {
             history.erase (history.begin() + historyCursor + 1, history.end());
         }
         updateHistoryButtons();
     }

     void commitEditHistory()
     {
         const auto snapshot = processor.getVisibleNotes();
         if (history.empty())
         {
             history.push_back (snapshot);
             historyCursor = 0;
             return;
         }
         if (! sameNotes (history.back(), snapshot))
         {
             history.push_back (snapshot);
             if (history.size() > 64)
             {
                 history.erase (history.begin());
             }
             historyCursor = (int) history.size() - 1;
         }
         updateHistoryButtons();
     }

     void undo()
     {
         if (history.empty())
             history.push_back (processor.getVisibleNotes());
         if (historyCursor <= 0)
             return;
         --historyCursor;
         processor.replaceVisibleNotes (history[(size_t) historyCursor]);
         selectedNote = -1;
         updateHistoryButtons();
         repaint();
     }

     void redo()
     {
         if (historyCursor + 1 >= (int) history.size())
             return;
         ++historyCursor;
         processor.replaceVisibleNotes (history[(size_t) historyCursor]);
         selectedNote = -1;
         updateHistoryButtons();
         repaint();
     }

     void clearAllNotes()
     {
         const auto current = processor.getVisibleNotes();
         if (current.empty())
             return;
         beginEditHistory();
         processor.replaceVisibleNotes (std::vector<MidiForgeAudioProcessor::VisibleNote>{});
         commitEditHistory();
         selectedNote = -1;
         repaint();
     }

     void copySelection()
     {
         if (selectedNote < 0)
             return;
         const auto notes = processor.getVisibleNotes();
         if (selectedNote < (int) notes.size())
             clipboard = notes[(size_t) selectedNote];
     }

     void pasteSelection()
     {
         if (! clipboard.has_value())
             return;
         beginEditHistory();
         auto n = *clipboard;
         n.step = juce::jlimit (0, juce::jmax (0, processor.getVisibleBars() * 16 - n.length), n.step + 1);
         processor.addVisibleNote (n.step, n.note, n.length, n.velocity, n.channel);
         commitEditHistory();
         selectedNote = (int) processor.getVisibleNotes().size() - 1;
         repaint();
     }

     void mouseMove (const juce::MouseEvent& e) override
     {
         lastMouseX = e.position.x;
     }

     MidiForgeAudioProcessor& processor;
     int selectedNote = -1;
     int selectedChannel = 3;
     int defaultLengthSteps = 2;
     int viewStartStep = 0;
     int viewLowNote = 36;
     float zoom = 1.0f;
     float lastMouseX = 450.0f;
     const int noteSpan = 60;
     DragMode dragMode = DragMode::none;
     juce::Point<float> dragOrigin;
     MidiForgeAudioProcessor::VisibleNote dragStartNote { 0, 2, 60, 100, 3 };
     std::optional<MidiForgeAudioProcessor::VisibleNote> clipboard;
     std::vector<std::vector<MidiForgeAudioProcessor::VisibleNote>> history;
     int historyCursor = -1;

     void timerCallback() override { repaint(); }
 };
 PianoRoll pianoRoll { processor };
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiForgeAudioProcessorEditor)
};
