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
versionLabel.setText("v0.86.1", juce::dontSendNotification);
versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
versionLabel.setFont(juce::Font(11.0f));
addAndMakeVisible(versionLabel);
tempoLabel.setText("DAW BPM --", juce::dontSendNotification);
tempoLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
tempoLabel.setFont(juce::Font(11.0f));
addAndMakeVisible(tempoLabel);
// Старый конструктор Font: жив и на JUCE 7, и на JUCE 8 (в 8 — deprecated, но компилируется).
title.setFont (juce::Font (30.0f, juce::Font::bold));
sectionLabel.setText("GENERATIVE MELODY STUDIO  /  FAST IDEA -> EDIT -> DRAG",juce::dontSendNotification);
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

for (auto* b : { &generate, &newSeed, &applyVariation, &exportMidi, &quantizeButton,
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
generate.setTooltip ("Randomize the musical DNA and generate a fresh set of melodies.");
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
humanizeModeButton.setButtonText("HUMANIZE");
humanizeModeButton.setToggleState(p.isHumanizeEnabled(), juce::dontSendNotification);
humanizeModeButton.setTooltip("Enable human performance timing, velocity and phrase asymmetry. OFF keeps MIDI tight and electronic.");
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
addAndMakeVisible(generate);addAndMakeVisible(newSeed);addAndMakeVisible(applyVariation);addAndMakeVisible(exportMidi);
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
addChildComponent (busyOverlay);
lastGenerationDone = processor.getGenerationDoneCounter();
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

    drawCard (g, { margin, 76, innerW, 106 }, "01  SOURCE / MUSICAL IDENTITY", juce::Colour (kAccent).withAlpha (0.35f));
    drawCard (g, { margin, 188, innerW, 64 }, "02  GENERATE / CHOOSE", juce::Colour (kAccent).withAlpha (0.45f));
    drawCard (g, { margin, 260, innerW, 54 }, "03  PARTS / CHARACTER", juce::Colour (kAccent2).withAlpha (0.35f));
    drawCard (g, { margin, 322, innerW, 254 }, "04  PIANO ROLL / EDIT", juce::Colour (kAccent2).withAlpha (0.30f));
    drawCard (g, { margin, 584, innerW, 148 }, "05  DETAIL / MUSICAL DNA", juce::Colour (kAccent).withAlpha (0.28f));
    drawCard (g, { margin, 740, innerW, 92 }, "06  KEEP / LEARN / EXPORT", juce::Colour (kSuccess).withAlpha (0.24f));

    drawFieldLabel (g, root, "Key");
    drawFieldLabel (g, scale, "Scale");
    drawFieldLabel (g, progression, "Progression");
    drawFieldLabel (g, rhythm, "Rhythm");
    drawFieldLabel (g, bars, "Bars");
    drawFieldLabel (g, octave, "Octave");
    drawFieldLabel (g, moodBox, "Mood");
    drawFieldLabel (g, melodyTypeBox, "Melody");
    drawFieldLabel (g, soundBox, "Sound");
    drawFieldLabel (g, articBox, "Articulation");
    drawFieldLabel (g, chordBox, "Chords");
    drawFieldLabel (g, arpRate, "Arp Rate");

    drawSliderLabel (g, chordDensity, "Chord Density");
    drawSliderLabel (g, bassDensity, "Bass Density");
    drawSliderLabel (g, melodyDensity, "Melody Density");
    drawSliderLabel (g, arpDensity, "Arp Density");
    drawSliderLabel (g, motifStrength, "Motif");
    drawSliderLabel (g, variationAmount, "Variation");
    drawSliderLabel (g, fillAmount, "Fill");
    drawSliderLabel (g, energy, "Energy");
    drawSliderLabel (g, melodyLength, "Note Length");
    drawSliderLabel (g, pauseChance, "Space");
    drawSliderLabel (g, leapChance, "Leaps");
    drawSliderLabel (g, ghostChance, "Ghosts");
    drawSliderLabel (g, swing, "Swing");
    drawSliderLabel (g, humanize, "Humanize");
    drawSliderLabel (g, complexity, "Complexity");

    g.setColour (juce::Colour (kMuted).withAlpha (0.75f));
    g.setFont (juce::FontOptions (9.0f));
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

    const int sourceX = margin + 12;
    const int sourceY1 = 108;
    const int sourceY2 = 150;

    root.setBounds (sourceX, sourceY1, 74, 26);
    scale.setBounds (sourceX + 82, sourceY1, 124, 26);
    progression.setBounds (sourceX + 214, sourceY1, 116, 26);
    rhythm.setBounds (sourceX + 338, sourceY1, 104, 26);
    bars.setBounds (sourceX + 450, sourceY1, 50, 26);
    octave.setBounds (sourceX + 508, sourceY1, 52, 26);

    moodBox.setBounds (sourceX, sourceY2, 100, 26);
    melodyTypeBox.setBounds (sourceX + 108, sourceY2, 116, 26);
    soundBox.setBounds (sourceX + 232, sourceY2, 160, 26);
    articBox.setBounds (sourceX + 400, sourceY2, 170, 26);
    chordBox.setBounds (sourceX + 578, sourceY2, 140, 26);
    arpRate.setBounds (sourceX + 726, sourceY2, 64, 26);

    variationBox.setBounds (margin + 12, 214, 78, 28);
    variationInfoLabel.setBounds (margin + 98, 214, 125, 28);
    generate.setBounds (margin + 232, 207, 132, 40);
    newSeed.setBounds (margin + 372, 214, 94, 28);
    applyVariation.setBounds (margin + 474, 214, 88, 28);
    similarButton.setBounds (margin + 570, 214, 88, 28);
    exportMidi.setBounds (margin + 666, 214, 110, 28);
    dragHandle.setBounds (margin + 786, 207, 148, 40);

    int x = margin + 12;
    const int y = 282;
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
    put (hookModeButton, 64);
    put (soundCloudButton, 104);
    put (autoNextBtn, 88);
    drumViewBtn.setBounds (W - margin - 92, 282, 80, 24);

    pianoGridBox.setBounds (margin + 12, 344, 58, 24);
    quantizeButton.setBounds (margin + 76, 344, 72, 24);
    resetViewButton.setBounds (margin + 156, 344, 76, 24);
    phraseButton.setBounds (margin + 240, 344, 66, 24);
    barButton.setBounds (margin + 314, 344, 48, 24);
    transposeDownButton.setBounds (margin + 370, 344, 48, 24);
    transposeUpButton.setBounds (margin + 426, 344, 48, 24);
    snapScaleButton.setBounds (margin + 482, 344, 62, 24);
    humanizeSelectionButton.setBounds (margin + 552, 344, 82, 24);
    selectionLabel.setBounds (margin + 642, 344, 120, 24);

    duplicateButton.setBounds (margin + 12, 370, 52, 22);
    reverseButton.setBounds (margin + 70, 370, 52, 22);
    doubleTimeButton.setBounds (margin + 128, 370, 46, 22);
    halfTimeButton.setBounds (margin + 180, 370, 52, 22);
    rotateButton.setBounds (margin + 238, 370, 52, 22);
    normalizeVelocityButton.setBounds (margin + 296, 370, 76, 22);
    frameSelectionButton.setBounds (margin + 378, 370, 66, 22);
    historyLabel.setBounds (margin + 454, 369, 190, 23);
    undoBtn.setBounds (margin + 652, 370, 58, 22);
    redoBtn.setBounds (margin + 718, 370, 58, 22);
    clearBtn.setBounds (margin + 784, 370, 58, 22);

    pianoRoll.setBounds (margin + 12, 400, innerW - 24, 166);
    drumGrid.setBounds (margin + 12, 400, innerW - 24, 166);

    const int detailX = margin + 12;
    const int detailTop = 610;
    const int detailGap = 14;
    const int colW = (innerW - 24 - detailGap * 3) / 4;
    const int rowH = 31;

    auto sliderAt = [&] (juce::Slider& s, int col, int row)
    {
        const int sx = detailX + col * (colW + detailGap);
        const int sy = detailTop + row * rowH;
        s.setBounds (sx, sy + 11, colW, 20);
    };

    sliderAt (chordDensity, 0, 0);
    sliderAt (bassDensity,   0, 1);
    sliderAt (melodyDensity, 0, 2);
    sliderAt (arpDensity,    0, 3);

    sliderAt (motifStrength,   1, 0);
    sliderAt (variationAmount, 1, 1);
    sliderAt (fillAmount,      1, 2);
    sliderAt (energy,          1, 3);

    sliderAt (melodyLength, 2, 0);
    sliderAt (pauseChance,  2, 1);
    sliderAt (leapChance,   2, 2);
    sliderAt (ghostChance,  2, 3);

    sliderAt (swing,      3, 0);
    sliderAt (humanize,   3, 1);
    sliderAt (complexity, 3, 2);
    humanizeModeButton.setBounds (detailX + 3 * (colW + detailGap), detailTop + 3 * rowH + 8, colW, 24);

    const int footerY = 763;

    likeBtn.setBounds (margin + 12, footerY, 64, 28);
    dislikeBtn.setBounds (margin + 82, footerY, 78, 28);
    lockChordsBtn.setBounds (margin + 172, footerY, 86, 28);
    lockBassBtn.setBounds (margin + 266, footerY, 80, 28);
    lockMelodyBtn.setBounds (margin + 354, footerY, 92, 28);
    lockArpBtn.setBounds (margin + 454, footerY, 80, 28);
    mutateButton.setBounds (margin + 542, footerY, 72, 28);
    evolveButton.setBounds (margin + 622, footerY, 72, 28);
    tasteToggleBtn.setBounds (margin + 702, footerY, 90, 28);
    resetTasteBtn.setBounds (margin + 800, footerY, 58, 28);

    const int dragY = 803;
    dragChords.setBounds (margin + 12, dragY, 96, 25);
    dragBass.setBounds   (margin + 116, dragY, 88, 25);
    dragMelody.setBounds (margin + 212, dragY, 98, 25);
    dragArp.setBounds    (margin + 318, dragY, 84, 25);
    dragDrums.setBounds  (margin + 410, dragY, 92, 25);
    tasteLabel.setBounds  (margin + 514, dragY - 2, innerW - 590, 30);

    // Advanced controls stay within the card even at the minimum window size.
    historyLabel.toFront (false);
    selectionLabel.toFront (false);
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
