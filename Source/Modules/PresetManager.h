#pragma once

#include <JuceHeader.h>
#include <array>
#include <string>

namespace presets
{

//==============================================================================
/** Preset data structure for saving/loading configurations */
struct PresetData
{
    // Musical settings
    int root = 0;
    int genre = 0;
    int scale = 0;
    int progression = 0;
    int rhythm = 0;
    int mood = 0;
    int melodyType = 0;
    int soundTarget = 0;
    
    // Densities
    float chordDensity = 0.9f;
    float bassDensity = 0.8f;
    float melodyDensity = 0.62f;
    float arpDensity = 0.25f;
    
    // Timing & feel
    float swing = 0.0f;
    float humanize = 0.15f;
    float complexity = 0.55f;
    
    // Melody parameters
    float melodyLength = 0.35f;
    float pauseChance = 0.10f;
    float leapChance = 0.18f;
    float ghostChance = 0.08f;
    
    // Harmony parameters
    float voicingWidth = 0.45f;
    float motifStrength = 0.78f;
    float variationAmount = 0.40f;
    float fillAmount = 0.18f;
    float energy = 0.65f;
    
    // Arp settings
    int arpRate = 4;
    
    // Flags
    bool chordExtensions = true;
    bool inversions = true;
    bool chordsEnabled = true;
    bool bassEnabled = true;
    bool melodyEnabled = true;
    bool arpEnabled = false;
    bool hookMode = true;
    bool drumsEnabled = false;
    
    // Articulation (0 off, 1 slides, 2 slides+vibrato)
    int articulation = 0;
    
    // Magic DNA
    float dnaMelody = 0.50f;
    float dnaRhythm = 0.50f;
    float dnaHarmony = 0.50f;
    float dnaMotif = 0.50f;
    float dnaRegister = 0.50f;
    float dnaGroove = 0.50f;
    float dnaEnergy = 0.50f;
    float dnaSurprise = 0.35f;
    
    // Layer locks
    bool lockChords = false;
    bool lockBass = false;
    bool lockMelody = false;
    bool lockArp = false;
    
    // Metadata
    juce::String name;
    juce::String description;
    juce::String tags; // Comma-separated tags
    
    bool operator==(const PresetData& other) const
    {
        return root == other.root && genre == other.genre && scale == other.scale &&
               progression == other.progression && rhythm == other.rhythm &&
               mood == other.mood && melodyType == other.melodyType &&
               soundTarget == other.soundTarget &&
               chordDensity == other.chordDensity && bassDensity == other.bassDensity &&
               melodyDensity == other.melodyDensity && arpDensity == other.arpDensity &&
               swing == other.swing && humanize == other.humanize &&
               complexity == other.complexity && melodyLength == other.melodyLength &&
               pauseChance == other.pauseChance && leapChance == other.leapChance &&
               ghostChance == other.ghostChance && voicingWidth == other.voicingWidth &&
               motifStrength == other.motifStrength && variationAmount == other.variationAmount &&
               fillAmount == other.fillAmount && energy == other.energy &&
               arpRate == other.arpRate && chordExtensions == other.chordExtensions &&
               inversions == other.inversions && chordsEnabled == other.chordsEnabled &&
               bassEnabled == other.bassEnabled && melodyEnabled == other.melodyEnabled &&
               arpEnabled == other.arpEnabled && hookMode == other.hookMode &&
               drumsEnabled == other.drumsEnabled && articulation == other.articulation &&
               dnaMelody == other.dnaMelody && dnaRhythm == other.dnaRhythm &&
               dnaHarmony == other.dnaHarmony && dnaMotif == other.dnaMotif &&
               dnaRegister == other.dnaRegister && dnaGroove == other.dnaGroove &&
               dnaEnergy == other.dnaEnergy && dnaSurprise == other.dnaSurprise &&
               lockChords == other.lockChords && lockBass == other.lockBass &&
               lockMelody == other.lockMelody && lockArp == other.lockArp;
    }
};

//==============================================================================
/** Manages preset storage and retrieval */
class PresetManager
{
public:
    PresetManager()
    {
        // Initialize with factory presets
        initializeFactoryPresets();
    }
    
    /** Returns the user presets directory */
    static juce::File getUserPresetsDirectory()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("MidiForge").getChildFile("Presets");
        
        if (!dir.exists())
            dir.createDirectory();
        
        return dir;
    }
    
    /** Saves a preset to file */
    bool savePreset(const PresetData& preset)
    {
        auto file = getPresetFile(preset.name);
        if (file.isEmpty())
            return false;
        
        juce::XmlElement xml("Preset");
        fillXmlElement(xml, preset);
        
        return xml.writeTo(file);
    }
    
    /** Loads a preset from file */
    std::unique_ptr<PresetData> loadPreset(const juce::String& name)
    {
        auto file = getPresetFile(name);
        if (!file.existsAsFile())
            return nullptr;
        
        auto xml = juce::XmlDocument::parse(file);
        if (!xml || !xml->hasTagName("Preset"))
            return nullptr;
        
        auto preset = std::make_unique<PresetData>();
        parseXmlElement(*xml, *preset);
        
        return preset;
    }
    
    /** Deletes a preset */
    bool deletePreset(const juce::String& name)
    {
        auto file = getPresetFile(name);
        if (file.existsAsFile())
            return file.deleteFile();
        return false;
    }
    
    /** Lists all available presets */
    juce::StringArray listPresets() const
    {
        juce::StringArray names;
        
        // Add factory presets
        for (const auto& fp : factoryPresets)
            names.add(fp.name);
        
        // Add user presets
        auto dir = getUserPresetsDirectory();
        if (dir.exists())
        {
            auto files = dir.findChildFiles(
                juce::File::findFiles, false, "*.xml");
            
            for (const auto& f : files)
            {
                auto name = f.getFileNameWithoutExtension();
                if (!names.contains(name))
                    names.add(name);
            }
        }
        
        return names;
    }
    
    /** Gets a factory preset by name */
    const PresetData* getFactoryPreset(const juce::String& name) const
    {
        for (const auto& fp : factoryPresets)
        {
            if (fp.name == name)
                return &fp;
        }
        return nullptr;
    }
    
    /** Exports preset to JSON string */
    static juce::String exportToJson(const PresetData& preset)
    {
        juce::var json;
        auto* obj = new juce::DynamicObject();
        
        obj->setProperty("name", preset.name);
        obj->setProperty("description", preset.description);
        obj->setProperty("root", preset.root);
        obj->setProperty("genre", preset.genre);
        obj->setProperty("scale", preset.scale);
        obj->setProperty("chordDensity", preset.chordDensity);
        obj->setProperty("melodyDensity", preset.melodyDensity);
        obj->setProperty("swing", preset.swing);
        obj->setProperty("humanize", preset.humanize);
        obj->setProperty("energy", preset.energy);
        obj->setProperty("dnaMelody", preset.dnaMelody);
        obj->setProperty("dnaRhythm", preset.dnaRhythm);
        obj->setProperty("dnaHarmony", preset.dnaHarmony);
        
        json = juce::var(obj);
        return json.toString();
    }
    
    /** Imports preset from JSON string */
    static std::unique_ptr<PresetData> importFromJson(const juce::String& jsonString)
    {
        auto json = juce::JSON::parse(jsonString);
        if (!json.isObject())
            return nullptr;
        
        auto preset = std::make_unique<PresetData>();
        auto* obj = json.getDynamicObject();
        
        preset->name = obj->getProperty("name", "Imported").toString();
        preset->description = obj->getProperty("description", "").toString();
        preset->root = static_cast<int>(obj->getProperty("root", 0));
        preset->genre = static_cast<int>(obj->getProperty("genre", 0));
        preset->scale = static_cast<int>(obj->getProperty("scale", 0));
        preset->chordDensity = static_cast<float>(obj->getProperty("chordDensity", 0.9f));
        preset->melodyDensity = static_cast<float>(obj->getProperty("melodyDensity", 0.6f));
        preset->swing = static_cast<float>(obj->getProperty("swing", 0.0f));
        preset->humanize = static_cast<float>(obj->getProperty("humanize", 0.15f));
        preset->energy = static_cast<float>(obj->getProperty("energy", 0.5f));
        preset->dnaMelody = static_cast<float>(obj->getProperty("dnaMelody", 0.5f));
        preset->dnaRhythm = static_cast<float>(obj->getProperty("dnaRhythm", 0.5f));
        preset->dnaHarmony = static_cast<float>(obj->getProperty("dnaHarmony", 0.5f));
        
        return preset;
    }
    
private:
    std::vector<PresetData> factoryPresets;
    
    void initializeFactoryPresets()
    {
        // Trap Banger
        {
            PresetData p;
            p.name = "Trap Banger";
            p.description = "Hard-hitting trap with 808s and hi-hat rolls";
            p.genre = 1; // Trap
            p.energy = 0.85f;
            p.dnaRhythm = 0.8f;
            p.dnaEnergy = 0.85f;
            p.drumsEnabled = true;
            p.bassEnabled = true;
            p.melodyEnabled = true;
            p.chordsEnabled = false;
            factoryPresets.push_back(p);
        }
        
        // Ambient Dreamscape
        {
            PresetData p;
            p.name = "Ambient Dreamscape";
            p.description = "Ethereal pads and gentle arpeggios";
            p.genre = 5; // Ambient
            p.energy = 0.3f;
            p.dnaMelody = 0.3f;
            p.dnaHarmony = 0.7f;
            p.dnaRegister = 0.7f;
            p.soundTarget = 4; // Pad/Strings
            p.chordsEnabled = true;
            p.bassEnabled = false;
            p.melodyEnabled = false;
            p.arpEnabled = true;
            factoryPresets.push_back(p);
        }
        
        // House Groove
        {
            PresetData p;
            p.name = "House Groove";
            p.description = "Four-on-the-floor house with funky bass";
            p.genre = 2; // House
            p.energy = 0.75f;
            p.swing = 0.1f;
            p.dnaRhythm = 0.6f;
            p.dnaGroove = 0.7f;
            p.drumsEnabled = true;
            p.bassEnabled = true;
            p.chordsEnabled = true;
            factoryPresets.push_back(p);
        }
        
        // Lofi Chill
        {
            PresetData p;
            p.name = "Lofi Chill";
            p.description = "Relaxed beats with jazzy chords";
            p.genre = 15; // Lofi
            p.energy = 0.4f;
            p.swing = 0.3f;
            p.humanize = 0.25f;
            p.dnaMelody = 0.4f;
            p.dnaHarmony = 0.6f;
            p.chordExtensions = true;
            p.chordsEnabled = true;
            p.bassEnabled = true;
            p.melodyEnabled = true;
            factoryPresets.push_back(p);
        }
        
        // Cinematic Epic
        {
            PresetData p;
            p.name = "Cinematic Epic";
            p.description = "Epic orchestral hybrid for trailers";
            p.genre = 6; // Cinematic
            p.energy = 0.9f;
            p.dnaHarmony = 0.8f;
            p.dnaEnergy = 0.9f;
            p.dnaRegister = 0.5f;
            p.soundTarget = 5; // Brass
            p.chordsEnabled = true;
            p.bassEnabled = true;
            p.melodyEnabled = true;
            factoryPresets.push_back(p);
        }
    }
    
    juce::File getPresetFile(const juce::String& name) const
    {
        if (name.isEmpty())
            return {};
        
        // Check if it's a factory preset
        for (const auto& fp : factoryPresets)
        {
            if (fp.name == name)
                return {}; // Factory presets are read-only
        }
        
        // Return user preset file path
        return getUserPresetsDirectory().getChildFile(name + ".xml");
    }
    
    void fillXmlElement(juce::XmlElement& xml, const PresetData& preset)
    {
        xml.setAttribute("name", preset.name);
        xml.setAttribute("description", preset.description);
        xml.setAttribute("root", preset.root);
        xml.setAttribute("genre", preset.genre);
        xml.setAttribute("scale", preset.scale);
        xml.setAttribute("progression", preset.progression);
        xml.setAttribute("rhythm", preset.rhythm);
        xml.setAttribute("mood", preset.mood);
        xml.setAttribute("melodyType", preset.melodyType);
        xml.setAttribute("soundTarget", preset.soundTarget);
        xml.setAttribute("chordDensity", preset.chordDensity);
        xml.setAttribute("bassDensity", preset.bassDensity);
        xml.setAttribute("melodyDensity", preset.melodyDensity);
        xml.setAttribute("arpDensity", preset.arpDensity);
        xml.setAttribute("swing", preset.swing);
        xml.setAttribute("humanize", preset.humanize);
        xml.setAttribute("complexity", preset.complexity);
        xml.setAttribute("melodyLength", preset.melodyLength);
        xml.setAttribute("pauseChance", preset.pauseChance);
        xml.setAttribute("leapChance", preset.leapChance);
        xml.setAttribute("ghostChance", preset.ghostChance);
        xml.setAttribute("voicingWidth", preset.voicingWidth);
        xml.setAttribute("motifStrength", preset.motifStrength);
        xml.setAttribute("variationAmount", preset.variationAmount);
        xml.setAttribute("fillAmount", preset.fillAmount);
        xml.setAttribute("energy", preset.energy);
        xml.setAttribute("arpRate", preset.arpRate);
        xml.setAttribute("chordExtensions", preset.chordExtensions);
        xml.setAttribute("inversions", preset.inversions);
        xml.setAttribute("chordsEnabled", preset.chordsEnabled);
        xml.setAttribute("bassEnabled", preset.bassEnabled);
        xml.setAttribute("melodyEnabled", preset.melodyEnabled);
        xml.setAttribute("arpEnabled", preset.arpEnabled);
        xml.setAttribute("hookMode", preset.hookMode);
        xml.setAttribute("drumsEnabled", preset.drumsEnabled);
        xml.setAttribute("articulation", preset.articulation);
        xml.setAttribute("dnaMelody", preset.dnaMelody);
        xml.setAttribute("dnaRhythm", preset.dnaRhythm);
        xml.setAttribute("dnaHarmony", preset.dnaHarmony);
        xml.setAttribute("dnaMotif", preset.dnaMotif);
        xml.setAttribute("dnaRegister", preset.dnaRegister);
        xml.setAttribute("dnaGroove", preset.dnaGroove);
        xml.setAttribute("dnaEnergy", preset.dnaEnergy);
        xml.setAttribute("dnaSurprise", preset.dnaSurprise);
        xml.setAttribute("lockChords", preset.lockChords);
        xml.setAttribute("lockBass", preset.lockBass);
        xml.setAttribute("lockMelody", preset.lockMelody);
        xml.setAttribute("lockArp", preset.lockArp);
    }
    
    void parseXmlElement(const juce::XmlElement& xml, PresetData& preset)
    {
        preset.name = xml.getStringAttribute("name", "Unnamed");
        preset.description = xml.getStringAttribute("description", "");
        preset.root = xml.getIntAttribute("root", 0);
        preset.genre = xml.getIntAttribute("genre", 0);
        preset.scale = xml.getIntAttribute("scale", 0);
        preset.progression = xml.getIntAttribute("progression", 0);
        preset.rhythm = xml.getIntAttribute("rhythm", 0);
        preset.mood = xml.getIntAttribute("mood", 0);
        preset.melodyType = xml.getIntAttribute("melodyType", 0);
        preset.soundTarget = xml.getIntAttribute("soundTarget", 0);
        preset.chordDensity = static_cast<float>(xml.getDoubleAttribute("chordDensity", 0.9));
        preset.bassDensity = static_cast<float>(xml.getDoubleAttribute("bassDensity", 0.8));
        preset.melodyDensity = static_cast<float>(xml.getDoubleAttribute("melodyDensity", 0.62));
        preset.arpDensity = static_cast<float>(xml.getDoubleAttribute("arpDensity", 0.25));
        preset.swing = static_cast<float>(xml.getDoubleAttribute("swing", 0.0));
        preset.humanize = static_cast<float>(xml.getDoubleAttribute("humanize", 0.15));
        preset.complexity = static_cast<float>(xml.getDoubleAttribute("complexity", 0.55));
        preset.melodyLength = static_cast<float>(xml.getDoubleAttribute("melodyLength", 0.35));
        preset.pauseChance = static_cast<float>(xml.getDoubleAttribute("pauseChance", 0.10));
        preset.leapChance = static_cast<float>(xml.getDoubleAttribute("leapChance", 0.18));
        preset.ghostChance = static_cast<float>(xml.getDoubleAttribute("ghostChance", 0.08));
        preset.voicingWidth = static_cast<float>(xml.getDoubleAttribute("voicingWidth", 0.45));
        preset.motifStrength = static_cast<float>(xml.getDoubleAttribute("motifStrength", 0.78));
        preset.variationAmount = static_cast<float>(xml.getDoubleAttribute("variationAmount", 0.40));
        preset.fillAmount = static_cast<float>(xml.getDoubleAttribute("fillAmount", 0.18));
        preset.energy = static_cast<float>(xml.getDoubleAttribute("energy", 0.65));
        preset.arpRate = xml.getIntAttribute("arpRate", 4);
        preset.chordExtensions = xml.getBoolAttribute("chordExtensions", true);
        preset.inversions = xml.getBoolAttribute("inversions", true);
        preset.chordsEnabled = xml.getBoolAttribute("chordsEnabled", true);
        preset.bassEnabled = xml.getBoolAttribute("bassEnabled", true);
        preset.melodyEnabled = xml.getBoolAttribute("melodyEnabled", true);
        preset.arpEnabled = xml.getBoolAttribute("arpEnabled", false);
        preset.hookMode = xml.getBoolAttribute("hookMode", true);
        preset.drumsEnabled = xml.getBoolAttribute("drumsEnabled", false);
        preset.articulation = xml.getIntAttribute("articulation", 0);
        preset.dnaMelody = static_cast<float>(xml.getDoubleAttribute("dnaMelody", 0.5));
        preset.dnaRhythm = static_cast<float>(xml.getDoubleAttribute("dnaRhythm", 0.5));
        preset.dnaHarmony = static_cast<float>(xml.getDoubleAttribute("dnaHarmony", 0.5));
        preset.dnaMotif = static_cast<float>(xml.getDoubleAttribute("dnaMotif", 0.5));
        preset.dnaRegister = static_cast<float>(xml.getDoubleAttribute("dnaRegister", 0.5));
        preset.dnaGroove = static_cast<float>(xml.getDoubleAttribute("dnaGroove", 0.5));
        preset.dnaEnergy = static_cast<float>(xml.getDoubleAttribute("dnaEnergy", 0.5));
        preset.dnaSurprise = static_cast<float>(xml.getDoubleAttribute("dnaSurprise", 0.35));
        preset.lockChords = xml.getBoolAttribute("lockChords", false);
        preset.lockBass = xml.getBoolAttribute("lockBass", false);
        preset.lockMelody = xml.getBoolAttribute("lockMelody", false);
        preset.lockArp = xml.getBoolAttribute("lockArp", false);
    }
};

} // namespace presets
