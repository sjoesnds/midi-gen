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
    if (dragStarted)
        return;
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
    setSize(900,980);

    title.setText("MIDI FORGE",juce::dontSendNotification);
    title.setFont (juce::Font (31.0f, juce::Font::bold));
    sectionLabel.setText("COMPOSITION ENGINE",juce::dontSendNotification);
    sectionLabel.setFont (juce::Font (12.0f));
    addAndMakeVisible(title);addAndMakeVisible(sectionLabel);

    root.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);
    root.setSelectedId(p.getRoot()+1); root.onChange=[this]{processor.setRoot(root.getSelectedId()-1);}; addAndMakeVisible(root);

    genre.addItemList({"Universal","Trap","House","Techno","Boom Bap","Ambient","Cinematic"},1);
    genre.setSelectedId(p.getGenre()+1); genre.onChange=[this]{processor.setGenre(genre.getSelectedId()-1);}; addAndMakeVisible(genre);

    scale.addItemList({"Major","Minor","Dorian","Phrygian","Harmonic Minor","Melodic Minor","Pentatonic"},1);
    scale.setSelectedId(p.getScale()+1); scale.onChange=[this]{processor.setScale(scale.getSelectedId()-1);}; addAndMakeVisible(scale);

    progression.addItemList({"Auto","Pop","Dark","Emotional","Cinematic","Jazz-like","Looping"},1);
    progression.setSelectedId(p.getProgression()+1); progression.onChange=[this]{processor.setProgression(progression.getSelectedId()-1);}; addAndMakeVisible(progression);

    rhythm.addItemList({"Straight","Syncopated","Broken","Euclidean"},1);
    rhythm.setSelectedId(p.getRhythm()+1); rhythm.onChange=[this]{processor.setRhythm(rhythm.getSelectedId()-1);}; addAndMakeVisible(rhythm);

    mode.addItemList({"Loop","Song","Song Extended"},1);
    mode.setSelectedId(p.getSectionMode()+1); mode.onChange=[this]{processor.setSectionMode(mode.getSelectedId()-1);}; addAndMakeVisible(mode);

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

    chords.setToggleState(p.isChordsEnabled(),juce::dontSendNotification);
    bass.setToggleState(p.isBassEnabled(),juce::dontSendNotification);
    melody.setToggleState(p.isMelodyEnabled(),juce::dontSendNotification);
    arp.setToggleState(p.isArpEnabled(),juce::dontSendNotification);
    extensions.setToggleState(p.getChordExtensions(),juce::dontSendNotification);
    inversions.setToggleState(p.getInversions(),juce::dontSendNotification);
    hookModeButton.setToggleState(p.getHookMode(),juce::dontSendNotification);

    chords.onClick=[this]{processor.setChordsEnabled(chords.getToggleState());};
    bass.onClick=[this]{processor.setBassEnabled(bass.getToggleState());};
    melody.onClick=[this]{processor.setMelodyEnabled(melody.getToggleState());};
    arp.onClick=[this]{processor.setArpEnabled(arp.getToggleState());};
    extensions.onClick=[this]{processor.setChordExtensions(extensions.getToggleState());};
    inversions.onClick=[this]{processor.setInversions(inversions.getToggleState());};
    hookModeButton.onClick=[this]{processor.setHookMode(hookModeButton.getToggleState());};

    addAndMakeVisible(chords);addAndMakeVisible(bass);addAndMakeVisible(melody);addAndMakeVisible(arp);
    addAndMakeVisible(extensions);addAndMakeVisible(inversions);addAndMakeVisible(hookModeButton);

    variationBox.addItemList({"VAR 1","VAR 2","VAR 3","VAR 4","VAR 5","VAR 6","VAR 7","VAR 8"},1);
    variationBox.setSelectedId(1); addAndMakeVisible(variationBox);
    variationBox.onChange=[this]{processor.chooseVariation(variationBox.getSelectedId()-1);};

    generate.setButtonText("GENERATE 8");
    newSeed.setButtonText("NEW SEED");
    applyVariation.setButtonText("USE VAR");
    exportMidi.setButtonText("EXPORT .MID");

    generate.onClick=[this]{processor.regenerateVariations();};
    newSeed.onClick=[this]{processor.setSeed(juce::Random::getSystemRandom().nextInt());};
    applyVariation.onClick=[this]{processor.chooseVariation(variationBox.getSelectedId()-1);};

    exportMidi.onClick=[this]{
        fileChooser = std::make_unique<juce::FileChooser>(
            "Export MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("MIDI_Forge_Song.mid"),
            "*.mid");
        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                const auto result = chooser.getResult();
                if (result == juce::File{})
                    return;
                const bool ok = processor.exportMidi(result);
                juce::AlertWindow::showMessageBoxAsync(
                    ok ? juce::MessageBoxIconType::InfoIcon
                       : juce::MessageBoxIconType::WarningIcon,
                    ok ? "MIDI Forge" : "MIDI Export Failed",
                    ok ? "MIDI exported successfully."
                       : "Could not write the MIDI file.",
                    "OK");
            });
    };

    addAndMakeVisible(generate);addAndMakeVisible(newSeed);addAndMakeVisible(applyVariation);addAndMakeVisible(exportMidi);

    exportButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save MIDI file", juce::File(), "*.mid");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File())
                    processor.exportMidiFileTo (file.withFileExtension ("mid"));
            });
    };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (dragHandle);
    addAndMakeVisible (pianoRoll);

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

    addAndMakeVisible(dragChords);addAndMakeVisible(dragBass);
    addAndMakeVisible(dragMelody);addAndMakeVisible(dragArp);

    startTimerHz (10);
}

void MidiForgeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d1015));
    g.setColour(juce::Colour(0xff252a33));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(12.f),12.f,1.f);
    g.setColour(juce::Colours::white);
    g.setFont(13.f);

    g.drawFittedText("SOURCE / HARMONY",24,108,820,20,juce::Justification::left,1);
    g.drawFittedText("PART DENSITY",24,196,820,20,juce::Justification::left,1);
    g.drawFittedText("MOTIF / ARRANGEMENT",24,336,820,20,juce::Justification::left,1);
    g.drawFittedText("MELODY",24,456,820,20,juce::Justification::left,1);
    g.drawFittedText("FEEL",24,574,820,20,juce::Justification::left,1);
    g.drawFittedText("PIANO ROLL",24,628,820,20,juce::Justification::left,1);
    g.drawFittedText("VARIATIONS",24,780,820,20,juce::Justification::left,1);
    g.drawFittedText("EXPORT",24,832,820,20,juce::Justification::left,1);
    g.drawFittedText("LOCK LAYERS (freeze while generating new variations)",24,876,820,20,juce::Justification::left,1);
    g.drawFittedText("DRAG SINGLE LAYER TO FL STUDIO",24,910,820,20,juce::Justification::left,1);
}

void MidiForgeAudioProcessorEditor::resized()
{
    title.setBounds(24,20,360,38);sectionLabel.setBounds(27,63,500,20);

    root.setBounds(24,130,70,28);genre.setBounds(103,130,110,28);scale.setBounds(222,130,125,28);
    progression.setBounds(356,130,120,28);rhythm.setBounds(485,130,110,28);
    mode.setBounds(604,130,100,28);bars.setBounds(713,130,52,28);octave.setBounds(774,130,50,28);

    chords.setBounds(24,158,88,24);bass.setBounds(116,158,84,24);melody.setBounds(204,158,98,24);
    arp.setBounds(306,158,75,24);extensions.setBounds(385,158,88,24);inversions.setBounds(477,158,75,24);
    arpRate.setBounds(560,158,65,24);
    hookModeButton.setBounds(632,158,75,24);

    chordDensity.setBounds(112,220,640,24);
    bassDensity.setBounds(112,252,640,24);
    melodyDensity.setBounds(112,284,640,24);
    arpDensity.setBounds(112,316,640,24);
    motifStrength.setBounds(112,360,640,24);
    variationAmount.setBounds(112,392,640,24);
    fillAmount.setBounds(112,424,640,24);
    energy.setBounds(112,456,640,24);
    melodyLength.setBounds(112,480,640,24);
    pauseChance.setBounds(112,512,640,24);
    leapChance.setBounds(112,544,640,24);
    ghostChance.setBounds(112,576,640,24);

    swing.setBounds(112,612,250,24);
    humanize.setBounds(390,612,250,24);
    complexity.setBounds(668,612,120,24);

    pianoRoll.setBounds(24,648,850,140);

    variationBox.setBounds(24,804,110,28);
    generate.setBounds(146,800,110,34);
    newSeed.setBounds(264,800,105,34);
    applyVariation.setBounds(377,800,95,34);
    exportMidi.setBounds(480,800,125,34);

    exportButton.setBounds(24,852,150,32);
    dragHandle.setBounds(184,852,170,32);

    lockChordsBtn.setBounds(24,896,140,24);
    lockBassBtn.setBounds(174,896,140,24);
    lockMelodyBtn.setBounds(324,896,140,24);
    lockArpBtn.setBounds(474,896,140,24);

    dragChords.setBounds(24,930,120,32);
    dragBass.setBounds(154,930,120,32);
    dragMelody.setBounds(284,930,120,32);
    dragArp.setBounds(414,930,120,32);
}

void MidiForgeAudioProcessorEditor::timerCallback()
{
    const int id = processor.getSelectedVariation() + 1;
    if (id >= 1 && variationBox.getSelectedId() != id)
        variationBox.setSelectedId (id, juce::dontSendNotification);
}
