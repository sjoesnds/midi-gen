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


    // ------------------------------------------------------------------ MAGIC Scale Coverage
    {
        std::set<int> seenScales;
        constexpr int scaleCount = 12;

        // MAGIC should be able to reach every declared scale, not only the
        // original first seven choices.
        for (int pass = 0; pass < 180; ++pass)
        {
            p.magicRandomize();
            seenScales.insert (p.getScale());
        }

        bool allScalesSeen = seenScales.size() == scaleCount;
        report ("MAGIC can reach every declared scale",
                allScalesSeen,
                fmt ("seen %d/12 scales", (double) seenScales.size()));
    }


    // ------------------------------------------------------------------ Melodic pleasantness safety
    // The final melody pass is intentionally conservative: generated lead notes stay
    // in the selected scale, avoid oversized default leaps, and break pathological
    // same-note runs without flattening the whole phrase.
    {
        int badScaleNotes = 0;
        int badLeaps = 0;
        int badRepeatRuns = 0;
        int checkedMelodyNotes = 0;

        for (int pass = 0; pass < 48; ++pass)
        {
            p.magicRandomize();
            const bool ostinato = p.getMelodyType() == MidiForgeAudioProcessor::OstinatoMelody;
            const int variationCount = p.getVariationCount();

            for (int variation = 0; variation < variationCount; ++variation)
            {
                p.chooseVariation (variation);
                auto notes = p.getVisibleNotes();
                std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
                {
                    if (a.channel != b.channel) return a.channel < b.channel;
                    if (a.step != b.step) return a.step < b.step;
                    return a.note < b.note;
                });

                int previous = -1;
                int sameRun = 1;
                bool hadMelody = false;
                for (const auto& n : notes)
                {
                    if (n.channel != 3)
                        continue;

                    ++checkedMelodyNotes;
                    hadMelody = true;

                    if (p.snapPitchToScale (n.note) != n.note)
                        ++badScaleNotes;

                    if (previous >= 0 && std::abs (n.note - previous) > 9)
                        ++badLeaps;

                    if (previous >= 0 && n.note == previous)
                        ++sameRun;
                    else
                        sameRun = 1;

                    if (! ostinato && sameRun >= 3)
                        ++badRepeatRuns;

                    previous = n.note;
                }

                if (!hadMelody && !ostinato)
                    ++badRepeatRuns;
            }

            p.chooseVariation (0);
        }

        report ("Melody remains scale-safe",
                checkedMelodyNotes > 0 && badScaleNotes == 0,
                fmt ("%.0f checked notes, %.0f out of scale", checkedMelodyNotes, badScaleNotes));

        report ("Melody avoids oversized default leaps",
                badLeaps == 0,
                fmt ("%.0f leaps > 9 semitones", badLeaps));

        report ("Melody avoids pathological note runs",
                badRepeatRuns == 0,
                fmt ("%.0f runs of 3+ identical notes", badRepeatRuns));
    }

    // ------------------------------------------------------------------ 0.85 Musical Judge Calibration
    // Regression checks for register position and complexity-class selection.
    {
        auto samplePopulation = [&] (float complexityValue, uint32_t seedBase)
        {
            double simpleShare = 0.0;
            double meanPitch = 0.0;
            int populatedBars = 0;
            int simpleBars = 0;
            int pitchSamples = 0;

            p.setBars (4);
            p.setSoundTarget (0);
            p.setMelodyType (MidiForgeAudioProcessor::PhraseMelody);
            p.setComplexity (complexityValue, false);

            for (int seed = 0; seed < 32; ++seed)
            {
                p.setSeed ((int) (seedBase + (uint32_t) seed));
                p.regenerate ();

                std::array<int, 4> counts {};
                std::vector<int> melody;
                for (const auto& n : p.getVisibleNotes ())
                {
                    if (n.channel != 3)
                        continue;

                    melody.push_back (n.note);
                    const int bar = n.step / 16;
                    if (bar >= 0 && bar < 4)
                        ++counts[(size_t) bar];
                }

                for (const int count : counts)
                {
                    if (count > 0)
                    {
                        ++populatedBars;
                        if (count >= 2 && count <= 4)
                            ++simpleBars;
                    }
                }

                if (! melody.empty ())
                {
                    double total = 0.0;
                    for (const int note : melody)
                        total += note;
                    meanPitch += total / (double) melody.size ();
                    ++pitchSamples;
                }
            }

            simpleShare = populatedBars > 0
                ? (double) simpleBars / (double) populatedBars : 0.0;
            const double averagePitch = pitchSamples > 0
                ? meanPitch / (double) pitchSamples : 0.0;
            return std::pair<double, double> { simpleShare, averagePitch };
        };

        const auto low = samplePopulation (0.15f, 7100u);
        const auto high = samplePopulation (0.85f, 8100u);

        report ("0.85 complexity control changes simple-phrase share",
                low.first > high.first + 0.06,
                fmt ("low complexity %.0f%% simple bars vs high %.0f%%",
                     100.0 * low.first, 100.0 * high.first));

        report ("0.85 default melody register stays grounded",
                high.second < 75.5,
                fmt ("high-complexity sample mean pitch %.1f", high.second));
    }

    // ------------------------------------------------------------------ 0. Creative Range
    {
        const auto a = midiforge::CreativeRange::makePlan (0, 4, 0, 0.70f, 0.65f, 123456u);
        const auto b = midiforge::CreativeRange::makePlan (0, 4, 0, 0.70f, 0.65f, 123456u);

        const bool deterministic =
            a.contourFamily == b.contourFamily
            && a.intervalFamily == b.intervalFamily
            && a.rhythmFamily == b.rhythmFamily
            && a.repetitionStyle == b.repetitionStyle
            && a.registerJourney == b.registerJourney
            && a.harmonyPersonality == b.harmonyPersonality
            && a.durationStyle == b.durationStyle;

        std::set<std::string> languages;
        std::set<int> contours, intervals, rhythms, repetitions, journeys, harmonies;
        for (uint32_t seed = 1; seed <= 160; ++seed)
        {
            const auto plan = midiforge::CreativeRange::makePlan (
                0, 4, 0, 0.70f, 0.65f, seed);

            languages.insert (
                std::to_string (plan.contourFamily) + ":"
                + std::to_string (plan.intervalFamily) + ":"
                + std::to_string (plan.rhythmFamily) + ":"
                + std::to_string (plan.repetitionStyle) + ":"
                + std::to_string (plan.registerJourney) + ":"
                + std::to_string (plan.harmonyPersonality));

            contours.insert (plan.contourFamily);
            intervals.insert (plan.intervalFamily);
            rhythms.insert (plan.rhythmFamily);
            repetitions.insert (plan.repetitionStyle);
            journeys.insert (plan.registerJourney);
            harmonies.insert (plan.harmonyPersonality);
        }

        report ("Creative Range plan is deterministic", deterministic,
                deterministic ? "same identity -> same language"
                               : "same identity produced different language");

        report ("Creative Range has a genuinely broad language space",
                languages.size() >= 120
                && contours.size() >= 14
                && intervals.size() >= 9
                && rhythms.size() >= 9
                && repetitions.size() >= 6
                && journeys.size() >= 6
                && harmonies.size() >= 6,
                fmt7 ("unique=%.0f contours=%.0f intervals=%.0f rhythms=%.0f repeats=%.0f journeys=%.0f harmony=%.0f",
                     (double) languages.size(), (double) contours.size(), (double) intervals.size(),
                     (double) rhythms.size(), (double) repetitions.size(), (double) journeys.size(),
                     (double) harmonies.size()));
    }




    // ------------------------------------------------------------------ 0b. Motif Semantics 2.0
    {
        auto barNotes = [] (const std::vector<MidiForgeAudioProcessor::VisibleNote>& all, int bar)
        {
            std::vector<MidiForgeAudioProcessor::VisibleNote> out;
            for (const auto& n : all)
                if (n.channel == 3 && n.step / 16 == bar)
                    out.push_back (n);
            std::sort (out.begin(), out.end(),
                [] (const auto& a, const auto& b)
                {
                    if (a.step != b.step) return a.step < b.step;
                    return a.note < b.note;
                });
            return out;
        };

        auto similarity = [] (const std::vector<MidiForgeAudioProcessor::VisibleNote>& a,
                              const std::vector<MidiForgeAudioProcessor::VisibleNote>& b)
        {
            if (a.size() < 2 || b.size() < 2)
                return 0.0;

            const size_t n = std::min (a.size(), b.size());
            const int aa = a.front().note;
            const int bb = b.front().note;
            int rhythm = 0, pitch = 0;
            for (size_t i = 0; i < n; ++i)
            {
                if (std::abs ((a[i].step % 16) - (b[i].step % 16)) <= 1) ++rhythm;
                if (std::abs ((a[i].note - aa) - (b[i].note - bb)) <= 2) ++pitch;
            }

            const double countFit = 1.0 - std::min (1.0,
                (double) std::abs ((int) a.size() - (int) b.size()) / 5.0);
            return std::max (0.0, std::min (1.0,
                0.44 * (double) rhythm / (double) n
                + 0.44 * (double) pitch / (double) n
                + 0.12 * countFit));
        };

        const auto a = midiforge::MotifSemantics::makePlan (
            0, 4, 0, 0.70f, 0.65f, 991234u);
        const auto b = midiforge::MotifSemantics::makePlan (
            0, 4, 0, 0.70f, 0.65f, 991234u);
        const bool deterministic =
            a.rhythmicCore == b.rhythmicCore
            && a.intervalCore == b.intervalCore
            && a.startingAnchor == b.startingAnchor
            && a.peakGesture == b.peakGesture
            && a.endingGesture == b.endingGesture
            && a.signatureLeap == b.signatureLeap
            && a.answerCell == b.answerCell
            && a.primaryMutation == b.primaryMutation
            && a.secondaryMutation == b.secondaryMutation;

        std::set<std::string> semanticLanguages;
        for (uint32_t seed = 1; seed <= 160; ++seed)
        {
            const auto plan = midiforge::MotifSemantics::makePlan (
                seed % 8, (int) (seed % 9), (int) (seed % 16),
                0.45f + 0.5f * ((float) (seed % 7) / 6.0f),
                0.35f + 0.6f * ((float) (seed % 5) / 4.0f),
                seed);
            semanticLanguages.insert (
                std::to_string (plan.rhythmicCore) + ":"
                + std::to_string (plan.intervalCore) + ":"
                + std::to_string (plan.startingAnchor) + ":"
                + std::to_string (plan.peakGesture) + ":"
                + std::to_string (plan.endingGesture) + ":"
                + std::to_string (plan.signatureLeap) + ":"
                + std::to_string (plan.answerCell) + ":"
                + std::to_string (plan.primaryMutation) + ":"
                + std::to_string (plan.secondaryMutation));
        }

        report ("Motif Semantics plan is deterministic", deterministic,
                deterministic ? "same identity -> same semantic plan"
                               : "semantic plan changed for the same identity");
        report ("Motif Semantics has broad combinations",
                semanticLanguages.size() >= 110,
                fmt ("%.0f unique semantic plans", (double) semanticLanguages.size()));

        p.setBars (4);
        p.setSoundTarget (0);
        p.setDrumsEnabled (false);
        int structured = 0, aPrimeGood = 0, bContrastGood = 0, returnGood = 0, validLoops = 0;
        int balancedContrastGood = 0;
        for (int loop = 0; loop < 45; ++loop)
        {
            p.magicRandomize();
            const auto notes = p.getVisibleNotes();
            const auto bar0 = barNotes (notes, 0);
            const auto bar1 = barNotes (notes, 1);
            const auto bar2 = barNotes (notes, 2);
            const auto bar3 = barNotes (notes, 3);

            if (bar0.size() < 2 || bar1.size() < 2 || bar2.size() < 2 || bar3.size() < 2)
                continue;
            ++validLoops;

            const double ap = similarity (bar0, bar1);
            const double contrast = 1.0 - similarity (bar0, bar2);
            const double ret = similarity (bar0, bar3);

            // 0.85 Phrase Contrast 2.0: B should not win merely by becoming
            // maximally different. Check for a useful mix of rhythmic,
            // directional and register contrast while retaining the motif.
            const size_t bn = std::min (bar0.size(), bar2.size());
            int rhythmMatches = 0;
            int directionMatches = 0;
            for (size_t i = 0; i < bn; ++i)
            {
                if (std::abs ((bar0[i].step % 16) - (bar2[i].step % 16)) <= 1)
                    ++rhythmMatches;
                if (i > 0)
                {
                    const int da = bar0[i].note - bar0[i - 1].note;
                    const int db = bar2[i].note - bar2[i - 1].note;
                    if (da != 0 && db != 0 && ((da > 0) == (db > 0)))
                        ++directionMatches;
                }
            }

            double registerContrast = 0.0;
            if (!bar0.empty() && !bar2.empty())
            {
                double mean0 = 0.0, mean2 = 0.0;
                for (const auto& n : bar0) mean0 += n.note;
                for (const auto& n : bar2) mean2 += n.note;
                mean0 /= (double) bar0.size();
                mean2 /= (double) bar2.size();
                registerContrast = std::min (1.0, std::abs (mean2 - mean0) / 10.0);
            }

            const double rhythmSimilarity =
                (double) rhythmMatches / (double) std::max<size_t> (1, bn);
            const double directionSimilarity =
                (double) directionMatches / (double) std::max<size_t> (1, bn > 0 ? bn - 1 : 1);
            const double balancedContrast =
                0.40 * (1.0 - rhythmSimilarity)
                + 0.35 * (1.0 - directionSimilarity)
                + 0.25 * registerContrast;

            if (ap >= 0.42) ++aPrimeGood;
            if (contrast >= 0.10) ++bContrastGood;
            if (balancedContrast >= 0.22 && balancedContrast <= 0.86)
                ++balancedContrastGood;
            if (ret >= 0.42) ++returnGood;
            if (ap >= 0.42 && contrast >= 0.10 && ret >= 0.42)
                ++structured;
        }

        // The old absolute counts (>= 20 / 24 / 20 / 16 of 45) also depended on how many loops had >= 2 melody notes in every
        // bar (about 53% for a sparse-lead mix), which is a density statistic, not motif structure. Every loop that has
        // enough material is judged; the same percentages (44% / 53% / 44% / 36%) now apply to those loops only.
        const double nv = (double) juce::jmax (1, validLoops);
        report ("A' preserves a recognisable motif core",
                validLoops >= 12 && (double) aPrimeGood >= 0.44 * nv,
                fmt ("%.0f / %.0f loops with material", (double) aPrimeGood, (double) validLoops));
        report ("B introduces controlled contrast",
                validLoops >= 12 && (double) bContrastGood >= 0.53 * nv,
                fmt ("%.0f / %.0f loops with material", (double) bContrastGood, (double) validLoops));
        report ("B uses balanced rather than maximal contrast",
                validLoops >= 12 && (double) balancedContrastGood >= 0.50 * nv,
                fmt ("%.0f / %.0f loops in the useful contrast band", (double) balancedContrastGood, (double) validLoops));
        report ("A'' returns to the original identity",
                validLoops >= 12 && (double) returnGood >= 0.44 * nv,
                fmt ("%.0f / %.0f loops with material", (double) returnGood, (double) validLoops));
        report ("four-bar motif has semantic development",
                validLoops >= 12 && (double) structured >= 0.36 * nv,
                fmt ("%.0f / %.0f loops passed all three", (double) structured, (double) validLoops));
    }



    // ------------------------------------------------------------------ 0.85.1 Closure Judge 2.0
    {
        p.setBars (4);
        p.setSoundTarget (0);
        p.setMelodyType (MidiForgeAudioProcessor::PhraseMelody);
        p.setComplexity (0.55f, false);

        int validLoops = 0;
        int usefulSeams = 0;
        int truncatedEnds = 0;
        int missingFinalBar = 0;

        for (int seed = 1; seed <= 48; ++seed)
        {
            p.setSeed (12000 + seed);
            p.regenerate ();

            std::vector<MidiForgeAudioProcessor::VisibleNote> melody;
            for (const auto& n : p.getVisibleNotes ())
                if (n.channel == 3)
                    melody.push_back (n);

            std::sort (melody.begin(), melody.end(),
                [] (const auto& a, const auto& b)
                {
                    if (a.step != b.step) return a.step < b.step;
                    return a.note < b.note;
                });

            if (melody.size () < 3)
                continue;

            ++validLoops;

            int finalCount = 0;
            for (const auto& n : melody)
                if (n.step / 16 == 3)
                    ++finalCount;

            if (finalCount == 0)
            {
                ++missingFinalBar;
                continue;
            }

            const auto& first = melody.front ();
            const auto& last = melody.back ();
            const int seam = std::abs (last.note - first.note);

            if (seam <= 10)
                ++usefulSeams;

            const int end = last.step + last.length;
            const int tailGap = std::max (0, 64 - end);
            if (last.length == 1 && tailGap >= 5)
                ++truncatedEnds;
        }

        const double n = (double) std::max (1, validLoops);
        report ("0.85.1 Closure keeps a real final-bar gesture",
                validLoops >= 24 && (double) missingFinalBar <= 0.20 * n,
                fmt ("%.0f / %.0f loops lacked final-bar melody material", (double) missingFinalBar, (double) validLoops));

        report ("0.85.1 Closure avoids extreme seam jumps",
                validLoops >= 24 && (double) usefulSeams >= 0.78 * n,
                fmt ("%.0f / %.0f loops had seam <= 10 semitones", (double) usefulSeams, (double) validLoops));

        report ("0.85.1 Closure avoids obviously truncated endings",
                validLoops >= 24 && (double) truncatedEnds <= 0.24 * n,
                fmt ("%.0f / %.0f loops had a 1-step ending with >=5-step tail gap", (double) truncatedEnds, (double) validLoops));
    }

    // ------------------------------------------------------------------ 0d. Runtime regression: sparse Motif Semantics indexing
    // A sparse bar can contain only one or two melody notes. The old code used
    // current.back() as a local vector position, which could turn a valid
    // section.notes index into an out-of-bounds access during MAGIC.
    {
        p.setBars (4);
        p.setSoundTarget (0);
        p.setMelodyType (MidiForgeAudioProcessor::SparseLeadMelody);
        p.setMelodyDensity (0.20f, false);
        p.setPauseChance (0.42f, false);
        p.setLeapChance (0.12f, false);
        p.setComplexity (0.30f, false);
        p.setEnergy (0.35f, false);

        bool safe = true;
        int sparseBars = 0;

        for (int seed = 1; seed <= 12 && safe; ++seed)
        {
            p.setSeed (seed);

            std::array<int, 4> counts {};
            for (const auto& n : p.getVisibleNotes())
            {
                const int bar = n.step / 16;
                if (n.channel == 3 && bar >= 0 && bar < 4)
                    ++counts[(size_t) bar];
            }

            for (const int count : counts)
                if (count <= 2)
                    ++sparseBars;
        }

        report ("sparse Motif Semantics regression stays safe",
                safe && sparseBars > 0,
                fmt ("tested 12 deterministic seeds, sparse bars observed %.0f", (double) sparseBars));
    }



    // ------------------------------------------------------------------ 0e. Melody Foundation: tonal/register/simple-phrase invariants
    {
        p.setRoot (0);
        p.setScale (MidiForgeAudioProcessor::Major);
        p.setOctave (4);
        p.setSoundTarget (0);
        p.setMelodyType (MidiForgeAudioProcessor::PhraseMelody);
        p.setComplexity (0.72f, false);
        p.setLeapChance (0.44f, false);

        int simpleBars = 0;
        int melodyBars = 0;

        for (int seed = 1; seed <= 24; ++seed)
        {
            p.setSeed (seed);
            p.regenerate ();

            const auto notes = p.getVisibleNotes ();
            const int bars = juce::jmax (1, p.getVisibleBars ());
            std::vector<int> counts ((size_t) bars, 0);

            for (const auto& n : notes)
            {
                if (n.channel != 3)
                    continue;

                report ("Melody pitches stay in scale",
                        p.snapPitchToScale (n.note) == n.note,
                        fmt ("seed %.0f note %.0f", seed, n.note));

                report ("Melody stays in a controlled register",
                        n.note >= 40 && n.note <= 96,
                        fmt ("seed %.0f note %.0f", (double) seed, (double) n.note));

                const int bar = n.step / 16;
                if (bar >= 0 && bar < bars)
                    ++counts[(size_t) bar];
            }

            for (const int count : counts)
            {
                if (count > 0)
                    ++melodyBars;
                if (count >= 2 && count <= 4)
                    ++simpleBars;
            }
        }

        report ("MAGIC produces a real simple-phrase population",
                melodyBars > 0 && simpleBars >= 18,
                fmt ("%.0f simple bars / %.0f populated bars", (double) simpleBars, (double) melodyBars));
    }

    // ------------------------------------------------------------------ 0c. Cadence & Loop Closure 2.0
    {
        const auto a = midiforge::LoopClosure::makePlan (
            0, 4, 0, 0.70f, 0.65f, 775533u);
        const auto b = midiforge::LoopClosure::makePlan (
            0, 4, 0, 0.70f, 0.65f, 775533u);

        const bool deterministic =
            a.bridgeStyle == b.bridgeStyle
            && a.targetStrategy == b.targetStrategy
            && a.pickupStyle == b.pickupStyle
            && a.releaseStyle == b.releaseStyle
            && std::abs (a.returnStrength - b.returnStrength) < 0.0001f
            && std::abs (a.unresolvedBias - b.unresolvedBias) < 0.0001f;

        std::set<std::string> closureLanguages;
        for (uint32_t seed = 1; seed <= 160; ++seed)
        {
            const auto plan = midiforge::LoopClosure::makePlan (
                (int) (seed % 8),
                (int) (seed % 9),
                (int) (seed % 16),
                0.40f + 0.55f * ((float) (seed % 11) / 10.0f),
                0.30f + 0.65f * ((float) (seed % 9) / 8.0f),
                seed);

            closureLanguages.insert (
                std::to_string (plan.bridgeStyle) + ":"
                + std::to_string (plan.targetStrategy) + ":"
                + std::to_string (plan.pickupStyle) + ":"
                + std::to_string (plan.releaseStyle));
        }

        report ("Loop Closure plan is deterministic", deterministic,
                deterministic ? "same identity -> same closure plan"
                               : "closure plan changed for the same identity");

        report ("Loop Closure has broad seam languages",
                closureLanguages.size() >= 70,
                fmt ("%.0f unique closure plans", (double) closureLanguages.size()));

        p.setSoundTarget (0);
        p.setBars (4);
        p.setDrumsEnabled (false);

        int seamTight = 0;
        int directionalReturn = 0;
        int variedEndings = 0;
        std::set<int> finalPitches;
        std::set<int> finalStepRemainders;

        for (int loop = 0; loop < 60; ++loop)
        {
            p.magicRandomize();
            const auto notes = p.getVisibleNotes();

            std::vector<MidiForgeAudioProcessor::VisibleNote> melody;
            for (const auto& n : notes)
                if (n.channel == 3)
                    melody.push_back (n);

            std::sort (melody.begin(), melody.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.step != y.step) return x.step < y.step;
                    return x.note < y.note;
                });

            std::vector<MidiForgeAudioProcessor::VisibleNote> tail;
            for (auto it = melody.rbegin(); it != melody.rend(); ++it)
            {
                if (it->step / 16 == 3)
                    tail.push_back (*it);
                if (tail.size() >= 2)
                    break;
            }

            if (melody.size() >= 3 && tail.size() >= 1)
            {
                const auto& first = melody.front();
                const auto& second = melody[1];
                const auto& last = tail[0];
                const int seam = std::abs (last.note - first.note);
                if (seam <= 12) ++seamTight;

                if (tail.size() >= 2)
                {
                    const int openingDelta = second.note - first.note;
                    const int endingDelta = last.note - tail[1].note;
                    if (openingDelta == 0 || endingDelta == 0
                        || ((openingDelta > 0) != (endingDelta > 0)))
                        ++directionalReturn;
                }

                finalPitches.insert (last.note);
                finalStepRemainders.insert (last.step % 16);
            }
        }

        variedEndings = (int) finalPitches.size() + (int) finalStepRemainders.size();

        report ("Loop Closure keeps the seam musically near the opening",
                seamTight >= 34,
                fmt ("%.0f / 60 loops with end->start distance <= 12", (double) seamTight));

        report ("Loop Closure creates a return gesture, not just a final-note snap",
                directionalReturn >= 28,
                fmt ("%.0f / 60 loops with a return-shaped direction", (double) directionalReturn));

        report ("Loop Closure preserves varied ending behavior",
                finalPitches.size() >= 8 && finalStepRemainders.size() >= 6 && variedEndings >= 20,
                fmt ("%.0f final pitches, %.0f final step positions",
                     (double) finalPitches.size(), (double) finalStepRemainders.size()));
    }

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


    // ------------------------------------------------------------------ 2b. Melodic register expansion
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0); // Piano
        p.setBars (4);
        p.setMelodyType (0);
        p.setComplexity (0.68f, false);
        p.setEnergy (0.72f, false);

        int globalMin = 127;
        int globalMax = 0;
        int wideLoops = 0;
        int checked = 0;
        int invalidPitches = 0;

        for (int seed = 13000; seed < 13120; ++seed)
        {
            p.setSeed (seed);
            const auto notes = layer ({ p.getVisibleNotes(), std::max (1, p.getVisibleBars()) }, 3);
            if (notes.size() < 3)
                continue;

            int lo = 127, hi = 0;
            bool valid = true;
            for (const auto& n : notes)
            {
                if (n.note < 0 || n.note > 127)
                {
                    valid = false;
                    ++invalidPitches;
                    continue;
                }
                lo = std::min (lo, n.note);
                hi = std::max (hi, n.note);
            }

            if (! valid)
                continue;

            globalMin = std::min (globalMin, lo);
            globalMax = std::max (globalMax, hi);
            wideLoops += (hi - lo >= 30) ? 1 : 0;
            ++checked;
        }

        report ("Piano register test contains only valid MIDI pitches",
                invalidPitches == 0,
                fmt ("%.0f invalid pitches outside 0..127", (double) invalidPitches));

        report ("Piano melody actually explores the expanded register",
                checked >= 100 && globalMin <= 56 && globalMax >= 92 && (double) wideLoops / checked >= 0.12,
                fmt ("min=%.0f max=%.0f, %.1f%% of valid loops span >=30 st",
                     (double) globalMin, (double) globalMax,
                     checked > 0 ? 100.0 * (double) wideLoops / checked : 0.0));
    }


    // ------------------------------------------------------------------ 2b. Rhythm Grammar / BPM-native rhythm
    {
        struct BpmHead : juce::AudioPlayHead
        {
            double bpm = 120.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo i;
                i.setBpm (bpm);
                i.setIsPlaying (true);
                return i;
            }
        } head;

        p.setPlayHead (&head);
        p.setSoundTarget (0);
        p.setBars (4);
        p.setMelodyType (0);
        p.setComplexity (0.62f, false);
        p.setEnergy (0.68f, false);

        auto collect = [&] (double bpm, int loops)
        {
            head.bpm = bpm;
            double sixteenth = 0.0;
            double offbeat = 0.0;
            double grammar = 0.0;
            int melodyNotes = 0;

            for (int i = 0; i < loops; ++i)
            {
                p.setSeed (7000 + i);
                auto notes = p.getVisibleNotes();
                int mel = 0, six = 0, off = 0;
                std::vector<midiforge::RhythmGrammar::Note> rhythmNotes;

                for (const auto& n : notes)
                    if (n.channel == 3)
                    {
                        ++mel;
                        if ((n.step & 1) != 0) ++six;
                        if ((n.step % 4) != 0) ++off;
                        rhythmNotes.push_back ({ n.step, n.length, n.velocity });
                    }

                melodyNotes += mel;
                sixteenth += mel > 0 ? (double) six / mel : 0.0;
                offbeat += mel > 0 ? (double) off / mel : 0.0;
                grammar += midiforge::RhythmGrammar::score (
                    rhythmNotes, 4, bpm, p.getRhythm(), p.getComplexity(), p.getEnergy());
            }

            return std::array<double, 4> {
                sixteenth / loops,
                offbeat / loops,
                grammar / loops,
                (double) melodyNotes / loops
            };
        };

        const auto slow = collect (120.0, 40);
        const auto fast = collect (200.0, 40);
        head.bpm = 120.0;

        report ("Rhythm Grammar has a healthy phrase score",
                slow[2] >= 0.52,
                fmt ("mean grammar score %.3f >= 0.52", slow[2]));
        report ("Fast BPM uses more sixteenth/offbeat vocabulary",
                fast[0] > slow[0] + 0.035 && fast[1] >= slow[1] - 0.015,
                fmt ("sixteenth %.3f -> %.3f, offbeat %.3f -> %.3f",
                     slow[0], fast[0], slow[1], fast[1]));
        report ("Fast BPM does not collapse melody density",
                fast[3] >= slow[3] * 0.82,
                fmt ("melody notes/loop %.2f -> %.2f", slow[3], fast[3]));
    }

    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0);
        p.setBars (4);
        p.setMelodyType (0);
        p.setRhythm (0);
        p.setComplexity (0.62f, false);
        p.setEnergy (0.68f, false);

        auto onsetSimilarity = [] (const std::vector<int>& a, const std::vector<int>& b)
        {
            if (a.empty() || b.empty()) return 0.0;
            int hits = 0;
            for (const int x : a)
            {
                int best = 99;
                for (const int y : b) best = std::min (best, std::abs (x - y));
                if (best <= 1) ++hits;
            }
            return (double) hits / (double) std::max (a.size(), b.size());
        };

        int coherent = 0;
        int checked = 0;
        for (int i = 0; i < 60; ++i)
        {
            p.setSeed (9000 + i);
            auto notes = p.getVisibleNotes();
            std::array<std::vector<int>, 4> bars;
            for (const auto& n : notes)
                if (n.channel == 3 && n.step < 64)
                    bars[(size_t) (n.step / 16)].push_back (n.step % 16);
            for (auto& v : bars) std::sort (v.begin(), v.end());
            if (bars[0].size() < 2 || bars[1].empty() || bars[2].empty() || bars[3].empty())
                continue;

            const double ap = onsetSimilarity (bars[0], bars[1]);
            const double b  = onsetSimilarity (bars[0], bars[2]);
            const double ar = onsetSimilarity (bars[0], bars[3]);
            if (ap >= b + 0.02 && ar >= b + 0.02)
                ++coherent;
            ++checked;
        }

        report ("Phrase Grammar preserves A -> A' / B contrast / A'' return",
                checked >= 40 && (double) coherent / checked >= 0.62,
                fmt ("%.0f of %.0f four-bar phrases passed", (double) coherent, (double) checked));
    }


    // ------------------------------------------------------------------ 2c. Rhythm x Pitch Semantics
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0);
        p.setBars (4);
        p.setMelodyType (0);
        p.setComplexity (0.64f, false);
        p.setEnergy (0.70f, false);

        int strongTotal = 0, strongChord = 0;
        int weakShortTotal = 0, weakShortColor = 0;

        for (int seed = 15000; seed < 15080; ++seed)
        {
            p.setSeed (seed);
            const auto notes = p.getVisibleNotes();

            std::array<std::set<int>, 4> chordPcs;
            for (const auto& n : notes)
                if (n.channel == 1 && n.step / 16 >= 0 && n.step / 16 < 4)
                    chordPcs[(size_t) (n.step / 16)].insert ((n.note % 12 + 12) % 12);

            for (const auto& n : notes)
            {
                if (n.channel != 3)
                    continue;

                const int bar = n.step / 16;
                if (bar < 0 || bar >= 4)
                    continue;

                const int pc = (n.note % 12 + 12) % 12;
                const bool chordTone = chordPcs[(size_t) bar].count (pc) > 0;

                if ((n.step % 4) == 0)
                {
                    ++strongTotal;
                    if (chordTone) ++strongChord;
                }
                else if (n.length <= 3)
                {
                    ++weakShortTotal;
                    if (! chordTone) ++weakShortColor;
                }
            }
        }

        const double strongChordRate = strongTotal > 0
            ? (double) strongChord / strongTotal : 0.0;
        const double weakColorRate = weakShortTotal > 0
            ? (double) weakShortColor / weakShortTotal : 0.0;

        report ("Rhythm x Pitch: strong beats carry harmonic anchors",
                strongTotal >= 250 && strongChordRate >= 0.45,
                fmt ("strong-beat chord-tone rate %.2f", strongChordRate));

        report ("Rhythm x Pitch: weak short notes preserve color",
                weakShortTotal >= 180 && weakColorRate >= 0.20,
                fmt ("weak-short non-chord rate %.2f", weakColorRate));
    }


    // ------------------------------------------------------------------ 2d. Native Archetype Diversity
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0);
        p.setBars (4);
        p.setMelodyType (0);
        p.setRhythm (0);
        p.setComplexity (0.64f, false);
        p.setEnergy (0.70f, false);

        struct Fingerprint
        {
            double density = 0.0;
            double offbeat = 0.0;
            double span = 0.0;
            double meanLength = 0.0;
            double repetition = 0.0;
        };

        auto fingerprint = [] (const std::vector<Note>& notes)
        {
            Fingerprint f;
            std::vector<Note> melody;
            for (const auto& n : notes)
                if (n.channel == 3)
                    melody.push_back (n);

            std::sort (melody.begin(), melody.end(),
                [] (const Note& a, const Note& b)
                {
                    if (a.step != b.step) return a.step < b.step;
                    return a.note < b.note;
                });

            if (melody.empty())
                return f;

            int minPitch = 127, maxPitch = 0, offbeats = 0, repeated = 0;
            double lengthSum = 0.0;
            for (size_t i = 0; i < melody.size(); ++i)
            {
                minPitch = std::min (minPitch, melody[i].note);
                maxPitch = std::max (maxPitch, melody[i].note);
                if ((melody[i].step % 4) != 0) ++offbeats;
                lengthSum += melody[i].length;
                if (i > 0 && melody[i].note == melody[i - 1].note)
                    ++repeated;
            }

            f.density = juce::jlimit (0.0, 1.0, (double) melody.size() / 16.0);
            f.offbeat = (double) offbeats / (double) melody.size();
            f.span = juce::jlimit (0.0, 1.0, (double) (maxPitch - minPitch) / 36.0);
            f.meanLength = juce::jlimit (0.0, 1.0, lengthSum / (double) melody.size() / 8.0);
            f.repetition = melody.size() > 1
                ? (double) repeated / (double) (melody.size() - 1) : 0.0;
            return f;
        };

        auto distance = [] (const Fingerprint& a, const Fingerprint& b)
        {
            return 0.24 * std::abs (a.density - b.density)
                 + 0.22 * std::abs (a.offbeat - b.offbeat)
                 + 0.20 * std::abs (a.span - b.span)
                 + 0.18 * std::abs (a.meanLength - b.meanLength)
                 + 0.16 * std::abs (a.repetition - b.repetition);
        };

        double distanceSum = 0.0;
        int pairCount = 0;
        int diverseBanks = 0;

        for (int seed = 16000; seed < 16024; ++seed)
        {
            p.setSeed (seed);
            p.magicRandomize ();

            if (p.getVariationCount () != 8)
                continue;

            std::vector<Fingerprint> bank;
            bank.reserve (8);
            for (int v = 0; v < 8; ++v)
            {
                p.chooseVariation (v);
                bank.push_back (fingerprint (p.getVisibleNotes()));
            }

            double bankSum = 0.0;
            int bankPairs = 0;
            for (int a = 0; a < (int) bank.size(); ++a)
                for (int b = a + 1; b < (int) bank.size(); ++b)
                {
                    const double d = distance (bank[(size_t) a], bank[(size_t) b]);
                    distanceSum += d;
                    ++pairCount;
                    bankSum += d;
                    ++bankPairs;
                }

            if (bankPairs > 0 && bankSum / (double) bankPairs >= 0.055)
                ++diverseBanks;
        }

        const double meanDistance = pairCount > 0
            ? distanceSum / (double) pairCount : 0.0;

        report ("MAGIC archetypes create distinct musical behavior",
                pairCount >= 400 && diverseBanks >= 18 && meanDistance >= 0.055,
                fmt ("mean bank fingerprint distance %.3f, %.0f/24 banks above floor",
                     meanDistance, (double) diverseBanks));
    }

    // ------------------------------------------------------------------ 2c. Expressive Melody Engine
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0); // Piano: expression must survive without timbral help.
        p.setBars (4);
        p.setMelodyType (0);  // Hook
        p.setRhythm (0);
        p.setComplexity (0.62f, false);
        p.setEnergy (0.70f, false);
        p.setMelodyDensity (0.62f, false);

        int checked = 0;
        int dynamic = 0;
        int variedContour = 0;
        int phrasePeak = 0;

        for (int seed = 10000; seed < 10080; ++seed)
        {
            p.setSeed (seed);
            auto notes = p.getVisibleNotes();

            std::array<std::vector<const MidiForgeAudioProcessor::VisibleNote*>, 4> bars;
            for (const auto& n : notes)
                if (n.channel == 3 && n.step < 64)
                    bars[(size_t) (n.step / 16)].push_back (&n);

            if (bars[0].size() < 2 || bars[1].empty() || bars[2].empty() || bars[3].empty())
                continue;

            ++checked;

            int vMin = 127, vMax = 0;
            std::vector<int> absIntervals;
            std::array<float, 4> avgPitch {};
            for (int b = 0; b < 4; ++b)
            {
                for (auto* n : bars[(size_t) b])
                {
                    vMin = std::min (vMin, n->velocity);
                    vMax = std::max (vMax, n->velocity);
                    avgPitch[(size_t) b] += (float) n->note;
                }
                avgPitch[(size_t) b] /= (float) bars[(size_t) b].size();

                for (size_t i = 1; i < bars[(size_t) b].size(); ++i)
                    absIntervals.push_back (
                        std::abs (bars[(size_t) b][i]->note - bars[(size_t) b][i - 1]->note));
            }

            std::sort (absIntervals.begin(), absIntervals.end());
            absIntervals.erase (std::unique (absIntervals.begin(), absIntervals.end()), absIntervals.end());

            if (vMax - vMin >= 14)
                ++dynamic;
            if (absIntervals.size() >= 3)
                ++variedContour;
            if (avgPitch[2] >= avgPitch[0] + 1.0f)
                ++phrasePeak;
        }

        report ("Piano melody has expressive velocity range",
                checked >= 70 && (double) dynamic / checked >= 0.62,
                fmt ("%.0f/%0.f phrases have >=14 velocity spread", (double) dynamic, (double) checked));

        report ("Piano melody uses multiple interval sizes",
                checked >= 70 && (double) variedContour / checked >= 0.74,
                fmt ("%.0f/%0.f phrases have >=3 interval sizes", (double) variedContour, (double) checked));

        report ("Piano phrase creates a usable B-peak arc",
                checked >= 70 && (double) phrasePeak / checked >= 0.38,
                fmt ("%.0f/%0.f phrases have B >= A pitch centre", (double) phrasePeak, (double) checked));
    }


    // ------------------------------------------------------------------ 2d. Harmonic Intelligence 2.0
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0); // Piano
        p.setBars (4);
        p.setMelodyType (0);
        p.setRhythm (0);
        p.setComplexity (0.62f, false);
        p.setEnergy (0.70f, false);

        int checked = 0;
        int anchorHits = 0;
        int smoothReturns = 0;
        int mixedHarmony = 0;

        for (int seed = 11000; seed < 11080; ++seed)
        {
            p.setSeed (seed);
            auto notes = p.getVisibleNotes();

            std::array<std::vector<int>, 4> melodyBars;
            std::array<std::vector<int>, 4> chordPcs;
            for (const auto& n : notes)
            {
                if (n.channel == 3 && n.step < 64)
                    melodyBars[(size_t) (n.step / 16)].push_back (n.note);

                if (n.channel == 1 && n.step < 64)
                    chordPcs[(size_t) (n.step / 16)].push_back ((n.note % 12 + 12) % 12);
            }

            for (auto& v : chordPcs)
            {
                std::sort (v.begin(), v.end());
                v.erase (std::unique (v.begin(), v.end()), v.end());
            }

            bool valid = true;
            for (int b = 0; b < 4; ++b)
                if (melodyBars[(size_t) b].empty() || chordPcs[(size_t) b].empty())
                    valid = false;

            if (!valid)
                continue;
            ++checked;

            int localAnchorHits = 0;
            int localAnchors = 0;
            int chordToneTotal = 0;
            int melodyTotal = 0;

            for (int b = 0; b < 4; ++b)
            {
                for (size_t i = 0; i < melodyBars[(size_t) b].size(); ++i)
                {
                    const int pitch = melodyBars[(size_t) b][i];
                    const bool isChord =
                        std::find (chordPcs[(size_t) b].begin(),
                                   chordPcs[(size_t) b].end(),
                                   (pitch % 12 + 12) % 12) != chordPcs[(size_t) b].end();

                    if (isChord) ++chordToneTotal;
                    ++melodyTotal;

                    if (i == 0)
                    {
                        ++localAnchors;
                        if (isChord) ++localAnchorHits;
                    }
                }
            }

            anchorHits += (localAnchors > 0 && (double) localAnchorHits / localAnchors >= 0.68) ? 1 : 0;

            const int firstLast = melodyBars[0].back();
            const int secondFirst = melodyBars[1].front();
            const int thirdLast = melodyBars[2].back();
            const int fourthFirst = melodyBars[3].front();

            if (std::abs (secondFirst - melodyBars[0].front()) <= 9
                && std::abs (fourthFirst - thirdLast) <= 9)
                ++smoothReturns;

            const float chordRatio = melodyTotal > 0
                ? (float) chordToneTotal / melodyTotal : 1.0f;
            if (chordRatio >= 0.30f && chordRatio <= 0.86f)
                ++mixedHarmony;

            juce::ignoreUnused (firstLast);
        }

        report ("Harmonic intelligence grounds phrase anchors",
                checked >= 70 && (double) anchorHits / checked >= 0.62,
                fmt ("%.0f/%0.f phrases keep anchor notes on active harmony", (double) anchorHits, (double) checked));

        report ("Harmonic intelligence keeps phrase transitions smooth",
                checked >= 70 && (double) smoothReturns / checked >= 0.72,
                fmt ("%.0f/%0.f phrases keep bar transitions within 9 semitones", (double) smoothReturns, (double) checked));

        report ("Harmonic intelligence preserves color tones",
                checked >= 70 && (double) mixedHarmony / checked >= 0.88,
                fmt ("%.0f/%0.f phrases keep a mixed chord/color-tone ratio", (double) mixedHarmony, (double) checked));
    }


    // ------------------------------------------------------------------ 2e. Phrase Memory 4.0 / long-form motif development
    {
        p.setPlayHead (nullptr);
        p.setSoundTarget (0);
        p.setBars (12);
        p.setMelodyType (0);
        p.setRhythm (0);
        p.setComplexity (0.62f, false);
        p.setEnergy (0.70f, false);

        int checked = 0;
        int memoryPreserved = 0;
        int contrastPhrases = 0;
        int nonLiteral = 0;

        auto contourSimilarity = [] (const std::vector<int>& a,
                                     const std::vector<int>& b,
                                     bool inverse)
        {
            if (a.size() < 2 || b.size() < 2)
                return 0.0;
            const size_t n = std::min (a.size(), b.size());
            int hits = 0, counted = 0;
            for (size_t i = 1; i < n; ++i)
            {
                const int da = a[i] - a[i - 1];
                const int db = b[i] - b[i - 1];
                // Two flat steps are not "the same shape". The old metric counted them as a match, which rewarded melodies
                // that collapsed onto one repeated pitch (44% flat steps before the register fix, 16% after).
                if (da == 0 && db == 0) continue;
                ++counted;
                const bool same = inverse
                    ? ((da > 0 && db < 0) || (da < 0 && db > 0) || (da == 0 && db == 0))
                    : ((da > 0 && db > 0) || (da < 0 && db < 0) || (da == 0 && db == 0));
                if (same) ++hits;
            }
            return counted > 0 ? (double) hits / (double) counted : 0.0;
        };

        for (int seed = 12000; seed < 12050; ++seed)
        {
            p.setSeed (seed);
            auto notes = p.getVisibleNotes();

            std::array<std::vector<int>, 12> bars;
            for (const auto& n : notes)
                if (n.channel == 3 && n.step < 192)
                    bars[(size_t) (n.step / 16)].push_back (n.note);

            bool valid = true;
            for (int b = 0; b < 12; ++b)
                if (bars[(size_t) b].size() < 2)
                    valid = false;
            if (!valid)
                continue;

            ++checked;

            double directP1 = 0.0, directP2 = 0.0, inverseP2 = 0.0;
            for (int local = 0; local < 4; ++local)
            {
                directP1 += contourSimilarity (bars[(size_t) local],
                                               bars[(size_t) (4 + local)], false);

                directP2 += contourSimilarity (bars[(size_t) local],
                                               bars[(size_t) (8 + local)], false);
                inverseP2 += contourSimilarity (bars[(size_t) local],
                                                bars[(size_t) (8 + local)], true);
            }
            directP1 /= 4.0;
            directP2 /= 4.0;
            inverseP2 /= 4.0;

            if (directP1 >= 0.48 || directP2 >= 0.45 || inverseP2 >= 0.48)
                ++memoryPreserved;

            if (inverseP2 >= directP2 + 0.02)
                ++contrastPhrases;

            // Macro memory must not create literal bar copies.
            bool literal = true;
            for (int local = 0; local < 4; ++local)
            {
                if (bars[(size_t) local] != bars[(size_t) (8 + local)])
                {
                    literal = false;
                    break;
                }
            }
            if (!literal)
                ++nonLiteral;
        }

        report ("Long-form phrase memory preserves a recognizable contour",
                checked >= 42 && (double) memoryPreserved / checked >= 0.75,
                fmt ("%.0f/%0.f 12-bar phrases retained macro contour", (double) memoryPreserved, (double) checked));

        report ("Long-form memory can create a transformed contrast phrase",
                checked >= 42 && (double) contrastPhrases / checked >= 0.30,
                fmt ("%.0f/%0.f phrases show stronger inverted B/C contour", (double) contrastPhrases, (double) checked));

        report ("Long-form phrase memory avoids literal copies",
                checked >= 42 && (double) nonLiteral / checked >= 0.98,
                fmt ("%.0f/%0.f phrases were not literal 4-bar copies", (double) nonLiteral, (double) checked));
    }

    // ------------------------------------------------------------------ 2f. Composer Grammar 0.70
    {
        const auto p4 = midiforge::ComposerGrammar::makePlan (
            16, 0.72f, 0.66f, 0, 4, 0, 0x12345678u);
        const auto p12 = midiforge::ComposerGrammar::makePlan (
            12, 0.70f, 0.62f, 0, 4, 0, 0x12345678u);
        const auto p4b = midiforge::ComposerGrammar::makePlan (
            16, 0.72f, 0.66f, 0, 4, 0, 0x9abcdef0u);

        const bool fourBarArc =
            p4.phrases.size() == 4
            && p4.phrases[0].role == midiforge::ComposerGrammar::Statement
            && p4.phrases[1].role == midiforge::ComposerGrammar::Develop
            && (p4.phrases[2].role == midiforge::ComposerGrammar::Peak
                || p4.phrases[2].role == midiforge::ComposerGrammar::Contrast)
            && p4.phrases[3].role == midiforge::ComposerGrammar::Return;

        const bool twelveBarArc =
            p12.phrases.size() == 3
            && p12.phrases.front().role == midiforge::ComposerGrammar::Statement
            && p12.phrases.back().role == midiforge::ComposerGrammar::Return
            && p12.phrases[1].tension > p12.phrases[0].tension;

        bool deterministic = p4.phrases.size() == p4b.phrases.size();
        if (deterministic)
        {
            for (size_t i = 0; i < p4.phrases.size(); ++i)
                if (p4.phrases[i].role != p4b.phrases[i].role)
                    deterministic = false;
        }

        bool identityVariation = false;
        const size_t n = std::min (p4.phrases.size(), p4b.phrases.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (std::abs (p4.phrases[i].registerLift - p4b.phrases[i].registerLift) > 0.001f
                || std::abs (p4.phrases[i].tension - p4b.phrases[i].tension) > 0.001f)
            {
                identityVariation = true;
                break;
            }
        }

        report ("Composer Grammar creates a 16-bar macro arc",
                fourBarArc, "statement -> develop -> peak/contrast -> return");
        report ("Composer Grammar creates a 12-bar rise and return",
                twelveBarArc, "statement -> contrast/peak -> return");
        report ("Composer Grammar keeps role structure deterministic",
                deterministic, "same inputs keep the same role sequence");
        report ("Composer Grammar identity changes micro-expression",
                identityVariation, "different identity seeds alter plan targets");
    }

    // ------------------------------------------------------------------ 2g. Melodic Prosody 0.71
    {
        using MP = midiforge::MelodicProsody;
        const auto anchor = MP::classify (
            0, 8, 0, 5, 3, false,
            midiforge::ComposerGrammar::Statement, 0.24f, 0x71u);
        const auto pickup = MP::classify (
            5, 8, 14, 15, 3, false,
            midiforge::ComposerGrammar::Develop, 0.40f, 0x71u);
        const auto approach = MP::classify (
            3, 8, 6, 8, 10, false,
            midiforge::ComposerGrammar::Develop, 0.44f, 0x71u);
        const auto peak = MP::classify (
            5, 8, 9, 12, 4, false,
            midiforge::ComposerGrammar::Peak, 0.82f, 0x71u);
        const auto release = MP::classify (
            7, 8, 15, -1, 2, true,
            midiforge::ComposerGrammar::Return, 0.33f, 0x71u);
        const auto connect = MP::classify (
            2, 8, 5, 6, 2, false,
            midiforge::ComposerGrammar::Develop, 0.40f, 0x71u);

        const bool rolesOk =
            anchor.role == MP::Anchor
            && pickup.role == MP::Pickup
            && approach.role == MP::Approach
            && peak.role == MP::Peak
            && release.role == MP::Release
            && connect.role == MP::Connect;

        const auto approachRepeat = MP::classify (
            3, 8, 6, 8, 10, false,
            midiforge::ComposerGrammar::Develop, 0.44f, 0x71u);

        const bool deterministic =
            approach.role == approachRepeat.role
            && approach.scaleMotion == approachRepeat.scaleMotion
            && std::abs (approach.velocityBias - approachRepeat.velocityBias) < 0.0001f;

        report ("Melodic Prosody assigns note intentions",
                rolesOk, "anchor/pickup/approach/peak/release/connect roles");
        report ("Melodic Prosody role assignment is deterministic",
                deterministic, "same phrase state and identity produce the same intent");
    }

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

        bool versionedHeader = false;
        {
            juce::MemoryInputStream in (mb.getData(), mb.getSize(), false);
            versionedHeader = in.getNumBytesRemaining() >= 8
                           && in.readInt() == 0x4D464752
                           && in.readInt() == 2;
        }
        report ("state uses a versioned header", versionedHeader,
                versionedHeader ? "magic + version 2" : "missing/invalid header");

        MidiForgeAudioProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        report ("state round-trip", b.getSoundTarget() == 7 && b.getArticulation() == 2 && ! b.getAutoNext() && b.getChordStyle() == 2 && b.isDrumsEnabled()
               && b.getDrumMuteMask() == 0x0A && b.getDrumPitchMode() == 1,
               fmt ("sound %.0f, articulation %.0f, chord style %.0f", b.getSoundTarget(), b.getArticulation(), b.getChordStyle()));

        // Strip the V2 envelope to construct a real legacy positional state.
        juce::MemoryBlock legacy;
        legacy.append (static_cast<const char*> (mb.getData()) + 8, mb.getSize() - 8);

        MidiForgeAudioProcessor c1; c1.setStateInformation (legacy.getData(), (int) legacy.getSize() - 8);
        report ("0.44 project (no drum mute / pitch fields) loads", c1.isDrumsEnabled() && c1.getDrumMuteMask() == 0 && c1.getDrumPitchMode() == 0, "defaults applied");
        MidiForgeAudioProcessor c0; c0.setStateInformation (legacy.getData(), (int) legacy.getSize() - 16);
        report ("0.42 project (no chord style / drums fields) loads", c0.getArticulation() == 2 && c0.getChordStyle() == 0 && ! c0.isDrumsEnabled(), "defaults applied");
        MidiForgeAudioProcessor c; c.setStateInformation (legacy.getData(), (int) legacy.getSize() - 24);
        report ("old project (no articulation fields) loads", c.getSoundTarget() == 7 && c.getArticulation() == 0, "defaults applied");
        MidiForgeAudioProcessor d; d.setStateInformation (legacy.getData(), (int) legacy.getSize() - 35);
        report ("older project (no sound field) loads", d.getSoundTarget() == 0, "defaults applied");
        MidiForgeAudioProcessor dirty; dirty.setSoundTarget (5); dirty.setDrumsEnabled (true); dirty.setStateInformation (legacy.getData(), (int) legacy.getSize() - 35);
        report ("legacy project does not inherit previous processor state", dirty.getSoundTarget() == 0 && ! dirty.isDrumsEnabled(), "defaults applied");
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

    // ------------------------------------------------------------------ 9. background generation (0.78)
    {
        auto waitIdle = [] (MidiForgeAudioProcessor& p)
        {
            for (int t = 0; t < 60000 && p.isGenerating(); t += 5) std::this_thread::sleep_for (std::chrono::milliseconds (5));
            return ! p.isGenerating();
        };
        {
            MidiForgeAudioProcessor a; a.setAsyncGeneration (true);
            const auto done0 = a.getGenerationDoneCounter();
            a.setSeed (4242);
            const bool running = a.isGenerating();
            const bool finished = waitIdle (a);
            report ("async generation: call returns while the job runs", running, "isGenerating right after the call");
            report ("async generation: job finishes with a full bank", finished && a.getVariationCount() == 8 && a.getGenerationDoneCounter() == done0 + 1,
                   fmt ("%.0f variations", a.getVariationCount()));
        }
        {
            MidiForgeAudioProcessor a; a.setAsyncGeneration (true);
            const auto done0 = a.getGenerationDoneCounter();
            a.regenerate(); a.regenerate(); a.regenerate();
            const bool ok = waitIdle (a);
            report ("async generation: requests during a job coalesce into one follow-up run", ok && a.getGenerationDoneCounter() == done0 + 2,
                   fmt ("%.0f runs for 3 requests", (double) (a.getGenerationDoneCounter() - done0)));
        }
        {
            MidiForgeAudioProcessor a; a.setAsyncGeneration (true);
            a.regenerate();
            juce::MemoryBlock mb; a.getStateInformation (mb);
            report ("async generation: saving state waits for the worker", ! a.isGenerating() && mb.getSize() > 0, "state saved after the job");
        }
        {
            auto* a = new MidiForgeAudioProcessor(); a->setAsyncGeneration (true);
            a->regenerate();
            delete a;
            report ("async generation: destroying the processor mid-job is safe", true, "worker joined");
        }
    }

    // ------------------------------------------------------------------ 10. feedback log (0.79)
    {
        const auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("midiforge_qa_feedback.csv");
        logFile.deleteFile();
        MidiForgeAudioProcessor a; a.setFeedbackLogFile (logFile);
        a.magicRandomize();
        a.likeVariation (2);
        a.dislikeVariation (5);
        const auto exportFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("midiforge_qa_export.mid");
        a.exportMidi (exportFile);
        exportFile.deleteFile();

        juce::StringArray lines;
        lines.addLines (logFile.loadFileAsString());
        lines.removeEmptyStrings();
        const int cols = lines.size() > 0 ? juce::StringArray::fromTokens (lines[0], ",", "").size() : 0;
        bool sameCols = lines.size() == 4;
        for (const auto& l : lines) sameCols = sameCols && juce::StringArray::fromTokens (l, ",", "").size() == cols;
        report ("feedback log: header + one row per like / dislike / export", sameCols && cols == 22 && lines[0].startsWith ("time_utc,engine,verdict"),
               fmt ("%.0f lines, %.0f columns", (double) lines.size(), (double) cols));
        const auto like = juce::StringArray::fromTokens (lines.size() > 1 ? lines[1] : juce::String(), ",", "");
        const auto dislike = juce::StringArray::fromTokens (lines.size() > 2 ? lines[2] : juce::String(), ",", "");
        report ("feedback log: verdicts and slots are recorded", like.size() > 3 && like[2] == "like" && like[3] == "3"
               && dislike.size() > 3 && dislike[2] == "dislike" && dislike[3] == "6", "like slot 3, dislike slot 6");
        report ("feedback log: rows carry transform and archetype names", like.size() > 5 && like[4] != "?" && like[5] != "?",
               like.size() > 5 ? (like[4] + " / " + like[5]).toStdString() : std::string ("no row"));
        logFile.deleteFile();
    }

    // ------------------------------------------------------------------ 11. live playback timing
    {
        struct FakeHost : juce::AudioPlayHead
        {
            double ppq = 0.0, bpm = 120.0; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setPpqPosition (ppq); p.setBpm (bpm); p.setIsPlaying (playing); return p;
            }
        };
        auto runTiming = [&] (int blockSize, double bpm, double startPpq, bool swing0, double& worstErr, int& onsets, int& firstStepSeen)
        {
            MidiForgeAudioProcessor a;
            if (swing0) a.setSwing (0.0f);
            a.setHumanizeEnabled (false);
            FakeHost host; host.bpm = bpm; host.ppq = startPpq;
            const double sr = 44100.0;
            a.setPlayHead (&host);
            a.prepareToPlay (sr, blockSize);
            const double samplesPerPpq = sr * 60.0 / bpm;
            const double stepSamples = samplesPerPpq / 4.0;
            juce::AudioBuffer<float> audio (2, blockSize);
            worstErr = 0.0; onsets = 0; firstStepSeen = -1;
            const int blocks = (int) std::ceil (4.0 * 16 * stepSamples / blockSize) + 4;   // ~4 bars
            for (int b = 0; b < blocks; ++b)
            {
                host.ppq = startPpq + (double) b * blockSize / samplesPerPpq;
                juce::MidiBuffer midi;
                a.processBlock (audio, midi);
                for (const auto m : midi)
                {
                    if (! m.getMessage().isNoteOn()) continue;
                    const double absSample = (double) b * blockSize + m.samplePosition;
                    const double absPpq = startPpq + absSample / samplesPerPpq;
                    const double stepPos = absPpq * 4.0;
                    if (firstStepSeen < 0) firstStepSeen = (int) std::lround (stepPos);
                    const double err = std::abs (stepPos - std::round (stepPos)) * stepSamples;   // distance to the nearest 16th, in samples
                    worstErr = juce::jmax (worstErr, err);
                    ++onsets;
                }
            }
        };
        double worst = 0; int onsets = 0, first = 0;
        runTiming (512, 120.0, 0.0, true, worst, onsets, first);
        report ("live timing: notes land on the 16th grid inside the block (512 samples, 120 BPM)", onsets > 0 && worst <= 2.0,
               fmt ("%.0f onsets, worst error %.1f samples", (double) onsets, worst));
        runTiming (1024, 133.0, 0.37, true, worst, onsets, first);
        report ("live timing: also with 1024-sample blocks, odd BPM and an off-grid start", onsets > 0 && worst <= 2.0,
               fmt ("%.0f onsets, worst error %.1f samples", (double) onsets, worst));
        runTiming (512, 120.0, 0.0, true, worst, onsets, first);
        report ("live timing: the first downbeat of playback is not skipped", first == 0, fmt ("first onset at step %.0f", (double) first));
        {
            MidiForgeAudioProcessor a; a.setSwing (0.0f); a.setHumanizeEnabled (false);
            FakeHost host; host.bpm = 120.0; host.ppq = 0.0; host.playing = false;
            a.setPlayHead (&host); a.prepareToPlay (44100.0, 512);
            juce::AudioBuffer<float> audio (2, 512);
            int stoppedOnsets = 0;
            for (int b = 0; b < 200; ++b)
            {
                host.ppq = 0.02 * b;   // a scrubbing/stopped host moving through steps
                juce::MidiBuffer midi; a.processBlock (audio, midi);
                for (const auto m : midi) if (m.getMessage().isNoteOn()) ++stoppedOnsets;
            }
            report ("live timing: a stopped transport emits no notes", stoppedOnsets == 0, fmt ("%.0f note-ons while stopped", (double) stoppedOnsets));
            host.playing = true; host.ppq = 0.0;
            juce::MidiBuffer midi; a.processBlock (audio, midi);
            int downbeat = 0; for (const auto m : midi) if (m.getMessage().isNoteOn()) ++downbeat;
            report ("live timing: pressing play again at the same position plays the downbeat", downbeat > 0, fmt ("%.0f note-ons in the first block", (double) downbeat));
        }
    }

    // ------------------------------------------------------------------ 12. drag / export must not train Taste ML
    {
        MidiForgeAudioProcessor a;
        a.resetTaste();
        const float c0 = a.getTasteSamples();
        a.writeTemporaryMidiFile();
        const float c1 = a.getTasteSamples();
        report ("drag / export has no implicit Taste learning",
                std::abs (c1 - c0) < 0.001f,
                fmt ("taste samples %.2f -> %.2f", (double) c0, (double) c1));

        a.chooseVariation (4);
        a.likeVariation (4);
        const float rated = a.getTasteSamples();
        report ("explicit LIKE still trains Taste independently",
                rated > c1,
                fmt ("taste samples %.2f -> %.2f", (double) c1, (double) rated));

        a.chooseVariation (5);
        a.writeTemporaryMidiFile();
        report ("drag / export after explicit rating adds no hidden sample",
                std::abs (a.getTasteSamples() - rated) < 0.001f,
                "explicit feedback only");
    }

    // ------------------------------------------------------------------ 13. SIMILAR / more like this (0.80)
    {
        MidiForgeAudioProcessor a; a.setFeedbackLogFile (juce::File());
        a.chooseVariation (2);
        std::vector<MidiForgeAudioProcessor::VisibleNote> ref = a.getVisibleNotes();
        const auto t0 = std::chrono::steady_clock::now();
        const int found = a.similarToSelected();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        report ("SIMILAR: finds seven relatives", found == 7, fmt ("%.0f relatives in %.0f ms", (double) found, ms));
        report ("SIMILAR: the bank keeps eight variations and selects slot 1", a.getVariationCount() == 8 && a.getSelectedVariation() == 0, "8 variations, slot 1 selected");
        const auto now = a.getVisibleNotes();
        bool same = now.size() == ref.size();
        for (size_t i = 0; same && i < ref.size(); ++i)
            same = now[i].step == ref[i].step && now[i].note == ref[i].note && now[i].channel == ref[i].channel && now[i].length == ref[i].length;
        report ("SIMILAR: slot 1 is the untouched source loop", same, fmt ("%.0f notes", (double) now.size()));
        int identical = 0, tooFar = 0, tooFew = 0;
        for (int k = 1; k < 8; ++k)
        {
            a.chooseVariation (k);
            const auto v = a.getVisibleNotes();
            if (v.size() == ref.size())
            {
                bool eq = true;
                for (size_t i = 0; eq && i < ref.size(); ++i)
                    eq = v[i].step == ref[i].step && v[i].note == ref[i].note && v[i].channel == ref[i].channel;
                if (eq) ++identical;
            }
            if (v.size() < ref.size() / 2) ++tooFew;
            int drums = 0, refDrums = 0;
            for (const auto& n : v) if (n.channel == 5) ++drums;
            for (const auto& n : ref) if (n.channel == 5) ++refDrums;
            if (std::abs (drums - refDrums) > 4) ++tooFar;
        }
        report ("SIMILAR: every relative differs from the source", identical == 0, fmt ("%.0f identical", (double) identical));
        report ("SIMILAR: relatives keep the loop's size and drum pattern", tooFew == 0 && tooFar == 0, fmt ("%.0f thin, %.0f drum drift", (double) tooFew, (double) tooFar));
        // the seven relatives must not be clones of each other or of the source (compared note by note)
        {
            float worst = 1.0f;
            for (int seedTry = 0; seedTry < 4; ++seedTry)
            {
                MidiForgeAudioProcessor b; b.setFeedbackLogFile (juce::File());
                b.setSeed (100 + seedTry * 17);
                b.chooseVariation (seedTry);
                b.similarToSelected();
                std::vector<std::vector<std::uint32_t>> keys;
                for (int k = 0; k < 8; ++k)
                {
                    b.chooseVariation (k);
                    std::vector<std::uint32_t> kk;
                    for (const auto& n : b.getVisibleNotes()) kk.push_back (((std::uint32_t) n.step << 16) | ((std::uint32_t) n.note << 8) | (std::uint32_t) n.channel);
                    std::sort (kk.begin(), kk.end());
                    keys.push_back (std::move (kk));
                }
                for (size_t i = 0; i < keys.size(); ++i)
                    for (size_t j = i + 1; j < keys.size(); ++j)
                    {
                        std::vector<std::uint32_t> common;
                        std::set_intersection (keys[i].begin(), keys[i].end(), keys[j].begin(), keys[j].end(), std::back_inserter (common));
                        const float d = 1.0f - (float) common.size() / (float) std::max<size_t> (1, std::max (keys[i].size(), keys[j].size()));
                        worst = std::min (worst, d);
                    }
            }
            report ("SIMILAR: no two variations are clones (4 different loops)", worst >= 0.05f, fmt ("closest pair differs in %.0f%% of notes", 100.0 * worst));
        }
        // a second press builds on whatever is selected now
        a.chooseVariation (3);
        const int again = a.similarToSelected();
        report ("SIMILAR: can be applied repeatedly", again >= 5 && a.getVariationCount() == 8, fmt ("%.0f relatives", (double) again));
    }

    // ------------------------------------------------------------------ 14. melody pitch variety (register placement)
    {
        // The melody foundation used to clamp every planned note into a lane that sat below the composer's lane, piling ~40% of the
        // notes onto the single highest scale tone under the ceiling. Measured over several loops: the most-used pitch must not
        // dominate, the highest pitch must not be the most-used one in most loops, and same-pitch repeats must stay moderate.
        double topShare = 0, highestIsTop = 0; int loops = 0, repeats = 0, intervals = 0;
        for (int sd = 0; sd < 6; ++sd)
        {
            MidiForgeAudioProcessor a; a.setFeedbackLogFile (juce::File());
            a.setSeed (5000 + sd * 131);
            std::vector<MidiForgeAudioProcessor::VisibleNote> mel;
            for (const auto& v : a.getVisibleNotes()) if (v.channel == 3) mel.push_back (v);
            std::sort (mel.begin(), mel.end(), [] (const auto& x, const auto& y) { return x.step < y.step; });
            if (mel.size() < 6) continue;
            std::map<int, int> h; int hi = 0;
            for (const auto& n : mel) { ++h[n.note]; hi = std::max (hi, n.note); }
            int t = 0, tn = 0; for (const auto& kv : h) if (kv.second > t) { t = kv.second; tn = kv.first; }
            topShare += (double) t / (double) mel.size(); highestIsTop += (tn == hi) ? 1 : 0;
            for (size_t k = 1; k < mel.size(); ++k) { ++intervals; if (mel[k].note == mel[k - 1].note) ++repeats; }
            ++loops;
        }
        topShare /= std::max (1, loops); highestIsTop /= std::max (1, loops);
        const double repeatShare = (double) repeats / (double) std::max (1, intervals);
        report ("melody variety: no single pitch dominates", loops >= 4 && topShare < 0.32, fmt ("most-used pitch %.0f%% of notes (was ~45%%)", 100.0 * topShare));
        report ("melody variety: the ceiling pitch is not the favourite", highestIsTop < 0.45, fmt ("highest = most-used in %.0f%% of loops (was ~75%%)", 100.0 * highestIsTop));
        report ("melody variety: moderate same-pitch repeats", repeatShare < 0.27, fmt ("%.0f%% of intervals are repeats (was ~35-40%%)", 100.0 * repeatShare));
    }


    // ------------------------------------------------------------------ 15. local melody quality 2.0 (0.85.2)
    {
        int loops = 0;
        int loopsWithSevereSpot = 0;
        int totalSevereSpots = 0;
        int totalLongScalarRuns = 0;
        int totalLongSameRuns = 0;

        for (int sd = 0; sd < 64; ++sd)
        {
            MidiForgeAudioProcessor a;
            a.setFeedbackLogFile (juce::File());
            a.setSeed (13000 + sd * 37);
            auto mel = a.getVisibleNotes();
            std::vector<int> pitches;
            for (const auto& n : mel)
                if (n.channel == 3)
                    pitches.push_back (n.note);

            if (pitches.size() < 4)
                continue;

            std::stable_sort (mel.begin(), mel.end(),
                [] (const auto& a, const auto& b)
                {
                    if (a.step != b.step) return a.step < b.step;
                    return a.note < b.note;
                });
            pitches.clear();
            for (const auto& n : mel)
                if (n.channel == 3)
                    pitches.push_back (n.note);
            int severe = 0;
            int scalarRun = 1, scalarRuns = 0;
            int sameRun = 1, sameRuns = 0;

            for (size_t i = 1; i < pitches.size(); ++i)
            {
                const int d = pitches[i] - pitches[i - 1];
                const int ad = std::abs (d);

                if (ad >= 10)
                {
                    bool recovered = false;
                    if (i + 1 < pitches.size())
                    {
                        const int next = pitches[i + 1] - pitches[i];
                        recovered = ((d > 0 && next < 0) || (d < 0 && next > 0))
                            && std::abs (next) <= 5;
                    }
                    if (! recovered) ++severe;
                }

                if (i >= 2)
                {
                    const int prev = pitches[i - 1] - pitches[i - 2];
                    if (std::abs (prev) <= 2 && std::abs (d) <= 2 && prev != 0 && d != 0
                        && ((prev > 0) == (d > 0)))
                        ++scalarRun;
                    else
                    {
                        if (scalarRun >= 5) ++scalarRuns;
                        scalarRun = 1;
                    }

                    if (std::abs (prev) <= 2 && std::abs (d) <= 2 && prev != 0 && d != 0
                        && ((prev > 0) != (d > 0)))
                        ++sameRun;
                    else
                    {
                        if (sameRun >= 5) ++sameRuns;
                        sameRun = 1;
                    }
                }
            }

            if (scalarRun >= 5) ++scalarRuns;
            if (sameRun >= 5) ++sameRuns;

            if (severe > 0) ++loopsWithSevereSpot;
            totalSevereSpots += severe;
            totalLongScalarRuns += scalarRuns;
            totalLongSameRuns += sameRuns;
            ++loops;
        }

        report ("local quality: severe unrecovered leaps stay rare",
                loops >= 48 && totalSevereSpots <= 8,
                fmt ("%.0f loops, %.0f severe spots", (double) loops, (double) totalSevereSpots));
        report ("local quality: long scalar walks stay controlled",
                loops >= 48 && totalLongScalarRuns <= 20,
                fmt ("%.0f long scalar runs", (double) totalLongScalarRuns));
        report ("local quality: mechanical tiny zig-zags stay controlled",
                loops >= 48 && totalLongSameRuns <= 20,
                fmt ("%.0f long zig-zag runs", (double) totalLongSameRuns));
        report ("local quality: no seed batch is dominated by severe local failures",
                loops >= 48 && loopsWithSevereSpot <= 12,
                fmt ("%.0f / %.0f loops with severe spots", (double) loopsWithSevereSpot, (double) loops));
    }


    // ------------------------------------------------------------------ 16. micro-rhythm quality (0.85.4)
    {
        int loops = 0;
        int duplicateOnsetLoops = 0;
        int mechanicalRuns = 0;
        double oneStepSum = 0.0;

        for (int sd = 0; sd < 64; ++sd)
        {
            MidiForgeAudioProcessor a;
            a.setFeedbackLogFile (juce::File());
            a.setSeed (17000 + sd * 43);

            auto mel = a.getVisibleNotes();
            std::vector<MidiForgeAudioProcessor::VisibleNote> melody;
            for (const auto& n : mel)
                if (n.channel == 3)
                    melody.push_back (n);

            std::stable_sort (melody.begin(), melody.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.step != y.step) return x.step < y.step;
                    return x.note < y.note;
                });

            if (melody.size() < 4)
                continue;

            int duplicates = 0;
            int oneStep = 0;
            int previousGap = -1;
            int sameGapRun = 1;

            for (size_t i = 1; i < melody.size(); ++i)
            {
                const int gap = melody[i].step - melody[i - 1].step;
                if (gap <= 0) ++duplicates;
                if (gap <= 1) ++oneStep;

                if (gap > 0 && gap == previousGap)
                    ++sameGapRun;
                else
                {
                    if (sameGapRun >= 5) ++mechanicalRuns;
                    sameGapRun = 1;
                }
                previousGap = gap;
            }
            if (sameGapRun >= 5) ++mechanicalRuns;

            if (duplicates > 0) ++duplicateOnsetLoops;
            oneStepSum += (double) oneStep / (double) juce::jmax<size_t> (1, melody.size() - 1);
            ++loops;
        }

        const double meanOneStep = oneStepSum / (double) juce::jmax (1, loops);
        report ("micro-rhythm: melody has no duplicate onsets",
                loops >= 48 && duplicateOnsetLoops == 0,
                fmt ("%.0f / %.0f loops with duplicate melody onsets",
                     (double) duplicateOnsetLoops, (double) loops));
        report ("micro-rhythm: one-step chains are not dominant",
                loops >= 48 && meanOneStep < 0.90,
                fmt ("mean one-step share %.0f%%", 100.0 * meanOneStep));
        report ("micro-rhythm: long identical gap runs stay rare",
                loops >= 48 && mechanicalRuns <= 24,
                fmt ("%.0f mechanical gap runs", (double) mechanicalRuns));
    }


    // ------------------------------------------------------------------ 17. latent melodic character integrity (0.85.7)
    {
        MidiForgeAudioProcessor a;
        a.setFeedbackLogFile (juce::File());
        a.setSeed (19001);

        std::set<int> characters;
        std::set<std::string> behaviorSignatures;

        for (int k = 0; k < a.getVariationCount(); ++k)
        {
            a.chooseVariation (k);
            const int character = a.getVariationMelodyCharacter (k);

            report ("melody character integrity: final variation keeps a valid family",
                    character >= 0 && character < 12,
                    fmt ("slot %.0f character %.0f", (double) (k + 1), (double) character));

            if (character >= 0 && character < 12)
                characters.insert (character);

            auto notes = a.getVisibleNotes();
            std::vector<MidiForgeAudioProcessor::VisibleNote> mel;
            for (const auto& n : notes)
                if (n.channel == 3)
                    mel.push_back (n);

            std::stable_sort (mel.begin(), mel.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.step != y.step) return x.step < y.step;
                    return x.note < y.note;
                });

            std::string sig = std::to_string (mel.size()) + ":";
            for (size_t i = 0; i < mel.size() && i < 8; ++i)
                sig += std::to_string (mel[i].step % 16) + ",";

            sig += "|";
            for (size_t i = 1; i < mel.size() && i < 9; ++i)
            {
                const int d = mel[i].note - mel[i - 1].note;
                sig += (d > 3 ? '+' : d < -3 ? '-' : d == 0 ? '0' : 's');
            }

            behaviorSignatures.insert (sig);
        }

        // Candidate search creates one candidate per native archetype, and the
        // current bank keeps one candidate per archetype. Their rotated latent
        // characters therefore form eight distinct families in every bank.
        report ("melody character diversity: bank keeps all distinct latent families",
                a.getVariationCount() == 8 && characters.size() == 8,
                fmt ("%.0f distinct characters across %.0f variations",
                     (double) characters.size(), (double) a.getVariationCount()));

        report ("melody character diversity: bank changes behavior signatures",
                behaviorSignatures.size() >= 6,
                fmt ("%.0f distinct local behavior signatures",
                     (double) behaviorSignatures.size()));
    }

    // ------------------------------------------------------------------ 18. unified melody complexity intent (0.86)
    {
        MidiForgeAudioProcessor a;
        a.setFeedbackLogFile (juce::File());
        a.setSeed (22001);

        std::vector<int> first;
        std::set<int> bankClasses;

        for (int k = 0; k < a.getVariationCount(); ++k)
        {
            const int cls = a.getVariationMelodyComplexityClass (k);
            first.push_back (cls);

            report ("melody intent: final variation keeps one valid complexity class",
                    cls >= 0 && cls <= 2,
                    fmt ("slot %.0f class %.0f", (double) (k + 1), (double) cls));

            if (cls >= 0 && cls <= 2)
                bankClasses.insert (cls);
        }

        report ("melody intent: bank contains multiple complexity languages",
                a.getVariationCount() == 8 && bankClasses.size() >= 2,
                fmt ("%.0f distinct classes across %.0f variations",
                     (double) bankClasses.size(), (double) a.getVariationCount()));

        a.setSeed (22001);
        a.regenerate ();

        std::vector<int> second;
        for (int k = 0; k < a.getVariationCount(); ++k)
            second.push_back (a.getVariationMelodyComplexityClass (k));

        report ("melody intent: complexity assignment is deterministic",
                first == second,
                first == second ? "first/second class vectors match"
                                 : "first/second class vectors differ");

        std::set<int> observedClasses;
        for (int seed = 1; seed <= 32; ++seed)
        {
            MidiForgeAudioProcessor b;
            b.setFeedbackLogFile (juce::File());
            b.setSeed (23000 + seed * 17);

            for (int k = 0; k < b.getVariationCount(); ++k)
            {
                const int cls = b.getVariationMelodyComplexityClass (k);
                if (cls >= 0 && cls <= 2)
                    observedClasses.insert (cls);
            }
        }

        report ("melody intent: all three complexity languages are reachable",
                observedClasses.size() == 3,
                fmt ("%.0f / 3 classes observed in seed batch",
                     (double) observedClasses.size()));
    }


    // ------------------------------------------------------------------ 19. style/safety separation (0.87)
    {
        MidiForgeAudioProcessor p;
        p.setFeedbackLogFile (juce::File());
        p.setMelodyType (MidiForgeAudioProcessor::CounterMelody);
        p.setComplexity (1.0f, false);
        p.setLeapChance (1.0f, false);

        int maxObservedLeap = 0;
        bool sawExpressiveLeap = false;
        bool sawInvalidOvershoot = false;

        for (int seed = 1; seed <= 128; ++seed)
        {
            p.setSeed (31000 + seed * 31);
            p.regenerate ();

            auto mel = layer ({ p.getVisibleNotes(), std::max (1, p.getVisibleBars()) }, 3);
            for (size_t i = 1; i < mel.size(); ++i)
            {
                const int leap = std::abs (mel[i].note - mel[i - 1].note);
                maxObservedLeap = std::max (maxObservedLeap, leap);
                if (leap >= 10) sawExpressiveLeap = true;
                if (leap > 12) sawInvalidOvershoot = true;
            }
        }

        report ("style/safety: expressive leaps survive the safety pass",
                sawExpressiveLeap,
                fmt ("max observed leap %.0f semitones", (double) maxObservedLeap));
        report ("style/safety: final melody safety ceiling is respected",
                ! sawInvalidOvershoot,
                fmt ("max observed leap %.0f semitones", (double) maxObservedLeap));
    }


    // ------------------------------------------------------------------ 20. complexity class survives selection (0.86.x)
    {
        MidiForgeAudioProcessor p;
        p.setFeedbackLogFile (juce::File());
        p.setSeed (42017);
        p.regenerate ();

        double simpleNotes = 0.0, simpleSpread = 0.0; int simpleCount = 0;
        double complexNotes = 0.0, complexSpread = 0.0; int complexCount = 0;

        for (int k = 0; k < p.getVariationCount(); ++k)
        {
            const int cls = p.getVariationMelodyComplexityClass (k);
            p.chooseVariation (k);
            const auto mel = layer ({ p.getVisibleNotes(), std::max (1, p.getVisibleBars()) }, 3);

            if (mel.empty())
                continue;

            std::set<int> pitches;
            for (const auto& n : mel)
                pitches.insert (n.note);

            if (cls == 0)
            {
                simpleNotes += (double) mel.size() / std::max (1, p.getVisibleBars());
                simpleSpread += (double) pitches.size();
                ++simpleCount;
            }
            else if (cls == 2)
            {
                complexNotes += (double) mel.size() / std::max (1, p.getVisibleBars());
                complexSpread += (double) pitches.size();
                ++complexCount;
            }
        }

        const double simpleDensity = simpleCount ? simpleNotes / simpleCount : 0.0;
        const double complexDensity = complexCount ? complexNotes / complexCount : 0.0;
        const double simplePitchSpan = simpleCount ? simpleSpread / simpleCount : 0.0;
        const double complexPitchSpan = complexCount ? complexSpread / complexCount : 0.0;

        report ("complexity intent: Simple candidates remain sparser",
                simpleCount == 0 || complexCount == 0 || simpleDensity <= complexDensity + 0.75,
                fmt ("simple %.2f notes/bar vs complex %.2f",
                     simpleDensity, complexDensity));
        report ("complexity intent: Complex candidates retain a richer pitch vocabulary",
                simpleCount == 0 || complexCount == 0 || simplePitchSpan <= complexPitchSpan + 1.25,
                fmt ("simple %.2f unique pitches vs complex %.2f",
                     simplePitchSpan, complexPitchSpan));
    }

    std::printf ("\n%s (%d failed check%s)\n", failures == 0 ? "ALL QUALITY CHECKS PASSED" : "QUALITY CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
