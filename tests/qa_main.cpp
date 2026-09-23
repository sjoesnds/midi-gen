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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
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
    std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
    {
        char buf[256];
        std::snprintf (buf, sizeof buf, f, a, b, c);
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
    MidiForgeAudioProcessor p;
    p.resetTaste();

    // ------------------------------------------------------------------ 1. invariants (every loop)
    {
        int loopsChecked = 0; std::string firstProblem;
        auto problem = [&] (const std::string& s) { if (firstProblem.empty()) firstProblem = s; };
        for (int sound = 0; sound < 8; ++sound)
        {
            p.setSoundTarget (sound); p.setArticulation (1);
            for (auto& l : makeLoops (p, 25))
            {
                ++loopsChecked;
                std::set<std::pair<int, int>> melSteps; std::set<std::tuple<int, int, int>> keys;
                for (auto& n : l.notes)
                {
                    if (n.note < 0 || n.note > 127 || n.length < 1 || n.velocity < 1 || n.velocity > 127) problem ("invalid note value");
                    if (! keys.insert ({ n.channel, n.step, n.note }).second) problem ("duplicate note");
                    if (n.channel == 3 && ! melSteps.insert ({ 0, n.step }).second) problem ("two melody notes on one step");
                }
                auto mel = layer (l, 3);
                for (size_t i = 1; i < mel.size(); ++i)
                    if (mel[i].step < mel[i - 1].step + mel[i - 1].length) problem ("melody notes overlap in the stored loop");
                auto ba = layer (l, 2);
                for (auto& n : ba) if (n.note < 28) problem ("bass below E1");
                auto ch = layer (l, 1);
                if (! ch.empty())
                    for (int b = 0; b < l.bars; ++b)
                    {
                        std::set<int> pcs; for (auto& n : ch) if (n.step / 16 == b) pcs.insert (n.note % 12);
                        if (pcs.size() < 3) problem ("bar without a full triad");
                    }
                if (! ba.empty() && sound != 6)
                    for (int b = 0; b < l.bars; ++b)
                    {
                        bool has = false; for (auto& n : ba) if (n.step == b * 16) has = true;
                        if (! has) problem ("bar without a bass anchor on beat 1");
                    }
                if (sound == 6 && ! ba.empty()) problem ("808 profile must not have a separate bass layer");
            }
        }
        report ("invariants on every loop", firstProblem.empty(), fmt ("%.0f loops over 8 sounds", loopsChecked) + (firstProblem.empty() ? "" : "  first problem: " + firstProblem));
    }

    // ------------------------------------------------------------------ 2. statistics (Piano)
    p.setSoundTarget (0);
    statBlock ("melody / harmony statistics (Piano, 120 MAGIC loops)", [&] (std::vector<std::string>& out)
    {
        auto s = analyse (makeLoops (p, 120));
        bool ok = true;
        auto row = [&] (const char* name, double v, bool good, const char* rule) { ok = ok && good; out.push_back (std::string (good ? "  [ok]   " : "  [FAIL] ") + name + " = " + fmt ("%.3f", v) + "   (" + rule + ")"); };
        row ("melody notes per bar",        s.melPerBar,        s.melPerBar >= 3.8,        ">= 3.8");
        row ("median melodic interval",     s.medianInterval,   s.medianInterval <= 6.0,   "<= 6 semitones");
        row ("share of octave+ leaps",      s.shareGE12,        s.shareGE12 <= 0.12,       "<= 0.12");
        row ("share of steps (1-4 st)",     s.shareStep,        s.shareStep >= 0.22,       ">= 0.22");
        row ("bars with repeated rhythm",   s.rhythmRepeat,     s.rhythmRepeat >= 0.28,    ">= 0.28 (loops of 4+ bars)");
        row ("most common groove share",    s.topPatternShare,  s.topPatternShare <= 0.15, "<= 0.15");
        row ("bars with plain triad",       s.plainTriad,       s.plainTriad >= 0.90,      ">= 0.90");
        row ("semitone clusters per bar",   s.clusterPerBar,    s.clusterPerBar <= 0.06,   "<= 0.06");
        return ok;
    });

    // ------------------------------------------------------------------ 3. profiles behave differently
    {
        const char* names[8] = { "Piano", "Pluck", "Synth Lead", "Bell", "Pad", "Brass", "808", "Guitar" };
        Stats st[8];
        for (int sound = 0; sound < 8; ++sound) { p.setSoundTarget (sound); p.setArticulation (1); st[sound] = analyse (makeLoops (p, 40)); }
        for (int i = 0; i < 8; ++i)
            std::printf ("    %-10s notes/bar %.2f  mean length %.2f  coverage %.2f  median pitch %.0f  bass notes %d\n", names[i], st[i].melPerBar, st[i].meanLen, st[i].coverage, st[i].medianPitch, st[i].bassNotes);
        report ("Pluck notes are short",        st[1].meanLen <= 2.0,                    fmt ("mean length %.2f <= 2.0", st[1].meanLen));
        report ("Synth Lead is legato",         st[2].coverage >= 0.80,                  fmt ("coverage %.2f >= 0.80", st[2].coverage));
        report ("Bell sits higher than Piano",  st[3].medianPitch - st[0].medianPitch >= 5, fmt ("%.0f vs %.0f semitones", st[3].medianPitch, st[0].medianPitch));
        report ("Pad is sparse with long notes", st[4].melPerBar <= 3.4 && st[4].meanLen >= 4.0, fmt ("%.2f notes/bar, length %.2f", st[4].melPerBar, st[4].meanLen));
        report ("808 is low and has no bass layer", st[6].medianPitch <= 56 && st[6].bassNotes == 0, fmt ("median pitch %.0f, bass notes %.0f", st[6].medianPitch, st[6].bassNotes));
        report ("Guitar lives in guitar range", st[7].medianPitch >= 52 && st[7].medianPitch <= 76, fmt ("median pitch %.0f", st[7].medianPitch));
    }

    // ------------------------------------------------------------------ 3b. 808 is ONE bass line
    {
        p.setSoundTarget (6); p.setArticulation (0);
        int otherLayers = 0, outOfLane = 0, bars = 0, beat1 = 0, intervals = 0, bigIntervals = 0;
        std::vector<int> pitches;
        for (auto& l : makeLoops (p, 60))
        {
            auto line = layer (l, 3);
            for (auto& n : l.notes) if (n.channel != 3) ++otherLayers;
            for (auto& n : line) { pitches.push_back (n.note); if (n.note < 28 || n.note > 50) ++outOfLane; }
            for (int b = 0; b < l.bars; ++b) { ++bars; for (auto& n : line) if (n.step == b * 16) { ++beat1; break; } }
            for (size_t i = 1; i < line.size(); ++i) { ++intervals; if (std::abs (line[i].note - line[i - 1].note) > 12) ++bigIntervals; }
        }
        const double bigShare = intervals ? (double) bigIntervals / intervals : 0;
        report ("808 is a single line (no chord / bass / arp notes)", otherLayers == 0, fmt ("%.0f notes on other layers", otherLayers));
        report ("808 stays in E1..D3", outOfLane == 0 && median (pitches) >= 30 && median (pitches) <= 46, fmt ("%.0f notes out of range, median pitch %.0f", outOfLane, median (pitches)));
        report ("808 plays on beat 1 of every bar", beat1 == bars, fmt ("%.0f of %.0f bars", beat1, bars));
        report ("808 moves like a bass line (few jumps above an octave)", bigShare <= 0.15, fmt ("share %.3f <= 0.15", bigShare));
    }

    // ------------------------------------------------------------------ 3c. chord comping
    {
        auto chordStarts = [&] (int style, int loops)
        {
            p.setSoundTarget (0); p.setChordStyle (style);
            double total = 0; int bars = 0;
            for (auto& l : makeLoops (p, loops))
            {
                auto ch = layer (l, 1);
                for (int b = 0; b < l.bars; ++b)
                {
                    std::set<int> starts; for (auto& n : ch) if (n.step / 16 == b) starts.insert (n.step);
                    if (! starts.empty()) { total += (double) starts.size(); ++bars; }
                }
            }
            return bars ? total / bars : 0.0;
        };
        const double held = chordStarts (1, 40), comp = chordStarts (2, 40);
        p.setChordStyle (0);
        report ("Chords = Held plays (almost) one hit per bar", held <= 1.6, fmt ("%.2f chord hits per bar", held));
        report ("Chords = Comping plays a rhythm",             comp >= 2.3, fmt ("%.2f chord hits per bar", comp));
    }

    // ------------------------------------------------------------------ 3d. drums layer
    {
        const std::set<int> gm { 36, 38, 39, 42, 43, 45, 46, 47, 49, 50, 70 };
        p.setSoundTarget (0); p.setDrumsEnabled (false);
        int drumsWhenOff = 0;
        for (auto& l : makeLoops (p, 20)) for (auto& n : l.notes) if (n.channel == 5) ++drumsWhenOff;
        p.setDrumsEnabled (true);
        int bars = 0, kickOnOne = 0, badPitch = 0, drumNotes = 0, hats = 0, otherLayerDrums = 0;
        for (auto& l : makeLoops (p, 60))
        {
            for (int b = 0; b < l.bars; ++b)
            {
                ++bars;
                for (auto& n : l.notes) if (n.channel == 5 && n.step == b * 16 && n.note == 36) { ++kickOnOne; break; }
            }
            for (auto& n : l.notes)
            {
                if (n.channel == 5) { ++drumNotes; if (! gm.count (n.note)) ++badPitch; if (n.note == 42 || n.note == 46) ++hats; }
                else if (gm.count (n.note) && n.note < 47 && n.channel == 4) { /* arp may legitimately use these pitches */ }
            }
        }
        report ("Drums off: no drum notes", drumsWhenOff == 0, fmt ("%.0f drum notes", drumsWhenOff));
        report ("Drums on: kick on beat 1 of every bar", kickOnOne == bars && drumNotes > 0, fmt ("%.0f of %.0f bars, %.0f drum notes", kickOnOne, bars, drumNotes));
        report ("Drums use General MIDI pitches only", badPitch == 0, fmt ("%.0f wrong pitches", badPitch));
        (void) hats; (void) otherLayerDrums;

        // exported file: drums are on General MIDI channel 10 (never on 5)
        MidiInfo total;
        for (int i = 0; i < 15; ++i)
        {
            p.magicRandomize();
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_drums.mid");
            p.exportMidiFileTo (f);
            auto m = readMidi (f); total.drumNotesCh10 += m.drumNotesCh10; total.notesCh5 += m.notesCh5; f.deleteFile();
        }
        report ("Drums are written to MIDI channel 10", total.drumNotesCh10 > 0 && total.notesCh5 == 0, fmt ("channel 10: %.0f notes, channel 5: %.0f", total.drumNotesCh10, total.notesCh5));

        // the 808 plays where the kick plays
        p.setSoundTarget (6);
        int hits808 = 0, locked = 0;
        for (auto& l : makeLoops (p, 40))
            for (auto& n : l.notes)
                if (n.channel == 3)
                {
                    ++hits808; bool onKick = false;
                    for (auto& k : l.notes) if (k.channel == 5 && k.note == 36 && k.step == n.step) { onKick = true; break; }
                    if (onKick) ++locked;
                }
        report ("808 locks to the kick when drums are on", hits808 > 0 && locked == hits808, fmt ("%.0f of %.0f hits on a kick", locked, hits808));
        // MUTATE / EVOLVE must not move the drum hits
        p.setSoundTarget (0); p.magicRandomize();
        auto drumSteps = [&] { std::multiset<std::pair<int,int>> v; for (auto& n : p.getVisibleNotes()) if (n.channel == 5) v.insert ({ n.step, n.note }); return v; };
        const auto before = drumSteps();
        p.mutateSelected (0.9f); p.evolveSelected();
        report ("MUTATE / EVOLVE keep the drum groove", drumSteps() == before && ! before.empty(), fmt ("%.0f drum hits", (double) before.size()));
        p.setDrumsEnabled (false); p.setSoundTarget (0);
    }

    // ------------------------------------------------------------------ 3e. drum section: one instrument per row
    {
        p.setSoundTarget (0); p.setDrumsEnabled (true); p.setDrumMuteMask (0); p.setDrumPitchMode (0);
        const std::set<std::string> rowNames { "Kick", "Snare", "Clap", "Hat", "Open Hat", "Toms", "Crash", "Shaker" };
        bool namesOk = true, oneInstrumentPerTrack = true, sampler = true; size_t maxTracks = 0;
        for (int i = 0; i < 20; ++i)
        {
            p.magicRandomize();
            p.setGenre (i % 2 ? 2 : 1);                      // House / Trap: many instruments
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_rows.mid");
            p.exportMidiFileTo (f);
            auto m = readMidi (f); f.deleteFile();
            maxTracks = std::max (maxTracks, m.drumTracks.size());
            for (auto& kv : m.drumTracks)
            {
                if (! rowNames.count (kv.first)) namesOk = false;
                for (int pitch : kv.second) if (MidiForgeAudioProcessor::drumRowForNote (pitch) >= 0 && kv.first != "Toms" && pitch != 60) sampler = false;
            }
            for (auto& kv : m.drumTracks) if (kv.first != "Toms" && kv.second.size() != 1) oneInstrumentPerTrack = false;
        }
        report ("drums are split into one named track per instrument", namesOk && maxTracks >= 4, fmt ("up to %.0f separate drum tracks", (double) maxTracks));
        report ("each drum track holds a single pitch (C5) in sampler mode", oneInstrumentPerTrack && sampler, "kick / snare / hats all on C5");

        // General MIDI mode keeps kit pitches
        p.setDrumPitchMode (1);
        p.magicRandomize();
        { auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_gm.mid");
          p.exportMidiFileTo (f); auto m = readMidi (f); f.deleteFile();
          bool kickIsGm = m.drumTracks.count ("Kick") && m.drumTracks["Kick"].count (36);
          report ("GM pitch mode writes kit pitches", kickIsGm, "kick = 36"); }
        p.setDrumPitchMode (0);

        // dragging a single instrument writes only that instrument
        p.magicRandomize();
        auto kickFile = p.writeTemporaryMidiFileForDrumRow (0);
        auto kickInfo = readMidi (kickFile); kickFile.deleteFile();
        report ("Drag Kick writes only the kick", kickInfo.drumTracks.size() == 1 && kickInfo.drumTracks.count ("Kick") == 1, fmt ("%.0f drum track(s)", (double) kickInfo.drumTracks.size()));

        // mute: the instrument disappears from the full file but can still be dragged alone
        p.setDrumMuteMask (1 << 0);
        auto f2 = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_mute.mid");
        p.exportMidiFileTo (f2); auto muted = readMidi (f2); f2.deleteFile();
        auto kickAlone = p.writeTemporaryMidiFileForDrumRow (0); auto kickAloneInfo = readMidi (kickAlone); kickAlone.deleteFile();
        report ("muted instrument is left out of the full file, still draggable alone",
               muted.drumTracks.count ("Kick") == 0 && kickAloneInfo.drumTracks.count ("Kick") == 1, "Kick muted");
        p.setDrumMuteMask (0);

        // step-grid editing
        p.magicRandomize();
        int freeStep = -1;
        auto before = p.getVisibleNotes();
        for (int st = 1; st < p.getVisibleBars() * 16 && freeStep < 0; ++st)
        {
            bool taken = false; for (auto& n : before) if (n.channel == 5 && n.step == st && MidiForgeAudioProcessor::drumRowForNote (n.note) == 3) taken = true;
            if (! taken) freeStep = st;
        }
        const bool on = p.toggleDrumHit (freeStep, 3);
        auto afterOn = p.getVisibleNotes();
        const bool off = ! p.toggleDrumHit (freeStep, 3);
        auto afterOff = p.getVisibleNotes();
        report ("clicking a step adds / removes a hat hit", on && off && afterOn.size() == before.size() + 1 && afterOff.size() == before.size(),
               fmt ("%.0f -> %.0f -> %.0f notes", (double) before.size(), (double) afterOn.size(), (double) afterOff.size()));
        p.setDrumsEnabled (false);
    }

    // ------------------------------------------------------------------ 4. articulation
    {
        auto exportAndRead = [&] (int sound, int art, int loops)
        {
            MidiInfo total;
            p.setSoundTarget (sound); p.setArticulation (art);
            for (int i = 0; i < loops; ++i)
            {
                p.magicRandomize();
                auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_art.mid");
                p.exportMidiFileTo (f);
                auto m = readMidi (f);
                total.ok = m.ok; total.cc1 += m.cc1; total.overlappingMelody += m.overlappingMelody; total.melodyNotes += m.melodyNotes;
                f.deleteFile();
            }
            return total;
        };
        auto off = exportAndRead (2, 0, 25), slides = exportAndRead (2, 1, 25), vib = exportAndRead (2, 2, 25), piano = exportAndRead (0, 2, 25);
        report ("Articulation Off: no overlaps, no CC1", off.ok && off.overlappingMelody == 0 && off.cc1 == 0, fmt ("overlaps %.0f, cc1 %.0f", off.overlappingMelody, off.cc1));
        report ("Lead + Slides: legato overlaps appear", slides.overlappingMelody > 0 && slides.cc1 == 0, fmt ("overlaps %.0f of %.0f notes, cc1 %.0f", slides.overlappingMelody, slides.melodyNotes, slides.cc1));
        report ("Lead + Vibrato: CC1 appears",          vib.cc1 > 0, fmt ("cc1 events %.0f", vib.cc1));
        report ("Piano ignores articulation",           piano.overlappingMelody == 0 && piano.cc1 == 0, fmt ("overlaps %.0f, cc1 %.0f", piano.overlappingMelody, piano.cc1));
    }

    // ------------------------------------------------------------------ 5. Taste ML learns
    p.setSoundTarget (0);
    statBlock ("Taste ML (simulated listener who likes dense, stepwise, high loops)", [&] (std::vector<std::string>& out)
    {
        p.resetTaste();
        const double before = firstLoopUtility (p, 30);
        for (int r = 0; r < 12; ++r)
        {
            p.magicRandomize();
            double best = -1e9, worst = 1e9; int bi = 0, wi = 0;
            for (int v = 0; v < p.getVariationCount(); ++v)
            {
                p.chooseVariation (v);
                const double u = utility (feats (p.getVisibleNotes(), p.getVisibleBars()));
                if (u > best) { best = u; bi = v; } if (u < worst) { worst = u; wi = v; }
            }
            p.likeVariation (bi); p.dislikeVariation (wi);
        }
        const double after = firstLoopUtility (p, 30);
        const bool ok = (after - before) >= 0.7 && p.getTasteConfidence() > 0.5f;
        out.push_back ("  utility of the first loop: " + fmt ("%.2f -> %.2f (needs +0.7)", before, after) + fmt ("   confidence %.2f", p.getTasteConfidence()));
        return ok;
    });
    {
        MidiForgeAudioProcessor q;      // a fresh instance must load what was saved
        report ("Taste model survives restart", q.getTasteConfidence() > 0.5f, fmt ("confidence %.2f", q.getTasteConfidence()));
        q.resetTaste();
        report ("Taste reset works", q.getTasteConfidence() < 0.01f && q.getTasteLikes() == 0, fmt ("confidence %.2f", q.getTasteConfidence()));
    }

    // ------------------------------------------------------------------ 6. state and AUTO-NEXT
    {
        MidiForgeAudioProcessor a; a.setSoundTarget (7); a.setArticulation (2); a.setAutoNext (false); a.setChordStyle (2); a.setDrumsEnabled (true);
        a.setDrumMuteMask (0x0A); a.setDrumPitchMode (1);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MidiForgeAudioProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        report ("state round-trip", b.getSoundTarget() == 7 && b.getArticulation() == 2 && ! b.getAutoNext() && b.getChordStyle() == 2 && b.isDrumsEnabled()
               && b.getDrumMuteMask() == 0x0A && b.getDrumPitchMode() == 1,
               fmt ("sound %.0f, articulation %.0f, chord style %.0f", b.getSoundTarget(), b.getArticulation(), b.getChordStyle()));
        MidiForgeAudioProcessor c1; c1.setStateInformation (mb.getData(), (int) mb.getSize() - 8);    // project saved by 0.44
        report ("0.44 project (no drum mute / pitch fields) loads", c1.isDrumsEnabled() && c1.getDrumMuteMask() == 0 && c1.getDrumPitchMode() == 0, "defaults applied");
        MidiForgeAudioProcessor c0; c0.setStateInformation (mb.getData(), (int) mb.getSize() - 16);   // project saved by 0.42 / 0.43
        report ("0.42 project (no chord style / drums fields) loads", c0.getArticulation() == 2 && c0.getChordStyle() == 0 && ! c0.isDrumsEnabled(), "defaults applied");
        MidiForgeAudioProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize() - 24);    // project saved by 0.40 / 0.41
        report ("old project (no articulation fields) loads", c.getSoundTarget() == 7 && c.getArticulation() == 0, "defaults applied");
        MidiForgeAudioProcessor d; d.setStateInformation (mb.getData(), (int) mb.getSize() - 28);    // project saved by 0.38 / 0.39
        report ("older project (no sound field) loads", d.getSoundTarget() == 0, "defaults applied");
    }
    {
        MidiForgeAudioProcessor a; a.setAutoNext (true); a.magicRandomize();
        const int s0 = a.getSelectedVariation(); a.dislikeAndAdvance();
        const bool moved = a.getSelectedVariation() == s0 + 1;
        a.setAutoNext (false); const int s1 = a.getSelectedVariation(); a.dislikeAndAdvance();
        report ("AUTO-NEXT moves to the next loop after DISLIKE", moved && a.getSelectedVariation() == s1, fmt ("%.0f -> %.0f", s0, s0 + 1.0));
    }

    // ------------------------------------------------------------------ 7. 0.45.1 regression checks
    {
        // 7a. EXPORT over an existing (longer) file must replace it, not append to it
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_overwrite.mid");
        f.deleteFile();
        MidiForgeAudioProcessor a; a.setBars (16); a.exportMidi (f);
        a.setBars (1); a.exportMidi (f); const auto sizeOver = f.getSize();
        f.deleteFile(); a.exportMidi (f); const auto sizeFresh = f.getSize();
        juce::MidiFile parsed; bool parsedOk = false;
        { juce::FileInputStream in (f); parsedOk = in.openedOk() && parsed.readFrom (in) && parsed.getNumTracks() >= 2; }
        report ("EXPORT .MID over an existing file replaces it", sizeOver == sizeFresh && parsedOk,
                fmt ("overwrite %.0f B, fresh %.0f B", (double) sizeOver, (double) sizeFresh));
        a.setBars (16); a.exportMidiFileTo (f); a.setBars (1); a.exportMidiFileTo (f); const auto s2 = f.getSize();
        f.deleteFile(); a.exportMidiFileTo (f);
        report ("Export MIDI... over an existing file replaces it", s2 == f.getSize(), fmt ("overwrite %.0f B, fresh %.0f B", (double) s2, (double) f.getSize()));
        f.deleteFile();
    }
    {
        // 7b. MUTATE keeps chords together and is different on every press
        MidiForgeAudioProcessor a; a.setChordStyle (1);
        int stepsBefore = 0, stepsAfter = 0;
        for (int r = 0; r < 20; ++r)
        {
            a.magicRandomize();
            auto countSteps = [] (const std::vector<Note>& v) { std::set<int> st; for (auto& n : v) if (n.channel == 1) st.insert (n.step); return (int) st.size(); };
            stepsBefore += countSteps (a.getVisibleNotes());
            a.mutateSelected (0.45f);
            stepsAfter += countSteps (a.getVisibleNotes());
        }
        report ("MUTATE never tears a chord apart", stepsAfter <= stepsBefore, fmt ("distinct chord start steps: %.0f before, %.0f after", stepsBefore, stepsAfter));
        a.magicRandomize();
        const auto orig = a.getVisibleNotes();
        a.mutateSelected (0.45f); const auto m1 = a.getVisibleNotes();
        a.replaceVisibleNotes (orig); a.mutateSelected (0.45f); const auto m2 = a.getVisibleNotes();
        bool same = m1.size() == m2.size();
        for (size_t i = 0; same && i < m1.size(); ++i) same = m1[i].step == m2[i].step && m1[i].note == m2[i].note && m1[i].length == m2[i].length;
        report ("two MUTATE presses on the same loop differ", ! same, same ? "identical" : "different");
    }
    {
        // 7c. SWING is written into exported / dragged MIDI (odd 16ths late by swing/2 of a step)
        MidiForgeAudioProcessor a; a.setSwing (0.5f); a.setDrumsEnabled (true);
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mf_qa_swing.mid");
        a.exportMidiFileTo (f);
        juce::MidiFile mf; { juce::FileInputStream in (f); mf.readFrom (in); }
        int total = 0, bad = 0, swung = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (int e = 0; e < mf.getTrack (t)->getNumEvents(); ++e)
            {
                const auto& m = mf.getTrack (t)->getEventPointer (e)->message;
                if (! m.isNoteOn()) continue;
                const long r = std::lround (m.getTimeStamp()) % 480;   // 960 ppq: one 8th = 480 ticks, odd 16th = 240 + swing 60
                ++total; if (r == 300) ++swung; else if (r != 0) ++bad;
            }
        f.deleteFile();
        report ("SWING reaches the exported MIDI", total > 0 && swung > 0 && bad == 0, fmt ("%.0f note-ons, %.0f swung, %.0f off-grid", total, swung, bad));
    }
    {
        // 7d. live output (processBlock): with maximum swing every event stays inside its block and a
        //     retriggered note is never cut by a stale note-off (on / off always pair up, depth 0..1)
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0.0, bpm = 120.0;
            juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); i.setPpqPosition (ppq); i.setIsPlaying (true); return i; }
        };
        MidiForgeAudioProcessor a; a.setSwing (0.75f); a.setDrumsEnabled (true); a.setBars (1);
        const double sr = 48000.0; const int blk = 256;
        a.prepareToPlay (sr, blk);
        Head head; a.setPlayHead (&head);
        juce::AudioBuffer<float> audio (2, blk);
        std::map<std::pair<int, int>, int> depth;
        int minDepth = 0, maxDepth = 0, beyond = 0, events = 0;
        const int blocks = (int) (8.0 * sr / blk);
        for (int b = 0; b < blocks; ++b)
        {
            head.ppq = (double) b * blk / sr * head.bpm / 60.0;
            juce::MidiBuffer mb; a.processBlock (audio, mb);
            for (const auto meta : mb)
            {
                const auto m = meta.getMessage();
                if (meta.samplePosition >= blk) ++beyond;
                auto& d = depth[{ m.getChannel(), m.getNoteNumber() }];
                if (m.isNoteOn())  { ++d; maxDepth = std::max (maxDepth, d); ++events; }
                if (m.isNoteOff()) { --d; minDepth = std::min (minDepth, d); }
            }
        }
        report ("live MIDI: every event inside its block (swing 0.75)", events > 0 && beyond == 0, fmt ("%.0f note-ons, %.0f outside the block", events, beyond));
        report ("live MIDI: note-on / note-off pair up, no retrigger cut", minDepth >= 0 && maxDepth <= 1, fmt ("depth min %.0f, max %.0f", minDepth, maxDepth));
    }

    std::printf ("\n%s (%d failed check%s)\n", failures == 0 ? "ALL QUALITY CHECKS PASSED" : "QUALITY CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
