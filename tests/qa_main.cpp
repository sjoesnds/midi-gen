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
                    for (auto& n : ch) if (n.step == b * 16) { pcs.insert (n.note % 12); ps.push_back (n.note); }
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

    struct MidiInfo { int cc1 = 0, overlappingMelody = 0, melodyNotes = 0; bool ok = false; };
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
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                auto* ev = seq.getEventPointer (i);
                if (ev->message.isController() && ev->message.getControllerNumber() == 1) ++info.cc1;
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
                        std::set<int> pcs; for (auto& n : ch) if (n.step == b * 16) pcs.insert (n.note % 12);
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
        MidiForgeAudioProcessor a; a.setSoundTarget (7); a.setArticulation (2); a.setAutoNext (false);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MidiForgeAudioProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        report ("state round-trip", b.getSoundTarget() == 7 && b.getArticulation() == 2 && ! b.getAutoNext(),
               fmt ("sound %.0f, articulation %.0f", b.getSoundTarget(), b.getArticulation()));
        MidiForgeAudioProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize() - 8);   // project saved by 0.40 / 0.41
        report ("old project (no articulation fields) loads", c.getSoundTarget() == 7 && c.getArticulation() == 0, "defaults applied");
        MidiForgeAudioProcessor d; d.setStateInformation (mb.getData(), (int) mb.getSize() - 12); // project saved by 0.38 / 0.39
        report ("older project (no sound field) loads", d.getSoundTarget() == 0, "defaults applied");
    }
    {
        MidiForgeAudioProcessor a; a.setAutoNext (true); a.magicRandomize();
        const int s0 = a.getSelectedVariation(); a.dislikeAndAdvance();
        const bool moved = a.getSelectedVariation() == s0 + 1;
        a.setAutoNext (false); const int s1 = a.getSelectedVariation(); a.dislikeAndAdvance();
        report ("AUTO-NEXT moves to the next loop after DISLIKE", moved && a.getSelectedVariation() == s1, fmt ("%.0f -> %.0f", s0, s0 + 1.0));
    }

    std::printf ("\n%s (%d failed check%s)\n", failures == 0 ? "ALL QUALITY CHECKS PASSED" : "QUALITY CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
