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
    // Only one native OLE drag may be started for a gesture.
    if (dragStarted)
        return;

    if (e.getDistanceFromDragStart() < 6)
        return;

    const auto now = juce::Time::getCurrentTime();
    if (lastDragStart.toMilliseconds() != 0
        && (now - lastDragStart).inMilliseconds() < 250)
        return;

    dragStarted = true;
    lastDragStart = now;

    // Предыдущий drag уже завершён (OLE-сессия синхронная) — старый временный файл можно убрать.
    if (activeDragFile != juce::File())
        activeDragFile.deleteFile();

    activeDragFile = owner.processor.writeTemporaryMidiFile();
    if (!activeDragFile.existsAsFile())
    {
        dragStarted = false;
        activeDragFile = juce::File();
        return;
    }

    // The actual button is the OLE source. Using the editor itself can make the
    // VST3 wrapper become the source window inside FL Studio.
    owner.performExternalDragDropOfFiles ({ activeDragFile.getFullPathName() },
                                          false,
                                          this,
                                          [this]
                                          {
                                              dragStarted = false;
                                          });

    // JUCE's native drag call is synchronous on Windows, so this runs only after
    // the OLE session has ended. The file is intentionally kept alive until the
    // next drag/editor lifetime instead of being deleted during the drag.
    dragStarted = false;
}
MidiForgeAudioProcessorEditor::MidiForgeAudioProcessorEditor(MidiForgeAudioProcessor& p)
: AudioProcessorEditor(&p),processor(p)
{
setSize(900,800);
setResizable(true, true);
setResizeLimits(980, 720, 1400, 1100);
title.setText("MIDI FORGE",juce::dontSendNotification);
versionLabel.setText("v0.53.0", juce::dontSendNotification);
versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
versionLabel.setFont(juce::Font(11.0f));
addAndMakeVisible(versionLabel);
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
soundBox.addItemList({"Sound: Piano","Sound: Pluck","Sound: Synth Lead","Sound: Bell / Mallet","Sound: Pad / Strings","Sound: Brass","Sound: 808 (one bass line)","Sound: Guitar"},1);
soundBox.setSelectedId(p.getSoundTarget()+1); soundBox.onChange=[this]{processor.setSoundTarget(soundBox.getSelectedId()-1);}; addAndMakeVisible(soundBox);
articBox.addItemList({"Artic: Off","Artic: Slides","Artic: Slides + Vibrato"},1);
articBox.setSelectedId(p.getArticulation()+1); articBox.onChange=[this]{processor.setArticulation(articBox.getSelectedId()-1);}; addAndMakeVisible(articBox);
chordBox.addItemList({"Chords: Auto","Chords: Held","Chords: Comping"},1);
chordBox.setSelectedId(p.getChordStyle()+1); chordBox.onChange=[this]{processor.setChordStyle(chordBox.getSelectedId()-1);}; addAndMakeVisible(chordBox);
autoNextBtn.setButtonText("AUTO-NEXT");
autoNextBtn.setToggleState(p.getAutoNext(), juce::dontSendNotification);
autoNextBtn.onClick=[this]{ processor.setAutoNext(autoNextBtn.getToggleState()); };
addAndMakeVisible(autoNextBtn);

// --- Piano Roll 2.0 toolbar -----------------------------------------
pianoGridBox.addItemList ({"1/16", "1/8", "1/4", "1/2"}, 1);
pianoGridBox.setSelectedId (1, juce::dontSendNotification);
pianoGridBox.onChange = [this]
{
    static constexpr int grids[] = { 1, 2, 4, 8 };
    pianoRoll.setGridSteps (grids[juce::jlimit (0, 3, pianoGridBox.getSelectedId() - 1)]);
};
addAndMakeVisible (pianoGridBox);
quantizeButton.setButtonText ("QUANTIZE");
quantizeButton.onClick = [this]
{
    static constexpr int grids[] = { 1, 2, 4, 8 };
    pianoRoll.quantizeSelected (grids[juce::jlimit (0, 3, pianoGridBox.getSelectedId() - 1)]);
};
resetViewButton.onClick = [this] { pianoRoll.resetView(); };
phraseButton.onClick = [this] { pianoRoll.selectPhrase(); };
transposeDownButton.onClick = [this] { pianoRoll.transposeSelected (-12); };
transposeUpButton.onClick = [this] { pianoRoll.transposeSelected (12); };
snapScaleButton.onClick = [this] { pianoRoll.snapSelectedToScale(); };
humanizeSelectionButton.onClick = [this] { pianoRoll.humanizeSelected(); };
selectionLabel.setText ("SEL 0", juce::dontSendNotification);
selectionLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
selectionLabel.setFont (juce::Font (11.0f));
selectionLabel.setJustificationType (juce::Justification::centredLeft);
addAndMakeVisible (quantizeButton);
addAndMakeVisible (resetViewButton);
addAndMakeVisible (phraseButton);
addAndMakeVisible (transposeDownButton);
addAndMakeVisible (transposeUpButton);
addAndMakeVisible (snapScaleButton);
addAndMakeVisible (humanizeSelectionButton);
addAndMakeVisible (selectionLabel);
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
chordDensity.onValueChange=[this]{processor.setChordDensity((float)chordDensity.getValue(), false); scheduleRegeneration(false);};
bassDensity.onValueChange=[this]{processor.setBassDensity((float)bassDensity.getValue(), false); scheduleRegeneration(false);};
melodyDensity.onValueChange=[this]{processor.setMelodyDensity((float)melodyDensity.getValue(), false); scheduleRegeneration(false);};
arpDensity.onValueChange=[this]{processor.setArpDensity((float)arpDensity.getValue(), false); scheduleRegeneration(false);};
motifStrength.onValueChange=[this]{processor.setMotifStrength((float)motifStrength.getValue(), false); scheduleRegeneration(false);};
variationAmount.onValueChange=[this]{processor.setVariationAmount((float)variationAmount.getValue(), false); scheduleRegeneration(true);};
fillAmount.onValueChange=[this]{processor.setFillAmount((float)fillAmount.getValue(), false); scheduleRegeneration(false);};
energy.onValueChange=[this]{processor.setEnergy((float)energy.getValue(), false); scheduleRegeneration(false);};
melodyLength.onValueChange=[this]{processor.setMelodyLength((float)melodyLength.getValue(), false); scheduleRegeneration(false);};
pauseChance.onValueChange=[this]{processor.setPauseChance((float)pauseChance.getValue(), false); scheduleRegeneration(false);};
leapChance.onValueChange=[this]{processor.setLeapChance((float)leapChance.getValue(), false); scheduleRegeneration(false);};
ghostChance.onValueChange=[this]{processor.setGhostChance((float)ghostChance.getValue(), false); scheduleRegeneration(false);};
swing.onValueChange=[this]{processor.setSwing((float)swing.getValue());};
humanize.onValueChange=[this]{processor.setHumanize((float)humanize.getValue());};
complexity.onValueChange=[this]{processor.setComplexity((float)complexity.getValue(), false); scheduleRegeneration(false);};
chords.setButtonText("CHORDS");bass.setButtonText("BASS");melody.setButtonText("MELODY");arp.setButtonText("ARP");drums.setButtonText("DRUMS");
extensions.setButtonText("7/9 EXT");inversions.setButtonText("INV");hookModeButton.setButtonText("HOOK");
soundCloudButton.setButtonText("SOUNDCLOUD");
chords.setToggleState(p.isChordsEnabled(),juce::dontSendNotification);
bass.setToggleState(p.isBassEnabled(),juce::dontSendNotification);
melody.setToggleState(p.isMelodyEnabled(),juce::dontSendNotification);
arp.setToggleState(p.isArpEnabled(),juce::dontSendNotification);
drums.setToggleState(p.isDrumsEnabled(),juce::dontSendNotification);
extensions.setToggleState(p.getChordExtensions(),juce::dontSendNotification);
inversions.setToggleState(p.getInversions(),juce::dontSendNotification);
hookModeButton.setToggleState(p.getHookMode(),juce::dontSendNotification);
soundCloudButton.setToggleState(p.getLeadStyleSoundCloud(),juce::dontSendNotification);
chords.onClick=[this]{processor.setChordsEnabled(chords.getToggleState());};
bass.onClick=[this]{processor.setBassEnabled(bass.getToggleState());};
melody.onClick=[this]{processor.setMelodyEnabled(melody.getToggleState());};
arp.onClick=[this]{processor.setArpEnabled(arp.getToggleState());};
drums.onClick=[this]{ processor.setDrumsEnabled(drums.getToggleState()); if(drums.getToggleState()) setDrumView(true); else if(drumView) setDrumView(false); };
extensions.onClick=[this]{processor.setChordExtensions(extensions.getToggleState());};
inversions.onClick=[this]{processor.setInversions(inversions.getToggleState());};
hookModeButton.onClick=[this]{processor.setHookMode(hookModeButton.getToggleState());};
soundCloudButton.onClick=[this]{processor.setLeadStyleSoundCloud(soundCloudButton.getToggleState());};
addAndMakeVisible(chords);addAndMakeVisible(bass);addAndMakeVisible(melody);addAndMakeVisible(arp);addAndMakeVisible(drums);
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
const int varIndex = processor.getSelectedVariation();
const int score = processor.getVariationScore(varIndex);
variationInfoLabel.setText ("VAR " + juce::String (varIndex + 1) + "  •  SCORE " + juce::String (score),
                            juce::dontSendNotification);
};
dislikeBtn.onClick=[this]{
processor.dislikeAndAdvance();
pianoRoll.resetEditHistory();
refreshTaste();
};
addAndMakeVisible(likeBtn);addAndMakeVisible(dislikeBtn);
tasteLabel.setText("TASTE: 0 like / 0 dislike",juce::dontSendNotification);
tasteLabel.setColour(juce::Label::textColourId,juce::Colours::white.withAlpha(0.8f));
tasteLabel.setFont(juce::Font(11.0f));
tasteLabel.setMinimumHorizontalScale(0.7f);
addAndMakeVisible(tasteLabel);
resetTasteBtn.setButtonText("RESET");
resetTasteBtn.onClick=[this]{
juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,"Reset taste",
"Forget everything MIDI Forge learned from your LIKE / DISLIKE?",{},{},nullptr,
juce::ModalCallbackFunction::create([this](int r){ if(r==1){ processor.resetTaste(); refreshTaste(); } }));
};
addAndMakeVisible(resetTasteBtn);
tasteToggleBtn.setButtonText("TASTE ML");
tasteToggleBtn.setToggleState(processor.getTasteEnabled(), juce::dontSendNotification);
tasteToggleBtn.onClick=[this]{ processor.setTasteEnabled(tasteToggleBtn.getToggleState()); };
addAndMakeVisible(tasteToggleBtn);
addAndMakeVisible (dragHandle);
addAndMakeVisible (pianoRoll);
addChildComponent (drumGrid);
drumViewBtn.onClick=[this]{ setDrumView(! drumView); };
addAndMakeVisible (drumViewBtn);
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
addAndMakeVisible(dragMelody);addAndMakeVisible(dragArp);addAndMakeVisible(dragDrums);
refreshTaste();
startTimerHz (10);
}
void MidiForgeAudioProcessorEditor::scheduleRegeneration (bool preserveSelection)
{
    regenerationPending = true;
    preserveSelectionOnRegenerate = preserveSelectionOnRegenerate || preserveSelection;
    scheduledGenerationNonce = processor.getGenerationNonce();
    regenerationDueMs = juce::Time::currentTimeMillis() + 250;
}
void MidiForgeAudioProcessorEditor::refreshTaste()
{
const int sel = processor.getSelectedVariation();
juce::String t = "TASTE +" + juce::String(processor.getTasteLikes()) + " / -"
+ juce::String(processor.getTasteDislikes()) + "   ML "
+ juce::String((int) std::round(processor.getTasteConfidence() * 100.0f)) + "%   VAR "
+ juce::String(sel + 1) + " (" + juce::String(processor.getVariationScore(sel)) + ")\n"
+ processor.getTasteSummary();
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
g.drawFittedText(drumView ? "DRUMS  -  one row per instrument: click a step to add / remove a hit, M = mute, DRAG = only that instrument" : "PIANO ROLL 2.0  -  multi-select / phrase tools / scale-safe editing",20,452,860,18,juce::Justification::left,1);
g.drawFittedText("VARIATIONS / LEARNING",20,668,860,18,juce::Justification::left,1);
}
void MidiForgeAudioProcessorEditor::resized()
{
const int W = getWidth();
const int left = 20;
const int contentW = W - 40;

// Header
title.setBounds(left,14,360,32);
versionLabel.setBounds(660,20,70,22);
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
drums.setBounds(766,112,80,22);
extensions.setBounds(354,112,82,22);
inversions.setBounds(440,112,68,22);
arpRate.setBounds(512,112,58,22);
hookModeButton.setBounds(574,112,72,22);
soundCloudButton.setBounds(650,112,110,22);
moodBox.setBounds(20,136,106,24);
melodyTypeBox.setBounds(133,136,112,24);
eraBox.setBounds(252,136,70,24);
soundBox.setBounds(329,136,168,24);
articBox.setBounds(503,136,176,24);
chordBox.setBounds(685,136,150,24);
autoNextBtn.setBounds(818,690,78,26);

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
pianoGridBox.setBounds(20,447,62,22);
quantizeButton.setBounds(88,447,80,22);
resetViewButton.setBounds(174,447,82,22);
phraseButton.setBounds(262,447,78,22);
transposeDownButton.setBounds(346,447,48,22);
transposeUpButton.setBounds(400,447,48,22);
snapScaleButton.setBounds(454,447,64,22);
humanizeSelectionButton.setBounds(524,447,82,22);
selectionLabel.setBounds(614,447,170,22);
pianoRoll.setBounds(left,472,contentW,190);
drumGrid.setBounds(left,472,contentW,190);
drumViewBtn.setBounds(850,112,120,22);

// Bottom action rows — always visible in the default 800px editor.
variationBox.setBounds(20,690,74,28);
variationInfoLabel.setBounds(100,690,118,28);
generate.setBounds(222,687,102,32);
newSeed.setBounds(328,687,98,32);
applyVariation.setBounds(432,687,92,32);
exportMidi.setBounds(530,687,118,32);
likeBtn.setBounds(654,687,70,32);
dislikeBtn.setBounds(730,687,82,32);
mutateButton.setBounds(818,687,76,32);
evolveButton.setBounds(900,687,76,32);

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
dragDrums.setBounds(522,764,92,28);
tasteLabel.setBounds(760,758,150,36);
resetTasteBtn.setBounds(916,764,58,26);
tasteToggleBtn.setBounds(884,733,92,24);
}
void MidiForgeAudioProcessorEditor::timerCallback()
{
    if (regenerationPending)
    {
        if (processor.getGenerationNonce() != scheduledGenerationNonce)
        {
            regenerationPending = false;
            preserveSelectionOnRegenerate = false;
        }
        else if (juce::Time::currentTimeMillis() >= regenerationDueMs)
        {
            const bool preserve = preserveSelectionOnRegenerate;
            regenerationPending = false;
            preserveSelectionOnRegenerate = false;
            if (preserve)
                processor.regenerateVariations();
            else
                processor.regenerate();
            pianoRoll.resetEditHistory();
        }
    }

const int id = processor.getSelectedVariation() + 1;
if (id >= 1 && variationBox.getSelectedId() != id)
variationBox.setSelectedId (id, juce::dontSendNotification);
if (moodBox.getSelectedId() != processor.getMood()+1) moodBox.setSelectedId(processor.getMood()+1, juce::dontSendNotification);
if (melodyTypeBox.getSelectedId() != processor.getMelodyType()+1) melodyTypeBox.setSelectedId(processor.getMelodyType()+1, juce::dontSendNotification);
if (eraBox.getSelectedId() != processor.getEra()+1) eraBox.setSelectedId(processor.getEra()+1, juce::dontSendNotification);
if (soundBox.getSelectedId() != processor.getSoundTarget()+1) soundBox.setSelectedId(processor.getSoundTarget()+1, juce::dontSendNotification);
if (articBox.getSelectedId() != processor.getArticulation()+1) articBox.setSelectedId(processor.getArticulation()+1, juce::dontSendNotification);
if (chordBox.getSelectedId() != processor.getChordStyle()+1) chordBox.setSelectedId(processor.getChordStyle()+1, juce::dontSendNotification);
if (drums.getToggleState() != processor.isDrumsEnabled()) drums.setToggleState(processor.isDrumsEnabled(), juce::dontSendNotification);
refreshTaste();
pianoRoll.updateHistoryButtons();
const juce::String selectedCount = juce::String (pianoRoll.getSelectionCount());
const juce::String selectedText = "SEL " + selectedCount;
if (selectionLabel.getText() != selectedText)
    selectionLabel.setText (selectedText, juce::dontSendNotification);
}
