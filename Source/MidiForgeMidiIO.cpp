#include "PluginProcessor.h"
#include "MidiForgeShared.h"

#include <algorithm>
#include <cmath>

juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForDrumRow(int row) const
{
    auto file=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("MidiForge_"+juce::String(row<0 ? "Drums" : drumRowName(row))+"_"
                      +juce::String(juce::Random::getSystemRandom().nextInt())+".mid");
    auto midiFile=buildMidiFile(5,row);
    if(auto stream=file.createOutputStream())
    {
        if (midiFile.writeTo(*stream) && file.existsAsFile() && file.getSize() > 0)
            return file;
    }
    return {};
}

std::vector<MidiForgeAudioProcessor::ArtInfo> MidiForgeAudioProcessor::articulationFor (const std::vector<NoteEvent>& notes) const
{
    std::vector<ArtInfo> art (notes.size());
    const auto prof = soundProfileFor (soundTarget);
    if (articulation <= 0 || (prof.slideChance <= 0.0f && prof.vibChance <= 0.0f)) return art;
    std::vector<size_t> idx;
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].channel == 3) idx.push_back (i);
    std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return notes[a].step < notes[b].step; });
    for (size_t k = 0; k < idx.size(); ++k)
    {
        const auto& cur = notes[idx[k]];
        const uint32_t hsh = hash32 (generationSeed ^ (uint32_t) (cur.step * 131 + cur.note * 17 + 5));
        if (k + 1 < idx.size())
        {
            const auto& nx = notes[idx[k + 1]];
            const int gap = nx.step - cur.step;
            const int iv = std::abs (nx.note - cur.note);
            // legato slide: the note runs on into the next one (touching notes, different pitch, no big leap)
            if (gap > 0 && gap - cur.length <= 1 && iv >= 1 && iv <= 7
                && (float) (hsh % 1000u) / 1000.0f < prof.slideChance)
            {
                art[idx[k]].slide = true;
                art[idx[k]].slideToStep = nx.step;
            }
        }
        if (articulation >= 2 && cur.length >= 4
            && (float) (hash32 (hsh ^ 0x51ed270bu) % 1000u) / 1000.0f < prof.vibChance)
            art[idx[k]].vib = true;
    }
    return art;
}

void MidiForgeAudioProcessor::addArticulation (juce::MidiMessageSequence& track, const ArtInfo& a, int channel,
                                               double onTick, double& offTick, double ticksPerStep) const
{
    if (a.slide && a.slideToStep >= 0)
        offTick = juce::jmax (offTick, (double) (a.slideToStep + 1) * ticksPerStep);   // overlap the next note by one step
    if (a.vib)
    {
        // delayed vibrato on the mod wheel (CC1): 0 -> 45 -> 85 -> 0
        const double dur = offTick - onTick;
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 0), onTick);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 45), onTick + 0.35 * dur);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 85), onTick + 0.60 * dur);
        track.addEvent (juce::MidiMessage::controllerEvent (channel, 1, 0), offTick);
    }
}

juce::MidiFile MidiForgeAudioProcessor::buildMidiFile (int channelFilter, int drumRow) const
{
juce::MidiFile midiFile;
constexpr int ticksPerQuarter = 960;
midiFile.setTicksPerQuarterNote (ticksPerQuarter);
const int ticksPerStep = ticksPerQuarter / 4;
Section pattern;
{
    const juce::ScopedLock sl (variationsLock);
    if (variations.empty())
        return midiFile;
    pattern = variations[(size_t) juce::jlimit (0, (int) variations.size() - 1, selectedVariation)];
}
static const char* trackNames[6] = { "", "Chords", "Bass", "Melody", "Arp", "Drums" };
const auto art = articulationFor (pattern.notes);
for (int channel = 1; channel <= 5; ++channel)
{
if (channelFilter != 0 && channel != channelFilter) continue;
const int midiCh = (channel == 5) ? 10 : channel;      // drums = General MIDI channel 10
if (channel == 5 && std::none_of (pattern.notes.begin(), pattern.notes.end(), [] (const NoteEvent& q) { return q.channel == 5; })) continue;
if (channel == 5)
{
    // one track per drum instrument (kick / snare / hat ...), so each can go to its own sampler
    for (int row = 0; row < kDrumRows; ++row)
    {
        if (drumRow >= 0 && row != drumRow) continue;
        if (drumRow < 0 && (drumMuteMask & (1 << row)) != 0) continue;
        juce::MidiMessageSequence dtrack;
        dtrack.addEvent (juce::MidiMessage::textMetaEvent (3, drumRowName (row)), 0.0);
        bool any = false;
        for (const auto& n : pattern.notes)
        {
            if (n.channel != 5 || drumRowForNote (n.note) != row) continue;
            any = true;
            const double onTick = n.step * (double) ticksPerStep + swingTicks (n.step, (double) ticksPerStep);
            const double offTick = swungEndTick (n.step, juce::jmax (1, n.length), (double) ticksPerStep);
            const int pitch = drumOutPitch (row, n.note);
            dtrack.addEvent (juce::MidiMessage::noteOn (10, pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), onTick);
            dtrack.addEvent (juce::MidiMessage::noteOff (10, pitch), offTick);
        }
        if (any) { dtrack.updateMatchedPairs(); midiFile.addTrack (dtrack); }
    }
    continue;
}
juce::MidiMessageSequence track;
track.addEvent (juce::MidiMessage::textMetaEvent (3, trackNames[channel]), 0.0);
for (size_t ni = 0; ni < pattern.notes.size(); ++ni)
{
const auto& n = pattern.notes[ni];
if (n.channel != channel) continue;
const double onTick  = n.step * (double) ticksPerStep + swingTicks (n.step, (double) ticksPerStep);
double offTick = swungEndTick (n.step, juce::jmax (1, n.length), (double) ticksPerStep);
addArticulation (track, art[ni], midiCh, onTick, offTick, (double) ticksPerStep);
auto on = juce::MidiMessage::noteOn (midiCh, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity));
auto off = juce::MidiMessage::noteOff (midiCh, n.note);
track.addEvent (on, onTick);
track.addEvent (off, offTick);
}
track.updateMatchedPairs();
midiFile.addTrack (track);
}
return midiFile;
}

bool MidiForgeAudioProcessor::exportMidiFileTo (const juce::File& file) const
{
    auto midiFile = buildMidiFile (0);
    file.deleteFile();
    if (auto stream = file.createOutputStream())
    {
        const bool written = midiFile.writeTo (*stream);
        return written && file.existsAsFile() && file.getSize() > 0;
    }
    return false;
}

bool MidiForgeAudioProcessor::exportMidiFileToChannel (const juce::File& file, int channel) const
{
    auto midiFile = buildMidiFile (channel);
    file.deleteFile();
    if (auto stream = file.createOutputStream())
    {
        const bool written = midiFile.writeTo (*stream);
        return written && file.existsAsFile() && file.getSize() > 0;
    }
    return false;
}

juce::File MidiForgeAudioProcessor::writeTemporaryMidiFile() const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("MidiForge_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
    if (! exportMidiFileTo (file) || ! file.existsAsFile() || file.getSize() <= 0)
        return {};
    logFeedback (-1, "drag_all");
    return file;
}

juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForChannel (int channel) const
{
    static const char* names[6] = { "All", "Chords", "Bass", "Melody", "Arp", "Drums" };
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("MidiForge_" + juce::String (names[juce::jlimit (0, 5, channel)])
                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
    if (! exportMidiFileToChannel (file, channel) || ! file.existsAsFile() || file.getSize() <= 0)
        return {};
    logFeedback (-1, "drag_part");
    return file;
}
