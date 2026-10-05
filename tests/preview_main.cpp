// MIDI Forge - fast offline musical preview.
// Generates a ~75-second fixed-context mood comparison without FL Studio/Serum.

#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double bpm = 120.0;
constexpr double stepSeconds = 60.0 / bpm / 4.0;
constexpr double sampleRate = 44100.0;
constexpr int tailSamples = 22050;

struct Segment
{
    int mood = 0;
    std::vector<MidiForgeAudioProcessor::VisibleNote> notes;
    int bars = 4;
};

double hz (int midi)
{
    return 440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0);
}

float env (double t, double length)
{
    const double attack = 0.012;
    const double release = std::min (0.08, std::max (0.015, length * 0.3));
    if (t < attack) return (float) (t / attack);
    if (t > length - release) return (float) std::max (0.0, (length - t) / release);
    return 1.0f;
}

float osc (double phase, int channel)
{
    const double s = std::sin (phase);
    if (channel == 2)
        return (float) (0.72 * s + 0.28 * std::sin (phase * 2.0));
    if (channel == 1)
        return (float) (0.65 * s + 0.25 * std::sin (phase * 2.0) + 0.10 * std::sin (phase * 3.0));
    return (float) (0.82 * s + 0.12 * std::sin (phase * 2.0) + 0.06 * std::sin (phase * 3.0));
}

void renderSegment (juce::AudioBuffer<float>& dst, int index, const Segment& segment, int segmentSamples)
{
    const int offset = index * segmentSamples;
    const int maxStep = segment.bars * 16;

    for (const auto& n : segment.notes)
    {
        if (n.step < 0 || n.step >= maxStep || n.note < 0 || n.note > 127)
            continue;
        if (n.channel != 1 && n.channel != 2 && n.channel != 3)
            continue;

        const double start = (double) n.step * stepSeconds;
        const double length = std::max (0.025, (double) std::max (1, n.length) * stepSeconds);
        const int begin = offset + (int) std::floor (start * sampleRate);
        const int end = std::min (offset + segmentSamples,
                                  begin + (int) std::ceil (length * sampleRate));

        float gain = n.channel == 3 ? 0.18f : (n.channel == 2 ? 0.10f : 0.045f);
        gain *= (float) juce::jlimit (0.55, 1.0, (double) n.velocity / 127.0);

        const double frequency = hz (n.note);
        for (int s = begin; s < end; ++s)
        {
            const double t = (double) (s - begin) / sampleRate;
            const float value = gain * env (t, length)
                * osc (2.0 * juce::MathConstants<double>::pi * frequency * t, n.channel);

            for (int ch = 0; ch < dst.getNumChannels(); ++ch)
                dst.addSample (ch, s, value);
        }
    }
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer)
{
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (stream == nullptr || ! stream->openedOk())
        return false;

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (stream.get(), sampleRate, (unsigned) buffer.getNumChannels(), 16, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
}

int main (int argc, char** argv)
{
    const juce::File out = argc > 1
        ? juce::File (juce::String::fromUTF8 (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("mood-preview");
    out.createDirectory();

    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("midiforge_preview_" + juce::String (juce::Time::currentTimeMillis()));
    settings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (settings);

    static constexpr const char* names[9] =
    {
        "neutral", "dark", "melancholic", "euphoric", "aggressive",
        "dreamy", "nostalgic", "mysterious", "energetic"
    };

    std::vector<Segment> segments;
    std::ofstream csv (out.getChildFile ("mood-report.csv").getFullPathName().toStdString(),
                       std::ios::out | std::ios::trunc);
    csv << "mood,melody_notes,median_pitch,mean_abs_interval,unique_pitches\n";

    for (int mood = 0; mood < 9; ++mood)
    {
        MidiForgeAudioProcessor p;
        p.setFeedbackLogFile (juce::File());
        p.setAsyncGeneration (false);

        // Same musical context for every mood: C minor, Universal, Hook, same seed.
        p.setRoot (0);
        p.setGenre (MidiForgeAudioProcessor::Universal);
        p.setScale (MidiForgeAudioProcessor::Minor);
        p.setMelodyType (MidiForgeAudioProcessor::HookMelody);
        p.setBars (4);
        p.setComplexity (0.55f, false);
        p.setEnergy (0.60f, false);
        p.setSeed (51000);
        p.setMood (mood);
        p.regenerate();
        p.chooseVariation (0);

        Segment segment;
        segment.mood = mood;
        segment.bars = std::max (1, p.getVisibleBars());

        for (const auto& n : p.getVisibleNotes())
            if (n.channel == 1 || n.channel == 2 || n.channel == 3)
                segment.notes.push_back (n);

        std::stable_sort (segment.notes.begin(), segment.notes.end(),
            [] (const auto& a, const auto& b)
            {
                if (a.step != b.step) return a.step < b.step;
                if (a.channel != b.channel) return a.channel < b.channel;
                return a.note < b.note;
            });

        std::vector<int> pitches;
        double intervalSum = 0.0;
        int previous = -1;
        int melodyCount = 0;

        for (const auto& n : segment.notes)
        {
            if (n.channel != 3) continue;
            ++melodyCount;
            pitches.push_back (n.note);
            if (previous >= 0) intervalSum += std::abs (n.note - previous);
            previous = n.note;
        }

        std::sort (pitches.begin(), pitches.end());
        pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());

        const double medianPitch = pitches.empty() ? 0.0 : (double) pitches[pitches.size() / 2];
        const double meanInterval = melodyCount > 1 ? intervalSum / (double) (melodyCount - 1) : 0.0;

        csv << names[mood] << ","
            << melodyCount << ","
            << medianPitch << ","
            << meanInterval << ","
            << pitches.size() << "\n";

        juce::MidiFile midi = p.buildMidiFile();
        juce::FileOutputStream midiOut (
            out.getChildFile (juce::String::formatted ("%02d-%s.mid", mood + 1, names[mood])));
        if (midiOut.openedOk())
            midi.writeTo (midiOut);

        segments.push_back (std::move (segment));
    }

    const int segmentSamples = (int) std::ceil (4.0 * 16.0 * stepSeconds * sampleRate) + tailSamples;
    juce::AudioBuffer<float> suite (2, segmentSamples * (int) segments.size());
    suite.clear();

    for (size_t i = 0; i < segments.size(); ++i)
        renderSegment (suite, (int) i, segments[i], segmentSamples);

    if (! writeWav (out.getChildFile ("mood-suite.wav"), suite))
    {
        std::fprintf (stderr, "Could not write mood-suite.wav\n");
        return 2;
    }

    std::printf ("Generated %d fixed-context mood previews in %s\n", (int) segments.size(),
                 out.getFullPathName().toRawUTF8());
    std::printf ("Context: C minor / Universal / Hook / seed 51000 / 120 BPM\n");
    std::printf ("Main file: mood-suite.wav\n");
    return 0;
}
