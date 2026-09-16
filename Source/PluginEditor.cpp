#include "PluginEditor.h"
static void setupSlider(juce::Slider& s,double mn,double mx,double step,double val,
juce::AudioProcessorEditor* owner)
{
s.setSliderStyle(juce::Slider::LinearHorizontal);
s.setRange(mn,mx,step);
s.setValue(val);
s.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,20);
owner->addAndMakeVisible(s);
}
void MidiForgeAudioProcessorEditor::DragHandle::paint (juce::Graphics& g)
{
g.setColour (juce::Colour (0xffe08a2e));
g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
g.setColour (juce::Colours::black);
g.setFont (13.0f);
g.drawFittedText ("Drag to FL Studio", getLocalBounds(), juce::Justification::centred, 1);
}
void MidiForgeAudioProcessorEditor::DragHandle::mouseDrag (const juce::MouseEvent& e)
{
// mouseDrag fires repeatedly (on every mouse-move during the gesture), not once.
// Starting a new native OS drag session on each of those calls stacked overlapping
// drag operations on top of each other — this is what made the drag look broken,
// and could leave an orphaned OS/COM drag handle alive that later blocked Windows
// from unloading the plugin DLL when it was removed from the mixer.
if (dragStarted)
return;
// Small threshold so a click doesn't immediately count as a drag.
if (e.getDistanceFromDragStart() < 6)
return;
dragStarted = true;
auto file = owner.processor.writeTemporaryMidiFile();
if (!file.existsAsFile())
{
dragStarted = false;
return;
}
owner.performExternalDragDropOfFiles ({ file.getFullPathName() }, false);
}
MidiForgeAudioProcessorEditor::MidiForgeAudioProcessorEditor(MidiForgeAudioProcessor& p)
: AudioProcessorEditor(&p),processor(p)
{
setSize(900,800);
setResizable(true, true);
setResizeLimits(760, 720, 1400, 1100);
title.setText("MIDI FORGE",juce::dontSendNotification);
// Старый конструктор Font: жив и на JUCE 7, и на JUCE 8 (в 8 — deprecated, но компилируется).
title.setFont (juce::Font (31.0f, juce::Font::bold));
sectionLabel.setText("COMPOSITION ENGINE",juce::dontSendNotification);
sectionLabel.setFont (juce::Font (12.0f));
addAndMakeVisible(title);addAndMakeVisible(sectionLabel);
root.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);
root.setSelectedId(p.getRoot()+1); root.onChange=[this]{processor.setRoot(root.getSelectedId()-1);}; addAndMakeVisible(root);
genre.addItemList({"Universal","Trap","House","Techno","Boom Bap","Ambient","Cinematic",
                    "R&B","Pop","Drill","DnB","Jersey","Afro","Hyperpop","Experimental","Lo-Fi"},1);
genre.setSelectedId(p.getGenre()+1); genre.onChange=[this]{processor.setGenre(genre.getSelectedId()-1);}; addAndMakeVisible(genre);
scale.addItemList({"Major","Minor","Dorian","Phrygian","Harmonic Minor","Melodic Minor","Pentatonic"},1);
scale.setSelectedId(p.getScale()+1); scale.onChange=[this]{processor.setScale(scale.getSelectedId()-1);}; addAndMakeVisible(scale);
progression.addItemList({"Auto","Pop","Dark","Emotional","Cinematic","Jazz-like","Looping"},1);
progression.setSelectedId(p.getProgression()+1); progression.onChange=[this]{processor.setProgression(progression.getSelectedId()-1);}; addAndMakeVisible(progression);
rhythm.addItemList({"Straight","Syncopated","Broken","Euclidean"},1);
rhythm.setSelectedId(p.getRhythm()+1); rhythm.onChange=[this]{processor.setRhythm(rhythm.getSelectedId()-1);}; addAndMakeVisible(rhythm);
moodBox.addItemList({"Neutral","Dark","Melancholic","Euphoric","Aggressive","Dreamy","Nostalgic","Mysterious","Energetic"},1);
moodBox.setSelectedId(p.getMood()+1); moodBox.onChange=[this]{processor.setMood(moodBox.getSelectedId()-1);}; addAndMakeVisible(moodBox);
melodyTypeBox.addItemList({"Hook","Vocal-like","Riff","Ostinato","Arp","Counter","Sparse Lead","Phrase"},1);
melodyTypeBox.setSelectedId(p.getMelodyType()+1); melodyTypeBox.onChange=[this]{processor.setMelodyType(melodyTypeBox.getSelectedId()-1);}; addAndMakeVisible(melodyTypeBox);
eraBox.addItemList({"70s","80s","90s","00s","10s","20s"},1);
eraBox.setSelectedId(p.getEra()+1); eraBox.onChange=[this]{processor.setEra(eraBox.getSelectedId()-1);}; addAndMakeVisible(eraBox);
mode.setVisible(false);
bars.addItemList({"1","2","4","8","16"},1);
const int bid=p.getBars()==1?1:p.getBars()==2?2:p.getBars()==4?3:p.getBars()==8?4:5;
bars.setSelectedId(bid);
bars.onChange=[this]{const int a[5]={1,2,4,8,16};processor.setBars(a[bars.getSelectedId()-1]);}; addAndMakeVisible(bars);
for(int i=2;i<=6;++i)octave.addItem(juce::String(i),i-1);
octave.setSelectedId(p.getOctave()-1); octave.onChange=[this]{processor.setOctave(octave.getSelectedId()+1);}; addAndMakeVisible(octave);
for(int i:{1,2,4,8})arpRate.addItem(juce::String(i)+"x",i);
arpRate.setSelectedId(p.getArpRate()); arpRate.onChange=[this]{processor.setArpRate(arpRate.getSelectedId());}; addAndMakeVisible(arpRate);
setupSlider(chordDensity,0,1,.01,p.getChordDensity(),this);
setupSlider(bassDensity,0,1,.01,p.getBassDensity(),this);
setupSlider(melodyDensity,0,1,.01,p.getMelodyDensity(),this);
setupSlider(arpDensity,0,1,.01,p.getArpDensity(),this);
setupSlider(motifStrength,0,1,.01,p.getMotifStrength(),this);
setupSlider(variationAmount,0,1,.01,p.getVariationAmount(),this);
setupSlider(fillAmount,0,1,.01,p.getFillAmount(),this);
setupSlider(energy,0,1,.01,p.getEnergy(),this);
setupSlider(melodyLength,0,1,.01,p.getMelodyLength(),this);
setupSlider(pauseChance,0,.7,.01,p.getPauseChance(),this);
setupSlider(leapChance,0,.7,.01,p.getLeapChance(),this);
setupSlider(ghostChance,0,.5,.01,p.getGhostChance(),this);
setupSlider(swing,0,.75,.01,p.getSwing(),this);
setupSlider(humanize,0,1,.01,p.getHumanize(),this);
setupSlider(complexity,0,1,.01,p.getComplexity(),this);
chordDensity.onValueChange=[this]{processor.setChordDensity((float)chordDensity.getValue());};
bassDensity.onValueChange=[this]{processor.setBassDensity((float)bassDensity.getValue());};
melodyDensity.onValueChange=[this]{processor.setMelodyDensity((float)melodyDensity.getValue());};
arpDensity.onValueChange=[this]{processor.setArpDensity((float)arpDensity.getValue());};
motifStrength.onValueChange=[this]{processor.setMotifStrength((float)motifStrength.getValue());};
variationAmount.onValueChange=[this]{processor.setVariationAmount((float)variationAmount.getValue());};
fillAmount.onValueChange=[this]{processor.setFillAmount((float)fillAmount.getValue());};
energy.onValueChange=[this]{processor.setEnergy((float)energy.getValue());};
melodyLength.onValueChange=[this]{processor.setMelodyLength((float)melodyLength.getValue());};
pauseChance.onValueChange=[this]{processor.setPauseChance((float)pauseChance.getValue());};
leapChance.onValueChange=[this]{processor.setLeapChance((float)leapChance.getValue());};
ghostChance.onValueChange=[this]{processor.setGhostChance((float)ghostChance.getValue());};
swing.onValueChange=[this]{processor.setSwing((float)swing.getValue());};
humanize.onValueChange=[this]{processor.setHumanize((float)humanize.getValue());};
complexity.onValueChange=[this]{processor.setComplexity((float)complexity.getValue());};
chords.setButtonText("CHORDS");bass.setButtonText("BASS");melody.setButtonText("MELODY");arp.setButtonText("ARP");
extensions.setButtonText("7/9 EXT");inversions.setButtonText("INV");hookModeButton.setButtonText("HOOK");
soundCloudButton.setButtonText("SOUNDCLOUD");
chords.setToggleState(p.isChordsEnabled(),juce::dontSendNotification);
bass.setToggleState(p.isBassEnabled(),juce::dontSendNotification);
melody.setToggleState(p.isMelodyEnabled(),juce::dontSendNotification);
arp.setToggleState(p.isArpEnabled(),juce::dontSendNotification);
extensions.setToggleState(p.getChordExtensions(),juce::dontSendNotification);
inversions.setToggleState(p.getInversions(),juce::dontSendNotification);
hookModeButton.setToggleState(p.getHookMode(),juce::dontSendNotification);
soundCloudButton.setToggleState(p.getLeadStyleSoundCloud(),juce::dontSendNotification);
chords.onClick=[this]{processor.setChordsEnabled(chords.getToggleState());};
bass.onClick=[this]{processor.setBassEnabled(bass.getToggleState());};
melody.onClick=[this]{processor.setMelodyEnabled(melody.getToggleState());};
arp.onClick=[this]{processor.setArpEnabled(arp.getToggleState());};
extensions.onClick=[this]{processor.setChordExtensions(extensions.getToggleState());};
inversions.onClick=[this]{processor.setInversions(inversions.getToggleState());};
hookModeButton.onClick=[this]{processor.setHookMode(hookModeButton.getToggleState());};
soundCloudButton.onClick=[this]{processor.setLeadStyleSoundCloud(soundCloudButton.getToggleState());};
addAndMakeVisible(chords);addAndMakeVisible(bass);addAndMakeVisible(melody);addAndMakeVisible(arp);
addAndMakeVisible(extensions);addAndMakeVisible(inversions);addAndMakeVisible(hookModeButton);
addAndMakeVisible(soundCloudButton);
variationBox.addItemList({"VAR 1","VAR 2","VAR 3","VAR 4","VAR 5","VAR 6","VAR 7","VAR 8"},1);
variationBox.setSelectedId(1); addAndMakeVisible(variationBox);
variationBox.onChange=[this]{ processor.chooseVariation(variationBox.getSelectedId()-1); pianoRoll.resetEditHistory(); };
generate.setButtonText("MAGIC");
newSeed.setButtonText("NEW SEED");
applyVariation.setButtonText("USE VAR");
exportMidi.setButtonText("EXPORT .MID");
generate.onClick=[this]{ processor.magicRandomize(); pianoRoll.resetEditHistory(); };
newSeed.onClick=[this]{ processor.rerollSameDNA(); pianoRoll.resetEditHistory(); };
applyVariation.onClick=[this]{ processor.chooseVariation(variationBox.getSelectedId()-1); pianoRoll.resetEditHistory(); };
exportMidi.onClick=[this]{
fileChooser = std::make_unique<juce::FileChooser>(
"Export MIDI",
juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
.getChildFile("MIDI_Forge_Song.mid"),
"*.mid");

auto safeThis = juce::Component::SafePointer<MidiForgeAudioProcessorEditor>(this);
fileChooser->launchAsync(
juce::FileBrowserComponent::saveMode |
juce::FileBrowserComponent::canSelectFiles |
juce::FileBrowserComponent::warnAboutOverwriting,
[safeThis](const juce::FileChooser& chooser)
{
if (safeThis == nullptr)
return;
const auto result = chooser.getResult();
if (result == juce::File{})
return;
const bool ok = safeThis->processor.exportMidi(result);
juce::AlertWindow::showMessageBoxAsync(
ok ? juce::MessageBoxIconType::InfoIcon
: juce::MessageBoxIconType::WarningIcon,
ok ? "MIDI Forge" : "MIDI Export Failed",
ok ? "MIDI exported successfully."
: "Could not write the MIDI file.",
"OK");
});
};
mutateButton.onClick=[this]{ processor.mutateSelected(0.45f); pianoRoll.resetEditHistory(); };
evolveButton.onClick=[this]{ processor.evolveSelected(); pianoRoll.resetEditHistory(); };
addAndMakeVisible(generate);addAndMakeVisible(newSeed);addAndMakeVisible(applyVariation);addAndMakeVisible(exportMidi);
addAndMakeVisible(mutateButton); addAndMakeVisible(evolveButton);

// --- P2: Undo / Redo / Clear ------------------------------------------
undoBtn.onClick = [this] { pianoRoll.undo(); };
redoBtn.onClick = [this] { pianoRoll.redo(); };
clearBtn.onClick = [this] { pianoRoll.clearAllNotes(); };
historyLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
historyLabel.setFont (juce::Font (11.0f));
historyLabel.setJustificationType (juce::Justification::centredLeft);
addAndMakeVisible (undoBtn);
addAndMakeVisible (redoBtn);
addAndMakeVisible (clearBtn);
addAndMakeVisible (historyLabel);

// --- Learning: лайк/дизлайк текущей вариации ---
likeBtn.setButtonText("LIKE");
dislikeBtn.setButtonText("DISLIKE");
likeBtn.onClick=[this]{
processor.likeVariation(processor.getSelectedVariation());
refreshTaste();
};
dislikeBtn.onClick=[this]{
processor.dislikeVariation(processor.getSelectedVariation());
refreshTaste();
};
addAndMakeVisible(likeBtn);addAndMakeVisible(dislikeBtn);
tasteLabel.setText("TASTE: 0 like / 0 dislike",juce::dontSendNotification);
tasteLabel.setColour(juce::Label::textColourId,juce::Colours::white.withAlpha(0.8f));
tasteLabel.setFont(juce::Font(12.0f));
addAndMakeVisible(tasteLabel);
// --- Экспорт MIDI ---
exportButton.onClick = [this]
{
fileChooser = std::make_unique<juce::FileChooser> ("Save MIDI file", juce::File(), "*.mid");
auto safeThis = juce::Component::SafePointer<MidiForgeAudioProcessorEditor>(this);
fileChooser->launchAsync (
juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
[safeThis] (const juce::FileChooser& fc)
{
if (safeThis == nullptr) return;
auto file = fc.getResult();
if (file != juce::File())
safeThis->processor.exportMidiFileTo (file.withFileExtension ("mid"));
});
};
addAndMakeVisible (exportButton);
addAndMakeVisible (dragHandle);
addAndMakeVisible (pianoRoll);
// --- Smart Lock: заморозка партии при GENERATE 8 ---
lockChordsBtn.setToggleState(p.getLockChords(), juce::dontSendNotification);
lockBassBtn.setToggleState(p.getLockBass(), juce::dontSendNotification);
lockMelodyBtn.setToggleState(p.getLockMelody(), juce::dontSendNotification);
lockArpBtn.setToggleState(p.getLockArp(), juce::dontSendNotification);
lockChordsBtn.onClick=[this]{processor.setLockChords(lockChordsBtn.getToggleState());};
lockBassBtn.onClick=[this]{processor.setLockBass(lockBassBtn.getToggleState());};
lockMelodyBtn.onClick=[this]{processor.setLockMelody(lockMelodyBtn.getToggleState());};
lockArpBtn.onClick=[this]{processor.setLockArp(lockArpBtn.getToggleState());};
addAndMakeVisible(lockChordsBtn);addAndMakeVisible(lockBassBtn);
addAndMakeVisible(lockMelodyBtn);addAndMakeVisible(lockArpBtn);
// --- Раздельный drag-and-drop по партиям ---
addAndMakeVisible(dragChords);addAndMakeVisible(dragBass);
addAndMakeVisible(dragMelody);addAndMakeVisible(dragArp);
refreshTaste();
startTimerHz (10);
}
void MidiForgeAudioProcessorEditor::refreshTaste()
{
const int sel = processor.getSelectedVariation();
juce::String t = "TASTE: " + juce::String(processor.getTasteLikes()) + " like / "
+ juce::String(processor.getTasteDislikes()) + " dislike   |   VAR "
+ juce::String(sel + 1) + " score: "
+ juce::String(processor.getVariationScore(sel));
if (t != lastTasteText)
{
lastTasteText = t;
tasteLabel.setText(t, juce::dontSendNotification);
}
}
void MidiForgeAudioProcessorEditor::paint(juce::Graphics& g)
{
g.fillAll(juce::Colour(0xff0d1015));
g.setColour(juce::Colour(0xff252a33));
g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.f),12.f,1.f);
g.setColour(juce::Colours::white);
g.setFont(12.f);
g.drawFittedText("SOURCE / HARMONY",20,82,860,18,juce::Justification::left,1);
g.drawFittedText("PART DENSITY / MUSICAL DNA",20,146,860,18,juce::Justification::left,1);
g.drawFittedText("MOTIF / ARRANGEMENT",20,234,860,18,juce::Justification::left,1);
g.drawFittedText("MELODY",20,322,860,18,juce::Justification::left,1);
g.drawFittedText("FEEL",20,410,860,18,juce::Justification::left,1);
g.drawFittedText("PIANO ROLL",20,452,860,18,juce::Justification::left,1);
g.drawFittedText("VARIATIONS / LEARNING",20,668,860,18,juce::Justification::left,1);
}
void MidiForgeAudioProcessorEditor::resized()
{
const int W = getWidth();
const int left = 20;
const int contentW = W - 40;

// Header
title.setBounds(left,14,360,32);
sectionLabel.setBounds(23,48,500,18);

// Source / harmony
const int gap = 7;
int x = left;
root.setBounds(x,82,68,26); x += 68 + gap;
genre.setBounds(x,82,106,26); x += 106 + gap;
scale.setBounds(x,82,118,26); x += 118 + gap;
progression.setBounds(x,82,112,26); x += 112 + gap;
rhythm.setBounds(x,82,104,26); x += 104 + gap;
mode.setBounds(x,82,90,26); x += 90 + gap;
bars.setBounds(x,82,48,26); x += 48 + gap;
octave.setBounds(x,82,46,26);

chords.setBounds(20,112,82,22);
bass.setBounds(106,112,76,22);
melody.setBounds(186,112,90,22);
arp.setBounds(280,112,70,22);
extensions.setBounds(354,112,82,22);
inversions.setBounds(440,112,68,22);
arpRate.setBounds(512,112,58,22);
hookModeButton.setBounds(574,112,72,22);
soundCloudButton.setBounds(650,112,110,22);
moodBox.setBounds(20,136,106,24);
melodyTypeBox.setBounds(133,136,112,24);
eraBox.setBounds(252,136,70,24);

// Two-column compact control layout.
const int sliderH = 21;
const int colW = juce::jmax(240, (contentW - 16) / 2);
const int x1 = left;
const int x2 = left + colW + 16;

chordDensity.setBounds(x1,164,colW,sliderH);
bassDensity.setBounds(x2,164,colW,sliderH);
melodyDensity.setBounds(x1,190,colW,sliderH);
arpDensity.setBounds(x2,190,colW,sliderH);

motifStrength.setBounds(x1,252,colW,sliderH);
variationAmount.setBounds(x2,252,colW,sliderH);
fillAmount.setBounds(x1,278,colW,sliderH);
energy.setBounds(x2,278,colW,sliderH);

melodyLength.setBounds(x1,340,colW,sliderH);
pauseChance.setBounds(x2,340,colW,sliderH);
leapChance.setBounds(x1,366,colW,sliderH);
ghostChance.setBounds(x2,366,colW,sliderH);

swing.setBounds(x1,428,(contentW - 16) / 3,21);
humanize.setBounds(x1 + (contentW - 16) / 3 + 8,428,(contentW - 16) / 3,21);
complexity.setBounds(x1 + 2 * ((contentW - 16) / 3) + 16,428,(contentW - 16) / 3,21);

// Piano roll is kept large enough to remain usable, but no longer pushes the
// action controls below the host window.
pianoRoll.setBounds(left,472,contentW,190);

// Bottom action rows — always visible in the default 800px editor.
variationBox.setBounds(20,690,98,28);
generate.setBounds(126,687,102,32);
newSeed.setBounds(236,687,98,32);
applyVariation.setBounds(342,687,92,32);
exportMidi.setBounds(442,687,118,32);
likeBtn.setBounds(568,687,70,32);
dislikeBtn.setBounds(644,687,82,32);
mutateButton.setBounds(732,687,76,32);
evolveButton.setBounds(814,687,76,32);
tasteLabel.setBounds(530,764,350,28);

exportButton.setBounds(20,733,122,28);
undoBtn.setBounds(150,733,66,28);
redoBtn.setBounds(224,733,66,28);
clearBtn.setBounds(298,733,66,28);
historyLabel.setBounds(372,733,136,28);
lockChordsBtn.setBounds(516,733,82,24);
lockBassBtn.setBounds(604,733,82,24);
lockMelodyBtn.setBounds(692,733,90,24);
lockArpBtn.setBounds(788,733,92,24);

dragHandle.setBounds(620,763,132,28);

dragChords.setBounds(20,764,118,28);
dragBass.setBounds(146,764,118,28);
dragMelody.setBounds(272,764,118,28);
dragArp.setBounds(398,764,118,28);
tasteLabel.setBounds(530,764,84,28);
}
void MidiForgeAudioProcessorEditor::timerCallback()
{
const int id = processor.getSelectedVariation() + 1;
if (id >= 1 && variationBox.getSelectedId() != id)
variationBox.setSelectedId (id, juce::dontSendNotification);
if (moodBox.getSelectedId() != processor.getMood()+1) moodBox.setSelectedId(processor.getMood()+1, juce::dontSendNotification);
if (melodyTypeBox.getSelectedId() != processor.getMelodyType()+1) melodyTypeBox.setSelectedId(processor.getMelodyType()+1, juce::dontSendNotification);
if (eraBox.getSelectedId() != processor.getEra()+1) eraBox.setSelectedId(processor.getEra()+1, juce::dontSendNotification);
refreshTaste();
}
