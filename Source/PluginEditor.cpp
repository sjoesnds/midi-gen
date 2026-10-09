#include "PluginEditor.h"
namespace
{
constexpr juce::uint32 kBg        = 0xff0a0d12;
constexpr juce::uint32 kCard     = 0xff121720;
constexpr juce::uint32 kCard2    = 0xff171d27;
constexpr juce::uint32 kBorder   = 0xff27303c;
constexpr juce::uint32 kText     = 0xffeef2f7;
constexpr juce::uint32 kMuted    = 0xff7f8b99;
constexpr juce::uint32 kAccent   = 0xffe7a24b;
constexpr juce::uint32 kAccent2  = 0xff6ca9ff;
constexpr juce::uint32 kSuccess  = 0xff63c88b;

static void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (kCard2));
    c.setColour (juce::ComboBox::outlineColourId, juce::Colour (kBorder));
    c.setColour (juce::ComboBox::textColourId, juce::Colour (kText));
    c.setColour (juce::ComboBox::arrowColourId, juce::Colour (kMuted));
}

static void styleSlider (juce::Slider& s)
{
    s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff27303a));
    s.setColour (juce::Slider::trackColourId, juce::Colour (kAccent));
    s.setColour (juce::Slider::thumbColourId, juce::Colour (kText));
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kText));
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0f141b));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (kBorder));
}

static void styleTextButton (juce::TextButton& b, juce::Colour normal = juce::Colour (kCard2))
{
    b.setColour (juce::TextButton::buttonColourId, normal);
    b.setColour (juce::TextButton::buttonOnColourId, normal.brighter (0.10f));
    b.setColour (juce::TextButton::textColourOffId, juce::Colour (kText));
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

static void styleToggle (juce::ToggleButton& b)
{
    b.setColour (juce::ToggleButton::textColourId, juce::Colour (kText));
    b.setColour (juce::ToggleButton::tickColourId, juce::Colour (kAccent));
    b.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (kMuted));
}

static void drawCard (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                      juce::Colour accent = juce::Colour (kBorder))
{
    const auto r = area.toFloat();
    g.setColour (juce::Colour (kCard));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (accent.withAlpha (0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (juce::Colour (kMuted));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (title.toUpperCase(), area.getX() + 12, area.getY() + 6, area.getWidth() - 24, 12,
                juce::Justification::left, false);
}

static void drawSliderLabel (juce::Graphics& g, const juce::Slider& s, const juce::String& text)
{
    const auto r = s.getBounds();
    g.setColour (juce::Colour (kMuted));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (text.toUpperCase(), r.getX(), r.getY() - 1, r.getWidth() - 62, 11,
                juce::Justification::left, false);
}

static void drawFieldLabel (juce::Graphics& g, const juce::Component& c, const juce::String& text)
{
    const auto r = c.getBounds();
    g.setColour (juce::Colour (kMuted));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (text.toUpperCase(), r.getX(), r.getY() - 13, r.getWidth(), 11,
                juce::Justification::left, false);
}
}

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
    // Dragging a MIDI file is an output action only; it never trains Taste ML.

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
setSize(1120, 850);
setResizable(true, true);
setResizeLimits(1060, 800, 1440, 1080);
title.setText("MIDI FORGE",juce::dontSendNotification);
versionLabel.setText("v0.100.1", juce::dontSendNotification);
versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
versionLabel.setFont(juce::Font(11.0f));
addAndMakeVisible(versionLabel);
tempoLabel.setText("DAW BPM --", juce::dontSendNotification);
tempoLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
tempoLabel.setFont(juce::Font(11.0f));
addAndMakeVisible(tempoLabel);
// Старый конструктор Font: жив и на JUCE 7, и на JUCE 8 (в 8 — deprecated, но компилируется).
title.setFont (juce::Font (30.0f, juce::Font::bold));
sectionLabel.setText("GENERATIVE MIDI STUDIO  /  FAST IDEA -> EDIT -> DRAG",juce::dontSendNotification);
sectionLabel.setFont (juce::FontOptions (11.0f));
sectionLabel.setColour (juce::Label::textColourId, juce::Colour (kMuted));
addAndMakeVisible(title);
addAndMakeVisible(sectionLabel);

for (auto* c : { &root, &scale, &progression, &rhythm, &mode, &bars, &octave,
                 &arpRate, &variationBox, &moodBox, &melodyTypeBox, &soundBox,
                 &articBox, &chordBox, &pianoGridBox })
    styleCombo (*c);

for (auto* s : { &chordDensity, &bassDensity, &melodyDensity, &arpDensity, &swing, &humanize,
                 &complexity, &motifStrength, &variationAmount, &fillAmount, &energy,
                 &melodyLength, &pauseChance, &leapChance, &ghostChance })
    styleSlider (*s);

for (auto* b : { &generate, &newSeed, &applyVariation, &exportMidi, &revertBtn, &advancedButton, &quantizeButton,
                 &resetViewButton, &phraseButton, &barButton, &transposeDownButton,
                 &transposeUpButton, &snapScaleButton, &humanizeSelectionButton,
                 &duplicateButton, &reverseButton, &doubleTimeButton, &halfTimeButton,
                 &rotateButton, &normalizeVelocityButton, &frameSelectionButton,
                 &mutateButton, &evolveButton, &similarButton, &drumViewBtn, &likeBtn, &dislikeBtn,
                 &resetTasteBtn, &undoBtn, &redoBtn, &clearBtn })
    styleTextButton (*b);

for (auto* b : { &chords, &bass, &melody, &arp, &drums, &extensions, &inversions,
                 &hookModeButton, &soundCloudButton, &humanizeModeButton,
                 &lockChordsBtn, &lockBassBtn, &lockMelodyBtn, &lockArpBtn,
                 &autoNextBtn, &tasteToggleBtn })
    styleToggle (*b);

generate.setColour (juce::TextButton::buttonColourId, juce::Colour (kAccent));
generate.setColour (juce::TextButton::buttonOnColourId, juce::Colour (kAccent).brighter (0.12f));
generate.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
generate.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
generate.setButtonText ("MAGIC");
generate.setTooltip ("Explore new musical DNA and generate a fresh set of coordinated loop variations.");
newSeed.setTooltip ("Keep the current musical direction but reroll the seed.");
applyVariation.setTooltip ("Commit the selected variation as the active loop.");
similarButton.setTooltip ("Generate close relatives of the selected loop.");
exportMidi.setTooltip ("Save the active loop as a standard MIDI file.");

root.setTooltip ("Root note / key center");
scale.setTooltip ("Scale / mode");
progression.setTooltip ("Chord progression style");
rhythm.setTooltip ("Rhythmic vocabulary");
bars.setTooltip ("Phrase length in bars");
octave.setTooltip ("Starting octave");
moodBox.setTooltip ("Emotional color");
melodyTypeBox.setTooltip ("Melody archetype");
soundBox.setTooltip ("Target instrument / register");
articBox.setTooltip ("Optional performance articulation");
chordBox.setTooltip ("Chord voicing behavior");
arpRate.setTooltip ("Arpeggio rate");

chordDensity.setTooltip ("Chord movement density");
bassDensity.setTooltip ("Bass movement density");
melodyDensity.setTooltip ("Melody note density");
arpDensity.setTooltip ("Arpeggio density");
motifStrength.setTooltip ("How strongly the central motif survives mutations");
variationAmount.setTooltip ("How far variations are allowed to drift");
fillAmount.setTooltip ("Phrase fill / activity");
energy.setTooltip ("Overall energy curve");
melodyLength.setTooltip ("Typical note length");
pauseChance.setTooltip ("Probability of intentional space");
leapChance.setTooltip ("Large interval probability");
ghostChance.setTooltip ("Quiet ghost-note activity");
swing.setTooltip ("Groove swing");
humanize.setTooltip ("Optional human timing / velocity amount");
complexity.setTooltip ("Simple -> complex melody bias");
mutateButton.setTooltip ("Make a controlled variation of the current loop.");
evolveButton.setTooltip ("Push the current loop into a new developmental state.");
likeBtn.setTooltip ("Teach Taste ML that this variation works.");
dislikeBtn.setTooltip ("Reject this variation and advance.");

root.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);
root.setSelectedId(p.getRoot()+1); root.onChange=[this]{processor.setRoot(root.getSelectedId()-1);}; addAndMakeVisible(root);
scale.addItemList({"Major","Minor","Dorian","Phrygian","Harmonic Minor","Melodic Minor","Pentatonic","Lydian","Mixolydian","Locrian","Harmonic Major","Blues"},1);
scale.setSelectedId(p.getScale()+1); scale.onChange=[this]{processor.setScale(scale.getSelectedId()-1);}; addAndMakeVisible(scale);
progression.addItemList({"Auto","Pop","Dark","Emotional","Cinematic","Jazz-like","Looping"},1);
progression.setSelectedId(p.getProgression()+1); progression.onChange=[this]{processor.setProgression(progression.getSelectedId()-1);}; addAndMakeVisible(progression);
rhythm.addItemList({"Straight","Syncopated","Broken","Euclidean"},1);
rhythm.setSelectedId(p.getRhythm()+1); rhythm.onChange=[this]{processor.setRhythm(rhythm.getSelectedId()-1);}; addAndMakeVisible(rhythm);
moodBox.addItemList({"Neutral","Dark","Melancholic","Euphoric","Aggressive","Dreamy","Nostalgic","Mysterious","Energetic"},1);
moodBox.setSelectedId(p.getMood()+1); moodBox.onChange=[this]{processor.setMood(moodBox.getSelectedId()-1);}; addAndMakeVisible(moodBox);
melodyTypeBox.addItemList({"Hook","Vocal-like","Riff","Ostinato","Arp","Counter","Sparse Lead","Phrase"},1);
melodyTypeBox.setSelectedId(p.getMelodyType()+1); melodyTypeBox.onChange=[this]{processor.setMelodyType(melodyTypeBox.getSelectedId()-1);}; addAndMakeVisible(melodyTypeBox);
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
barButton.onClick = [this] { pianoRoll.selectBar(); };
transposeDownButton.onClick = [this] { pianoRoll.transposeSelected (-12); };
transposeUpButton.onClick = [this] { pianoRoll.transposeSelected (12); };
snapScaleButton.onClick = [this] { pianoRoll.snapSelectedToScale(); };
humanizeSelectionButton.onClick = [this] { pianoRoll.humanizeSelected(); };
duplicateButton.onClick = [this] { pianoRoll.duplicateSelected(); };
reverseButton.onClick = [this] { pianoRoll.reverseSelected(); };
doubleTimeButton.onClick = [this] { pianoRoll.scaleTimeSelected (true); };
halfTimeButton.onClick = [this] { pianoRoll.scaleTimeSelected (false); };
rotateButton.onClick = [this] { pianoRoll.rotateSelected(); };
normalizeVelocityButton.onClick = [this] { pianoRoll.normalizeVelocitySelected(); };
frameSelectionButton.onClick = [this] { pianoRoll.frameSelection(); };
selectionLabel.setText ("SEL 0", juce::dontSendNotification);
selectionLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
selectionLabel.setFont (juce::Font (11.0f));
selectionLabel.setJustificationType (juce::Justification::centredLeft);
addAndMakeVisible (quantizeButton);
addAndMakeVisible (resetViewButton);
addAndMakeVisible (phraseButton);
addAndMakeVisible (barButton);
addAndMakeVisible (transposeDownButton);
addAndMakeVisible (transposeUpButton);
addAndMakeVisible (snapScaleButton);
addAndMakeVisible (humanizeSelectionButton);
addAndMakeVisible (duplicateButton);
addAndMakeVisible (reverseButton);
addAndMakeVisible (doubleTimeButton);
addAndMakeVisible (halfTimeButton);
addAndMakeVisible (rotateButton);
addAndMakeVisible (normalizeVelocityButton);
addAndMakeVisible (frameSelectionButton);
addAndMakeVisible (selectionLabel);

duplicateButton.setTooltip ("Duplicate the selected phrase inside the loop");
reverseButton.setTooltip ("Reverse selected notes in time");
doubleTimeButton.setTooltip ("Compress selected timing to 2x speed");
halfTimeButton.setTooltip ("Expand selected timing to half speed");
rotateButton.setTooltip ("Rotate selected phrase by one quarter");
normalizeVelocityButton.setTooltip ("Set selected velocities to their average");
frameSelectionButton.setTooltip ("Zoom and pan to the current selection");
    revertBtn.setTooltip ("Restore the variation to the state captured before Piano Roll edits.");
mode.setVisible(false);
bars.addItemList({"1","2","4","8","16"},1);
const int bid=p.getBars()==1?1:p.getBars()==2?2:p.getBars()==4?3:p.getBars()==8?4:5;
bars.setSelectedId(bid);
bars.onChange=[this]{const int a[5]={1,2,4,8,16};processor.setBars(a[bars.getSelectedId()-1]);}; addAndMakeVisible(bars);
advancedButton.setButtonText ("ADVANCED");
advancedButton.setTooltip ("Show progression, rhythm, sound, harmony and per-part generation controls.");
advancedButton.onClick = [this] { setAdvancedControlsVisible (! advancedControlsVisible); };
addAndMakeVisible (advancedButton);
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
humanizeModeButton.setButtonText("LIVE HUMANIZE");
humanizeModeButton.setToggleState(p.isHumanizeEnabled(), juce::dontSendNotification);
humanizeModeButton.setTooltip("Playback-only human performance layer. Generation and exported MIDI stay clean.");
humanizeModeButton.onClick = [this]
{
    processor.setHumanizeEnabled (humanizeModeButton.getToggleState());
};
addAndMakeVisible (humanizeModeButton);
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
revertBtn.setButtonText ("REVERT");
revertBtn.setTooltip ("Revert Piano Roll to the state captured when this variation was selected.");
generate.onClick=[this]{
    if (processor.isGenerating())
        magicPending = true;
    else
    {
        processor.magicRandomize();
        busyOverlay.sync();
        pianoRoll.resetEditHistory();
    }
};
newSeed.onClick=[this]{ processor.rerollSameDNA(); busyOverlay.sync(); pianoRoll.resetEditHistory(); };
applyVariation.onClick=[this]{ processor.chooseVariation(variationBox.getSelectedId()-1); pianoRoll.resetEditHistory(); };
revertBtn.onClick=[this]{ pianoRoll.revertEdits(); };
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
similarButton.onClick=[this]{ processor.similarToSelected(); pianoRoll.resetEditHistory(); repaint(); };
similarButton.setTooltip ("More like this: keeps the selected loop as variation 1 and fills 2-8 with close relatives, nearest first");
addAndMakeVisible(generate);addAndMakeVisible(newSeed);addAndMakeVisible(applyVariation);addAndMakeVisible(exportMidi);addAndMakeVisible(revertBtn);
addAndMakeVisible(mutateButton); addAndMakeVisible(evolveButton); addAndMakeVisible(similarButton);

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

// Compact creator-first UI: key, scale and length stay immediately visible, while
// the ADVANCED drawer exposes the restored musical controls on demand.
// Keep the rarely used legacy section-mode control hidden; real musical controls
// are available through the ADVANCED drawer instead of being silently removed.
mode.setVisible (false);
setAdvancedControlsVisible (false);

addChildComponent (busyOverlay);
lastGenerationDone = processor.getGenerationDoneCounter();
startTimerHz (10);
}
void MidiForgeAudioProcessorEditor::setAdvancedControlsVisible (bool visible)
{
    advancedControlsVisible = visible;
    for (auto* c : { &progression, &rhythm, &octave, &arpRate, &moodBox,
                     &melodyTypeBox, &soundBox, &articBox, &chordBox })
        c->setVisible (visible);
    for (auto* slider : { &chordDensity, &bassDensity, &melodyDensity, &arpDensity,
                          &complexity, &motifStrength, &variationAmount, &fillAmount,
                          &energy, &melodyLength, &pauseChance, &leapChance, &ghostChance })
        slider->setVisible (visible);

    advancedButton.setButtonText (visible ? "HIDE ADVANCED" : "ADVANCED");
    const int targetHeight = visible ? 1060 : 850;
    if (getHeight() != targetHeight)
        setSize (getWidth(), targetHeight);
    resized();
    repaint();
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
    g.fillAll (juce::Colour (kBg));

    const int W = getWidth();
    const int margin = 18;
    const int innerW = W - margin * 2;

    g.setColour (juce::Colour (kCard));
    g.fillRoundedRectangle (juce::Rectangle<float> (margin, 12, innerW, 54), 12.0f);
    g.setColour (juce::Colour (kBorder));
    g.drawRoundedRectangle (juce::Rectangle<float> (margin, 12, innerW, 54), 12.0f, 1.0f);

    g.setColour (juce::Colour (kAccent));
    g.fillRoundedRectangle (juce::Rectangle<float> (margin + 14, 24, 4, 30), 2.0f);
    g.setColour (juce::Colour (kText));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("MIDI", margin + 28, 20, 36, 12, juce::Justification::left, false);

    const int layoutShift = advancedControlsVisible ? 208 : 0;
    drawCard (g, { margin, 76, innerW, advancedControlsVisible ? 284 : 76 }, "01  CREATIVE SPACE", juce::Colour (kAccent).withAlpha (0.40f));
    drawCard (g, { margin, 160 + layoutShift, innerW, 64 }, "02  DISCOVER", juce::Colour (kAccent).withAlpha (0.45f));
    drawCard (g, { margin, 232 + layoutShift, innerW, 54 }, "03  PARTS", juce::Colour (kAccent2).withAlpha (0.35f));
    drawCard (g, { margin, 294 + layoutShift, innerW, 282 }, "04  PIANO ROLL / EDIT", juce::Colour (kAccent2).withAlpha (0.30f));
    drawCard (g, { margin, 584 + layoutShift, innerW, 116 }, "05  PERFORMANCE", juce::Colour (kAccent).withAlpha (0.26f));
    drawCard (g, { margin, 712 + layoutShift, innerW, 120 }, "06  KEEP / EXPORT", juce::Colour (kSuccess).withAlpha (0.24f));

    drawFieldLabel (g, root, "Key");
    drawFieldLabel (g, scale, "Scale");
    drawFieldLabel (g, bars, "Bars");
    if (advancedControlsVisible)
    {
        drawFieldLabel (g, progression, "Progression");
        drawFieldLabel (g, rhythm, "Rhythm");
        drawFieldLabel (g, moodBox, "Mood");
        drawFieldLabel (g, melodyTypeBox, "Melody type");
        drawFieldLabel (g, soundBox, "Sound target");
        drawFieldLabel (g, articBox, "Articulation");
        drawFieldLabel (g, chordBox, "Chord style");
        drawFieldLabel (g, octave, "Octave");
        drawFieldLabel (g, arpRate, "Arp rate");
        drawSliderLabel (g, chordDensity, "Chord density");
        drawSliderLabel (g, bassDensity, "Bass density");
        drawSliderLabel (g, melodyDensity, "Melody density");
        drawSliderLabel (g, arpDensity, "Arp density");
        drawSliderLabel (g, complexity, "Complexity");
        drawSliderLabel (g, motifStrength, "Motif strength");
        drawSliderLabel (g, variationAmount, "Variation amount");
        drawSliderLabel (g, fillAmount, "Phrase fill");
        drawSliderLabel (g, energy, "Energy");
        drawSliderLabel (g, melodyLength, "Note length");
        drawSliderLabel (g, pauseChance, "Pause chance");
        drawSliderLabel (g, leapChance, "Leap chance");
        drawSliderLabel (g, ghostChance, "Ghost notes");
    }
    drawSliderLabel (g, swing, "Swing");
    drawSliderLabel (g, humanize, "Humanize");
}

void MidiForgeAudioProcessorEditor::resized()
{
    busyOverlay.setBounds (getLocalBounds());

    const int W = getWidth();
    const int margin = 18;
    const int innerW = W - margin * 2;
    const int gap = 8;

    title.setBounds (margin + 28, 20, 380, 28);
    sectionLabel.setBounds (margin + 28, 46, 600, 16);
    versionLabel.setBounds (W - 215, 20, 70, 22);
    tempoLabel.setBounds (W - 140, 20, 122, 22);

    // Only a few explicit constraints remain visible. Everything else is
    // generated from the internal creative DNA when MAGIC is pressed.
    const int layoutShift = advancedControlsVisible ? 208 : 0;
    root.setBounds (margin + 12, 108, 92, 26);
    scale.setBounds (margin + 114, 108, 150, 26);
    bars.setBounds (margin + 274, 108, 62, 26);
    advancedButton.setBounds (margin + 350, 108, 94, 26);
    hookModeButton.setBounds (margin + 454, 108, 76, 26);
    soundCloudButton.setBounds (margin + 538, 108, 110, 26);

    if (advancedControlsVisible)
    {
        progression.setBounds (margin + 12, 149, 132, 24);
        rhythm.setBounds (margin + 152, 149, 118, 24);
        moodBox.setBounds (margin + 278, 149, 138, 24);
        melodyTypeBox.setBounds (margin + 424, 149, 152, 24);
        soundBox.setBounds (margin + 584, 149, 188, 24);

        articBox.setBounds (margin + 12, 184, 155, 24);
        chordBox.setBounds (margin + 175, 184, 148, 24);
        octave.setBounds (margin + 331, 184, 92, 24);
        arpRate.setBounds (margin + 431, 184, 92, 24);

        const int advancedX = margin + 12;
        const int advancedGap = 10;
        const int advancedWidth = (innerW - 24 - 4 * advancedGap) / 5;
        auto placeAdvancedSlider = [&] (juce::Slider& slider, int column, int row)
        {
            slider.setBounds (advancedX + column * (advancedWidth + advancedGap),
                              220 + row * 38, advancedWidth, 24);
        };
        placeAdvancedSlider (chordDensity, 0, 0);
        placeAdvancedSlider (bassDensity, 1, 0);
        placeAdvancedSlider (melodyDensity, 2, 0);
        placeAdvancedSlider (arpDensity, 3, 0);
        placeAdvancedSlider (complexity, 4, 0);
        placeAdvancedSlider (motifStrength, 0, 1);
        placeAdvancedSlider (variationAmount, 1, 1);
        placeAdvancedSlider (fillAmount, 2, 1);
        placeAdvancedSlider (energy, 3, 1);
        placeAdvancedSlider (melodyLength, 4, 1);
        placeAdvancedSlider (pauseChance, 0, 2);
        placeAdvancedSlider (leapChance, 1, 2);
        placeAdvancedSlider (ghostChance, 2, 2);
    }

    variationBox.setBounds (margin + 12, 178 + layoutShift, 78, 28);
    variationInfoLabel.setBounds (margin + 98, 178 + layoutShift, 125, 28);
    generate.setBounds (margin + 232, 171 + layoutShift, 132, 40);
    newSeed.setBounds (margin + 374, 178 + layoutShift, 92, 28);
    applyVariation.setBounds (margin + 474, 178 + layoutShift, 88, 28);
    similarButton.setBounds (margin + 570, 178 + layoutShift, 88, 28);
    exportMidi.setBounds (margin + 666, 178 + layoutShift, 110, 28);
    dragHandle.setBounds (margin + 786, 171 + layoutShift, 148, 40);

    int x = margin + 12;
    const int y = 250 + layoutShift;
    auto put = [&] (juce::Component& c, int w)
    {
        c.setBounds (x, y, w, 24);
        x += w + gap;
    };

    put (chords, 76);
    put (bass, 64);
    put (melody, 80);
    put (arp, 58);
    put (drums, 70);
    put (extensions, 72);
    put (inversions, 58);
    put (autoNextBtn, 88);
    drumViewBtn.setBounds (W - margin - 92, 250 + layoutShift, 80, 24);

    pianoGridBox.setBounds (margin + 12, 306 + layoutShift, 58, 24);
    quantizeButton.setBounds (margin + 76, 306 + layoutShift, 72, 24);
    resetViewButton.setBounds (margin + 156, 306 + layoutShift, 76, 24);
    phraseButton.setBounds (margin + 240, 306 + layoutShift, 66, 24);
    barButton.setBounds (margin + 314, 306 + layoutShift, 48, 24);
    transposeDownButton.setBounds (margin + 370, 306 + layoutShift, 48, 24);
    transposeUpButton.setBounds (margin + 426, 306 + layoutShift, 48, 24);
    snapScaleButton.setBounds (margin + 482, 306 + layoutShift, 62, 24);
    humanizeSelectionButton.setBounds (margin + 552, 306 + layoutShift, 82, 24);
    selectionLabel.setBounds (margin + 642, 306 + layoutShift, 120, 24);

    duplicateButton.setBounds (margin + 12, 332 + layoutShift, 52, 22);
    reverseButton.setBounds (margin + 70, 332 + layoutShift, 52, 22);
    doubleTimeButton.setBounds (margin + 128, 332 + layoutShift, 46, 22);
    halfTimeButton.setBounds (margin + 180, 332 + layoutShift, 52, 22);
    rotateButton.setBounds (margin + 238, 332 + layoutShift, 52, 22);
    normalizeVelocityButton.setBounds (margin + 296, 332 + layoutShift, 76, 22);
    frameSelectionButton.setBounds (margin + 378, 332 + layoutShift, 66, 22);
    revertBtn.setBounds (margin + 454, 332 + layoutShift, 66, 22);
    historyLabel.setBounds (margin + 526, 331 + layoutShift, 116, 23);
    undoBtn.setBounds (margin + 650, 332 + layoutShift, 58, 22);
    redoBtn.setBounds (margin + 718, 332 + layoutShift, 58, 22);
    clearBtn.setBounds (margin + 784, 332 + layoutShift, 58, 22);

    pianoRoll.setBounds (margin + 12, 362 + layoutShift, innerW - 24, 198);
    drumGrid.setBounds (margin + 12, 362 + layoutShift, innerW - 24, 198);

    // Performance controls are deliberately tiny and optional. Generation is
    // not exposed here; humanization remains an explicit post-generation action.
    swing.setBounds (margin + 12, 604 + layoutShift, 300, 20);
    humanize.setBounds (margin + 326, 604 + layoutShift, 300, 20);
    humanizeModeButton.setBounds (margin + 640, 600 + layoutShift, 250, 28);

    const int footerY = 656 + layoutShift;
    likeBtn.setBounds (margin + 12, footerY, 64, 28);
    dislikeBtn.setBounds (margin + 82, footerY, 78, 28);
    lockChordsBtn.setBounds (margin + 172, footerY, 86, 28);
    lockBassBtn.setBounds (margin + 266, footerY, 80, 28);
    lockMelodyBtn.setBounds (margin + 354, footerY, 92, 28);
    lockArpBtn.setBounds (margin + 454, footerY, 80, 28);
    mutateButton.setBounds (margin + 542, footerY, 72, 28);
    evolveButton.setBounds (margin + 622, footerY, 72, 28);
    tasteToggleBtn.setBounds (margin + 702, footerY, 90, 28);
    resetTasteBtn.setBounds (margin + 800, footerY, 92, 28);

    const int dragFooterY = 704 + layoutShift;
    dragChords.setBounds (margin + 12,  dragFooterY, 84, 24);
    dragBass.setBounds   (margin + 104, dragFooterY, 84, 24);
    dragMelody.setBounds (margin + 196, dragFooterY, 84, 24);
    dragArp.setBounds    (margin + 288, dragFooterY, 84, 24);
    dragDrums.setBounds  (margin + 380, dragFooterY, 84, 24);

    busyOverlay.toFront (false);
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

const uint32_t generationDone = processor.getGenerationDoneCounter();
if (generationDone != lastGenerationDone)
{
    lastGenerationDone = generationDone;
    pianoRoll.resetEditHistory();   // the background job finished: fresh bank, old undo history no longer applies
    repaint();
}

if (magicPending && ! processor.isGenerating())
{
    magicPending = false;
    processor.magicRandomize();
    busyOverlay.sync();
    pianoRoll.resetEditHistory();
}

const double bpm = processor.getHostBpm();
const juce::String bpmText = "DAW BPM " + juce::String (juce::roundToInt (bpm));
if (tempoLabel.getText() != bpmText)
    tempoLabel.setText (bpmText, juce::dontSendNotification);

const int id = processor.getSelectedVariation() + 1;
if (id >= 1 && variationBox.getSelectedId() != id)
variationBox.setSelectedId (id, juce::dontSendNotification);

// MAGIC changes the source/harmony controls and several musical sliders in the processor.
// Keep the editor as a faithful view of the actual generation state.
if (root.getSelectedId() != processor.getRoot()+1) root.setSelectedId (processor.getRoot()+1, juce::dontSendNotification);
if (scale.getSelectedId() != processor.getScale()+1) scale.setSelectedId (processor.getScale()+1, juce::dontSendNotification);
if (progression.getSelectedId() != processor.getProgression()+1) progression.setSelectedId (processor.getProgression()+1, juce::dontSendNotification);
if (rhythm.getSelectedId() != processor.getRhythm()+1) rhythm.setSelectedId (processor.getRhythm()+1, juce::dontSendNotification);
const int barsId = processor.getBars() == 1 ? 1 : processor.getBars() == 2 ? 2 : processor.getBars() == 4 ? 3 : processor.getBars() == 8 ? 4 : 5;
if (bars.getSelectedId() != barsId) bars.setSelectedId (barsId, juce::dontSendNotification);
if (octave.getSelectedId() != processor.getOctave()-1) octave.setSelectedId(processor.getOctave()-1, juce::dontSendNotification);
if (arpRate.getSelectedId() != processor.getArpRate()) arpRate.setSelectedId(processor.getArpRate(), juce::dontSendNotification);

if (moodBox.getSelectedId() != processor.getMood()+1) moodBox.setSelectedId(processor.getMood()+1, juce::dontSendNotification);
if (melodyTypeBox.getSelectedId() != processor.getMelodyType()+1) melodyTypeBox.setSelectedId(processor.getMelodyType()+1, juce::dontSendNotification);
if (soundBox.getSelectedId() != processor.getSoundTarget()+1) soundBox.setSelectedId(processor.getSoundTarget()+1, juce::dontSendNotification);
if (articBox.getSelectedId() != processor.getArticulation()+1) articBox.setSelectedId(processor.getArticulation()+1, juce::dontSendNotification);
if (chordBox.getSelectedId() != processor.getChordStyle()+1) chordBox.setSelectedId(processor.getChordStyle()+1, juce::dontSendNotification);

auto syncSlider = [] (juce::Slider& slider, double value)
{
    if (std::abs (slider.getValue() - value) > 0.0005)
        slider.setValue (value, juce::dontSendNotification);
};
syncSlider (chordDensity, processor.getChordDensity());
syncSlider (bassDensity, processor.getBassDensity());
syncSlider (melodyDensity, processor.getMelodyDensity());
syncSlider (arpDensity, processor.getArpDensity());
syncSlider (motifStrength, processor.getMotifStrength());
syncSlider (variationAmount, processor.getVariationAmount());
syncSlider (fillAmount, processor.getFillAmount());
syncSlider (energy, processor.getEnergy());
syncSlider (melodyLength, processor.getMelodyLength());
syncSlider (pauseChance, processor.getPauseChance());
syncSlider (leapChance, processor.getLeapChance());
syncSlider (ghostChance, processor.getGhostChance());
syncSlider (swing, processor.getSwing());
syncSlider (humanize, processor.getHumanize());
syncSlider (complexity, processor.getComplexity());

if (chords.getToggleState() != processor.isChordsEnabled()) chords.setToggleState(processor.isChordsEnabled(), juce::dontSendNotification);
if (bass.getToggleState() != processor.isBassEnabled()) bass.setToggleState(processor.isBassEnabled(), juce::dontSendNotification);
if (melody.getToggleState() != processor.isMelodyEnabled()) melody.setToggleState(processor.isMelodyEnabled(), juce::dontSendNotification);
if (arp.getToggleState() != processor.isArpEnabled()) arp.setToggleState(processor.isArpEnabled(), juce::dontSendNotification);
if (extensions.getToggleState() != processor.getChordExtensions()) extensions.setToggleState(processor.getChordExtensions(), juce::dontSendNotification);
if (inversions.getToggleState() != processor.getInversions()) inversions.setToggleState(processor.getInversions(), juce::dontSendNotification);
if (hookModeButton.getToggleState() != processor.getHookMode()) hookModeButton.setToggleState(processor.getHookMode(), juce::dontSendNotification);
if (soundCloudButton.getToggleState() != processor.getLeadStyleSoundCloud()) soundCloudButton.setToggleState(processor.getLeadStyleSoundCloud(), juce::dontSendNotification);

if (drums.getToggleState() != processor.isDrumsEnabled()) drums.setToggleState(processor.isDrumsEnabled(), juce::dontSendNotification);
if (humanizeModeButton.getToggleState() != processor.isHumanizeEnabled()) humanizeModeButton.setToggleState(processor.isHumanizeEnabled(), juce::dontSendNotification);
refreshTaste();
pianoRoll.updateHistoryButtons();
const juce::String selectedCount = juce::String (pianoRoll.getSelectionCount());
const juce::String selectedText = "SEL " + selectedCount;
if (selectionLabel.getText() != selectedText)
    selectionLabel.setText (selectedText, juce::dontSendNotification);
}