// MIDI Forge - automatic quality checks (runs headless in CI, see .github/workflows/quality.yml)
//
// The plugin is a generator, so "does it still sound right" is checked on the MIDI it produces:
//   1. invariants that must hold for EVERY loop (full triads, bass anchor, one-line melody, ...)
//   2. statistics over many MAGIC loops (density, smoothness, hook repetition, groove variety, ...)
//   3. every Sound profile really behaves differently (pluck is short, pad is sparse, 808 has no bass, ...)
//   4. Articulation (slides = overlapping notes, vibrato = CC1) only where it should be
//   5. Taste ML actually learns (simulated user) and survives save / load / reset
//   6. project state round-trip, old-state compatibility, AUTO-NEXT
// Statistical checks get one retry with fresh loops (MAGIC is random); invariants never retry.
#include "PluginProcessor.h"
#include "MelodyIntent.h"
#include "RhythmGrammar.h"
#include "ComposerGrammar.h"
#include "MelodicProsody.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Note = MidiForgeAudioProcessor::VisibleNote;

namespace
{
    int failures = 0;

    void report (const std::string& name, bool ok, const std::string& detail)
    {
        std::printf ("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
        if (! ok) ++failures;
    }
    std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
    {
        char buf[512];
        std::snprintf (buf, sizeof buf, f, a, b, c, d);
        return buf;
    }

    std::string fmt7 (const char* f,
                      double a, double b, double c, double d,
                      double e, double g, double h)
    {
        char buf[512];
        std::snprintf (buf, sizeof buf, f, a, b, c, d, e, g, h);
        return buf;
    }

    struct Loop { std::vector<Note> notes; int bars = 1; };

    std::vector<Loop> makeLoops (MidiForgeAudioProcessor& p, int n)
    {
        std::vector<Loop> out;
        for (int i = 0; i < n; ++i)
        {
            p.magicRandomize();
            out.push_back ({ p.getVisibleNotes(), std::max (1, p.getVisibleBars()) });
        }
        return out;
    }

    std::vector<Note> layer (const Loop& l, int ch)
    {
        std::vector<Note> v;
        for (auto& n : l.notes) if (n.channel == ch) v.push_back (n);
        std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
        return v;
    }
    double mean (const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / (double) v.size(); }
    double median (std::vector<int> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }

    struct Stats
    {
        double melPerBar = 0, medianInterval = 0, shareGE12 = 0, shareStep = 0, coverage = 0, meanLen = 0, medianPitch = 0;
        double rhythmRepeat = 0, topPatternShare = 0, plainTriad = 0, clusterPerBar = 0, bassAnchor = 0;
        int loops = 0, bassNotes = 0;
    };

    Stats analyse (const std::vector<Loop>& loops)
    {
        Stats s; s.loops = (int) loops.size();
        std::vector<double> perBar, cov, len, rep, plain, cluster, anchor;
        std::vector<int> intervals, pitches;
        std::map<std::vector<int>, int> patterns; int patternBars = 0;
        for (auto& l : loops)
        {
            auto mel = layer (l, 3); auto ch = layer (l, 1); auto ba = layer (l, 2);
            s.bassNotes += (int) ba.size();
            perBar.push_back ((double) mel.size() / l.bars);
            std::vector<bool> covered ((size_t) l.bars * 16, false);
            for (auto& n : mel)
            {
                len.push_back (n.length); pitches.push_back (n.note);
                for (int t = n.step; t < n.step + std::max (1, n.length) && t < l.bars * 16; ++t) if (t >= 0) covered[(size_t) t] = true;
            }
            cov.push_back ((double) std::count (covered.begin(), covered.end(), true) / (double) covered.size());
            for (size_t i = 1; i < mel.size(); ++i) intervals.push_back (std::abs (mel[i].note - mel[i - 1].note));
            std::vector<std::vector<int>> sig ((size_t) l.bars);
            for (auto& n : mel) sig[(size_t) std::min (l.bars - 1, n.step / 16)].push_back (n.step % 16);
            for (auto& b : sig) if (! b.empty()) { ++patterns[b]; ++patternBars; }
            if (l.bars >= 4)
            {
                int r = 0, c = 0;
                for (int b = 1; b < l.bars; ++b)
                {
                    if (sig[(size_t) b].empty()) continue;
                    ++c;
                    for (int a = 0; a < b; ++a) if (sig[(size_t) a] == sig[(size_t) b]) { ++r; break; }
                }
                if (c) rep.push_back ((double) r / c);
            }
            if (! ch.empty())
                for (int b = 0; b < l.bars; ++b)
                {
                    std::set<int> pcs; std::vector<int> ps;
                    int first = 1 << 30;
                    for (auto& n : ch) if (n.step / 16 == b) first = std::min (first, n.step);
                    for (auto& n : ch) if (n.step == first) { pcs.insert (n.note % 12); ps.push_back (n.note); }
                    if (pcs.size() < 3) continue;
                    bool ok = false;
                    for (int r : pcs) if ((pcs.count ((r + 4) % 12) || pcs.count ((r + 3) % 12)) && pcs.count ((r + 7) % 12)) ok = true;
                    plain.push_back (ok ? 1.0 : 0.0);
                    std::sort (ps.begin(), ps.end()); int cl = 0;
                    for (size_t i = 1; i < ps.size(); ++i) if (ps[i] - ps[i - 1] == 1) ++cl;
                    cluster.push_back (cl);
                }
            if (! ba.empty())
                for (int b = 0; b < l.bars; ++b)
                {
                    bool has = false; for (auto& n : ba) if (n.step == b * 16) has = true;
                    anchor.push_back (has ? 1.0 : 0.0);
                }
        }
        s.melPerBar = mean (perBar); s.coverage = mean (cov); s.meanLen = mean (len); s.rhythmRepeat = mean (rep);
        s.plainTriad = mean (plain); s.clusterPerBar = mean (cluster); s.bassAnchor = mean (anchor);
        s.medianInterval = median (intervals); s.medianPitch = median (pitches);
        if (! intervals.empty())
        {
            s.shareGE12 = (double) std::count_if (intervals.begin(), intervals.end(), [] (int i) { return i >= 12; }) / (double) intervals.size();
            s.shareStep = (double) std::count_if (intervals.begin(), intervals.end(), [] (int i) { return i >= 1 && i <= 4; }) / (double) intervals.size();
        }
        int top = 0; for (auto& kv : patterns) top = std::max (top, kv.second);
        s.topPatternShare = patternBars ? (double) top / patternBars : 0;
        return s;
    }

    // statistical checks: MAGIC is random, so a failing block is repeated once with fresh loops
    template <typename Fn>
    void statBlock (const std::string& title, Fn&& evaluate)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            int before = failures;
            std::vector<std::string> lines;
            const bool ok = evaluate (lines);
            if (ok || attempt == 1)
            {
                std::printf ("--- %s%s\n", title.c_str(), attempt == 1 && ! ok ? " (failed twice)" : "");
                for (auto& l : lines) std::printf ("%s\n", l.c_str());
                if (! ok) ++failures;
                return;
            }
            failures = before;
            std::printf ("... %s: retrying with fresh loops\n", title.c_str());
        }
    }

    struct MidiInfo { int cc1 = 0, overlappingMelody = 0, melodyNotes = 0, drumNotesCh10 = 0, notesCh5 = 0; bool ok = false;
                      std::map<std::string, std::set<int>> drumTracks; };   // track name -> pitches used on channel 10
    MidiInfo readMidi (const juce::File& f)
    {
        MidiInfo info;
        juce::FileInputStream in (f);
        juce::MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) return info;
        info.ok = true;
        struct Span { double a, b; };
        std::vector<Span> mel;
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence seq (*mf.getTrack (t));
            seq.updateMatchedPairs();
            std::string trackName;
            for (int i = 0; i < seq.getNumEvents(); ++i)
                if (seq.getEventPointer (i)->message.isTrackNameEvent()) trackName = seq.getEventPointer (i)->message.getTextFromTextMetaEvent().toStdString();
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                auto* ev = seq.getEventPointer (i);
                if (ev->message.isController() && ev->message.getControllerNumber() == 1) ++info.cc1;
                if (ev->message.isNoteOn() && ev->message.getChannel() == 10) { ++info.drumNotesCh10; info.drumTracks[trackName].insert (ev->message.getNoteNumber()); }
                if (ev->message.isNoteOn() && ev->message.getChannel() == 5) ++info.notesCh5;
                if (ev->message.isNoteOn() && ev->message.getChannel() == 3 && ev->noteOffObject != nullptr)
                    mel.push_back ({ ev->message.getTimeStamp(), ev->noteOffObject->message.getTimeStamp() });
            }
        }
        std::sort (mel.begin(), mel.end(), [] (const Span& x, const Span& y) { return x.a < y.a; });
        info.melodyNotes = (int) mel.size();
        for (size_t i = 1; i < mel.size(); ++i) if (mel[i].a < mel[i - 1].b - 1.0) ++info.overlappingMelody;
        return info;
    }

    // ---- simulated listener for the Taste ML check ------------------------------------------------
    struct F5 { double dens, steps, leaps, reg, len; };
    F5 feats (const std::vector<Note>& notes, int bars)
    {
        std::vector<Note> mel; for (auto& n : notes) if (n.channel == 3) mel.push_back (n);
        std::sort (mel.begin(), mel.end(), [] (const Note& a, const Note& b) { return a.step < b.step; });
        F5 f { 4.57, 0.267, 0.30, 72.7, 2.07 };
        if (mel.size() < 3) return f;
        f.dens = (double) mel.size() / std::max (1, bars);
        int st = 0, lp = 0, n = 0; std::vector<int> ps; double len = 0;
        for (size_t i = 0; i < mel.size(); ++i)
        {
            ps.push_back (mel[i].note); len += mel[i].length;
            if (i) { int d = std::abs (mel[i].note - mel[i - 1].note); ++n; if (d >= 1 && d <= 4) ++st; if (d >= 8) ++lp; }
        }
        std::sort (ps.begin(), ps.end());
        f.steps = (double) st / n; f.leaps = (double) lp / n; f.reg = ps[ps.size() / 2]; f.len = len / (double) mel.size();
        return f;
    }
    double utility (const F5& f)   // hidden taste: dense, stepwise, high register
    {
        return (f.dens - 4.57) / 0.44 + (f.steps - 0.267) / 0.16 + (f.reg - 72.7) / 5.3;
    }
    double firstLoopUtility (MidiForgeAudioProcessor& p, int presses)
    {
        double u = 0;
        for (int i = 0; i < presses; ++i) { p.magicRandomize(); u += utility (feats (p.getVisibleNotes(), p.getVisibleBars())); }
        return u / presses;
    }
}

int main()
{
    // Hermetic settings: never touch the real taste.json / feedback.csv (earlier runs used to leak learned taste into later ones).
    const auto qaSettings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("midiforge_qa_settings_" + juce::String (juce::Time::currentTimeMillis()));
    qaSettings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (qaSettings);

    MidiForgeAudioProcessor p;
    p.resetTaste();

    // ------------------------------------------------------------------ Unified Melody Intent contract
    {
        const auto a = midiforge::MelodyIntent::makePlan (
            4, 0, 0, 0.65f, 0.55f, 0x1234ABCDu, 0);
        const auto b = midiforge::MelodyIntent::makePlan (
            4, 0, 0, 0.65f, 0.55f, 0x1234ABCDu, 0);

        const bool deterministic =
               a.characterIndex == b.characterIndex
            && a.language.contourFamily == b.language.contourFamily
            && a.language.intervalFamily == b.language.intervalFamily
            && a.language.rhythmFamily == b.language.rhythmFamily
            && a.language.repetitionStyle == b.language.repetitionStyle
            && a.language.registerJourney == b.language.registerJourney
            && a.grammar.phrases.size() == b.grammar.phrases.size()
            && a.simpleProbability == b.simpleProbability
            && a.complexProbability == b.complexProbability;

        std::set<std::string> languageSignatures;
        for (uint32_t i = 0; i < 16; ++i)
        {
            const auto plan = midiforge::MelodyIntent::makePlan (
                4, 0, 0, 0.65f, 0.55f, 0x9000u + i * 7919u, (int) (i % 8));
            languageSignatures.insert (
                std::to_string (plan.language.contourFamily) + "/"
                + std::to_string (plan.language.intervalFamily) + "/"
                + std::to_string (plan.language.rhythmFamily) + "/"
                + std::to_string (plan.characterIndex));
        }

        const bool probabilityContract =
               a.simpleProbability >= 0.18f
            && a.simpleProbability <= 0.68f
            && a.complexProbability >= 0.08f
            && a.complexProbability <= 0.38f
            && a.simpleProbability + a.complexProbability <= 0.90f;

        report ("Unified Melody Intent",
                deterministic && probabilityContract && languageSignatures.size() >= 4,
                fmt ("deterministic=%d signatures=%d simple=%.3f complex=%.3f",
                     deterministic, (double) languageSignatures.size(),
                     a.simpleProbability, a.complexProbability));
    }


    // ------------------------------------------------------------------ MAGIC Scale Coverage
    {
        std::set<int> seenScales;
        constexpr int scaleCount = 12;

        // MAGIC should be able to reach every declared scale, not only the
        // original first seven choices.
        for (int pass = 0; pass < 180; ++pass)
        {
            p.magicRandomize();