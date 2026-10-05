
            phraseShape = 0.30f * peakFit
                        + 0.24f * releaseFit
                        + 0.22f * repeatWithChange
                        + 0.24f * juce::jlimit (0.0f, 1.0f, (float) usefulLeaps / (float) juce::jmax (1, intervals) * 2.7f);
        }
    }

    return juce::jlimit (
        0.0f, 1.0f,
        0.23f * intervalVariety
        + 0.20f * leapFit
        + 0.16f * turnFit
        + 0.18f * phraseShape
        + 0.13f * velocityShape
        + 0.10f * juce::jlimit (0.0f, 1.0f, (float) intervals / 12.0f));
}

void MidiForgeAudioProcessor::applyMelodicProsody (Section& section, uint32_t identity) const
{
    // 0.71 Melodic Prosody:
    // Every melody note gets a musical function before Harmony resolves its
    // exact pitch. This pass does not invent a second melody; it gives existing
    // notes intentional jobs: anchor, pickup, approach, connective motion,
    // accent, peak and release.
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    std::vector<size_t> melody;
    melody.reserve (section.notes.size());
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    if (melody.size() < 2)
        return;

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            if (section.notes[a].step != section.notes[b].step)
                return section.notes[a].step < section.notes[b].step;
            return section.notes[a].note < section.notes[b].note;
        });

    const auto composerPlan = section.hasMelodyIntent
        ? section.melodyIntent.grammar
        : midiforge::ComposerGrammar::makePlan (
            section.bars, energy, complexity, melodyType, mood,
            hash32 (identity ^ 0xC0719F0u));

    for (size_t i = 0; i < melody.size(); ++i)
    {
        auto& n = section.notes[melody[i]];
        const int phrase = juce::jlimit (0, (int) composerPlan.phrases.size() - 1, n.step / 64);
        const auto composerState = composerPlan.stateFor (phrase);

        const int nextStep = (i + 1 < melody.size()) ? section.notes[melody[i + 1]].step : n.step;
        const int nextLocalStep = (i + 1 < melody.size()) ? (nextStep % 16) : -1;
        const int localStep = n.step % 16;
        const int nextInterval = (i + 1 < melody.size())
            ? section.notes[melody[i + 1]].note - n.note
            : 0;
        const bool finalOfPhrase = (i + 1 == melody.size())
            || (nextStep / 64 != n.step / 64);

        const auto intent = midiforge::MelodicProsody::classify (
            (int) i,
            (int) melody.size(),
            localStep,
            nextLocalStep,
            std::abs (nextInterval),
            finalOfPhrase,
            composerState.role,
            composerState.tension,
            hash32 (identity ^ 0x71A11CEu));

        // Prosody is intentionally pitch-neutral. The melodic author and
        // phrase-development stages own contour and interval decisions; prosody
        // only gives existing notes performance/function roles.
        switch (intent.role)
        {
            case midiforge::MelodicProsody::Anchor:
                n.velocity = juce::jlimit (35, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmin (6, n.length + 1);
                break;

            case midiforge::MelodicProsody::Accent:
                n.velocity = juce::jlimit (35, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                break;

            case midiforge::MelodicProsody::Pickup:
                n.velocity = juce::jlimit (35, 118,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmax (1, n.length - 1);
                break;

            case midiforge::MelodicProsody::Approach:
                n.length = juce::jmax (1, n.length);
                break;

            case midiforge::MelodicProsody::Connect:
                break;

            case midiforge::MelodicProsody::Peak:
                n.velocity = juce::jlimit (40, 122,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmax (1, n.length - 1);
                break;

            case midiforge::MelodicProsody::Release:
                n.velocity = juce::jlimit (35, 118,
                    n.velocity + juce::roundToInt (intent.velocityBias * 92.0f));
                n.length = juce::jmin (6, n.length + 1);
                break;
        }

        if (intent.sustainBias > 0.04f && (intent.role == midiforge::MelodicProsody::Anchor
                                         || intent.role == midiforge::MelodicProsody::Release))
            n.length = juce::jmin (6, n.length + 1);
    }

    cleanMelodyLine (section.notes);
    removeDuplicateNotes (section.notes);
}

float MidiForgeAudioProcessor::melodicProsodyScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 4)
        return 0.52f;

    std::vector<const NoteEvent*> melody;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            melody.push_back (&n);

    std::stable_sort (melody.begin(), melody.end(),
        [] (const NoteEvent* a, const NoteEvent* b)
        {
            if (a->step != b->step) return a->step < b->step;
            return a->note < b->note;
        });

    if (melody.size() < 3)
        return 0.45f;

    const auto composerPlan = section.hasMelodyIntent
        ? section.melodyIntent.grammar
        : midiforge::ComposerGrammar::makePlan (
            section.bars, energy, complexity, melodyType, mood,
            hash32 (generationSeed ^ 0xC0719F0u));

    int approachGood = 0, approachCount = 0;
    int releaseGood = 0, releaseCount = 0;
    int peakGood = 0, peakCount = 0;
    int anchorGood = 0, anchorCount = 0;
    int accentGood = 0, accentCount = 0;

    for (size_t i = 0; i < melody.size(); ++i)
    {
        const auto* cur = melody[i];
        const int phrase = juce::jlimit (0, (int) composerPlan.phrases.size() - 1, cur->step / 64);
        const auto state = composerPlan.stateFor (phrase);
        const int nextStep = i + 1 < melody.size() ? melody[i + 1]->step : cur->step;
        const int nextInterval = i + 1 < melody.size()
            ? melody[i + 1]->note - cur->note : 0;
        const bool finalOfPhrase = i + 1 == melody.size()
            || nextStep / 64 != cur->step / 64;

        const auto intent = midiforge::MelodicProsody::classify (
            (int) i, (int) melody.size(), cur->step % 16,
            i + 1 < melody.size() ? nextStep % 16 : -1,
            std::abs (nextInterval),
            finalOfPhrase,
            state.role,
            state.tension,
            hash32 (generationSeed ^ 0x71A11CEu));

        const int local = cur->step % 16;
        switch (intent.role)
        {
            case midiforge::MelodicProsody::Approach:
                ++approachCount;
                if (i + 1 < melody.size())
                {
                    const int after = melody[i + 1]->note - cur->note;
                    if (std::abs (after) <= 7)
                        ++approachGood;
                }
                break;

            case midiforge::MelodicProsody::Release:
                ++releaseCount;
                if ((i == 0 || melody[i - 1]->note >= cur->note)
                    && cur->length >= 2)
                    ++releaseGood;
                break;

            case midiforge::MelodicProsody::Peak:
            {
                ++peakCount;
                int phraseMax = cur->note;
                const int phraseStart = (cur->step / 64) * 64;
                for (const auto* n : melody)
                    if (n->step >= phraseStart && n->step < phraseStart + 64)
                        phraseMax = std::max (phraseMax, n->note);
                if (cur->note >= phraseMax - 1)
                    ++peakGood;
                break;
            }

            case midiforge::MelodicProsody::Anchor:
                ++anchorCount;
                if (cur->velocity >= 76)
                    ++anchorGood;
                break;

            case midiforge::MelodicProsody::Accent:
                ++accentCount;
                if (cur->velocity >= 72)
                    ++accentGood;
                break;

            default:
                break;
        }

        juce::ignoreUnused (local);
    }

    const float approachFit = approachCount > 0
        ? (float) approachGood / (float) approachCount : 0.58f;
    const float releaseFit = releaseCount > 0
        ? (float) releaseGood / (float) releaseCount : 0.58f;
    const float peakFit = peakCount > 0
        ? (float) peakGood / (float) peakCount : 0.56f;
    const float anchorFit = anchorCount > 0
        ? (float) anchorGood / (float) anchorCount : 0.56f;
    const float accentFit = accentCount > 0
        ? (float) accentGood / (float) accentCount : 0.56f;

    return juce::jlimit (0.0f, 1.0f,
        0.30f * approachFit
        + 0.22f * releaseFit
        + 0.22f * peakFit
        + 0.14f * anchorFit
        + 0.12f * accentFit);
}

void MidiForgeAudioProcessor::applyHarmonicIntelligence (Section& section, uint32_t identity) const
{
    // 0.68 Harmonic Intelligence 2.0:
    // Harmony provides destinations and voice-leading gravity, but it does not
    // flatten the melody into an arpeggio. Strong/long notes prefer the active
    // chord, weak notes may remain as passing/color tones, and late-bar notes
    // can anticipate the next chord.
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || ! melodyEnabled)
        return;

    const auto prog = progressionDegrees();
    if (prog.empty())
        return;

    auto pitchClass = [] (int n) { return (n % 12 + 12) % 12; };

    auto chordPcsForBar = [&] (int bar)
    {
        std::array<int, 12> pcs {};
        int count = 0;

        for (const auto& n : section.notes)
        {
            if (n.channel != 1 || n.step / 16 != bar)
                continue;

            const int pc = pitchClass (n.note);
            if (pcs[(size_t) pc] == 0)
            {
                pcs[(size_t) pc] = 1;
                ++count;
            }
        }

        if (count == 0)
        {
            const int degree = prog[(size_t) (bar % (int) prog.size())];
            for (int offset : { 0, 2, 4 })
            {
                const int pc = pitchClass (degreeToPitch (degree + offset, octave));
                if (pcs[(size_t) pc] == 0)
                {
                    pcs[(size_t) pc] = 1;
                    ++count;
                }
            }
        }

        return pcs;
    };

    auto nearestChordPitch = [&] (int bar, int pitch, bool preferStable) -> int
    {
        const auto pcs = chordPcsForBar (bar);
        const int degree = prog[(size_t) (bar % (int) prog.size())];
        const int rootPcForBar = pitchClass (degreeToPitch (degree, octave));
        const int thirdPcForBar = pitchClass (degreeToPitch (degree + 2, octave));
        const int fifthPcForBar = pitchClass (degreeToPitch (degree + 4, octave));

        int best = pitch;
        float bestScore = 1.0e9f;

        for (int oct = -3; oct <= 3; ++oct)
        {
            const int baseOct = ((pitch / 12) * 12) + (oct * 12);
            for (int pc = 0; pc < 12; ++pc)
            {
                if (pcs[(size_t) pc] == 0)
                    continue;

                const int candidate = baseOct + pc;
                if (candidate < 34 || candidate > 108)
                    continue;

                float score = (float) std::abs (candidate - pitch);

                // On a bar anchor, roots are useful. Everywhere else, thirds and
                // fifths are slightly preferred so the melody does not become
                // a root-note machine.
                if (preferStable)
                {
                    if (pc == rootPcForBar) score -= 0.85f;
                    if (pc == thirdPcForBar) score -= 1.15f;
                    if (pc == fifthPcForBar) score -= 0.45f;
                }
                else
                {
                    if (pc == thirdPcForBar) score -= 1.55f;
                    if (pc == fifthPcForBar) score -= 0.80f;
                    if (pc == rootPcForBar) score += 0.80f;
                }

                if (score < bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
        }

        return best;
    };

    auto isChordTone = [&] (int bar, int pitch)
    {
        const auto pcs = chordPcsForBar (bar);
        return pcs[(size_t) pitchClass (pitch)] != 0;
    };

    std::vector<size_t> melody;
    for (size_t i = 0; i < section.notes.size(); ++i)
        if (section.notes[i].channel == 3)
            melody.push_back (i);

    std::stable_sort (melody.begin(), melody.end(),
        [&] (size_t a, size_t b)
        {
            return section.notes[a].step < section.notes[b].step;
        });

    if (melody.size() < 2)
        return;

    for (size_t k = 0; k < melody.size(); ++k)
    {
        auto& n = section.notes[melody[k]];
        const int bar = juce::jlimit (0, juce::jmax (0, section.bars - 1), n.step / 16);
        const int local = n.step % 16;
        const bool strong = (local % 4) == 0;
        const bool longNote = n.length >= 3;
        const bool late = local >= 12;
        const bool firstOfBar = (k == 0 || section.notes[melody[k - 1]].step / 16 != bar);

        const bool chordTone = isChordTone (bar, n.note);

        // Harmony is a correction layer, not a second melody author. Only a
        // genuinely non-chord note on a structural location receives pitch
        // gravity, and the destination is limited to a small local correction.
        float targetWeight = 0.0f;
        if (! chordTone)
        {
            if (firstOfBar && strong)      targetWeight = 0.34f;
            else if (strong)               targetWeight = 0.28f;
            else if (longNote)             targetWeight = 0.20f;
            else if (late)                 targetWeight = 0.18f;
            else                           targetWeight = 0.0f;
        }

        // Keep some harmonic tension in the B bar and on deliberately weak events.
        if ((bar & 3) == 2 && !firstOfBar)
            targetWeight *= 0.72f;
        if (complexity > 0.70f && !strong)
            targetWeight *= 0.72f;

        int target = n.note;
        if (! chordTone && targetWeight > 0.0f)
            target = nearestChordPitch (bar, n.note, firstOfBar || strong);

        // Late notes may point toward the next harmony, but only through the same
        // small local correction budget.
        if (! chordTone && late && bar + 1 < section.bars)
        {
            const int nextTarget = nearestChordPitch (bar + 1, n.note, false);
            const float anticipation = ((bar & 3) == 3) ? 0.70f : 0.50f;
            const int blend = juce::roundToInt (
                (1.0f - anticipation) * (float) target
                + anticipation * (float) nextTarget);
            target = juce::jlimit (34, 108, blend);
            targetWeight = juce::jmax (targetWeight, 0.18f);
        }

        // At a phrase seam, prioritise a smooth destination, still only when the
        // current note is genuinely outside the active chord.
        if (! chordTone && firstOfBar && k > 0)
        {
            const int previous = section.notes[melody[k - 1]].note;
            const int voiceTarget = nearestChordPitch (bar, previous, true);
            target = juce::roundToInt (
                0.48f * (float) target
                + 0.52f * (float) voiceTarget);
            targetWeight = juce::jmax (targetWeight, 0.26f);
        }

        if (! chordTone && targetWeight > 0.0f)
        {
            const int correction = juce::jlimit (-4, 4, target - n.note);
            const int limitedTarget = n.note + correction;
            const int blended = juce::roundToInt (
                (1.0f - targetWeight) * (float) n.note
                + targetWeight * (float) limitedTarget);
            n.note = foldIntoLane (snapToScale (blended), 34, 108);
        }

        // A nearby double non-chord passage can receive a tiny directed nudge,
        // but never a full jump to the chord tone.
        if (k + 1 < melody.size())
        {
            auto& next = section.notes[melody[k + 1]];
            const int nextBar = next.step / 16;
            if (nextBar == bar && next.step - n.step <= 3
                && !isChordTone (bar, n.note) && !isChordTone (bar, next.note))
            {
                const int resolution = nearestChordPitch (bar, next.note, false);
                const int correction = juce::jlimit (-3, 3, resolution - next.note);
                const int limitedTarget = next.note + correction;
                next.note = foldIntoLane (
                    snapToScale (juce::roundToInt (
                        0.60f * (float) next.note + 0.40f * (float) limitedTarget)),
                    34, 108);
            }
        }

        // Slight dynamic separation: harmonic anchors speak more clearly than
        // passing tones, while anticipation stays audible but does not dominate.
        if (strong)
            n.velocity = juce::jlimit (42, 118, n.velocity + 3);
        else if (late && bar + 1 < section.bars)
            n.velocity = juce::jlimit (42, 118, n.velocity + 1);
        else if (!isChordTone (bar, n.note))
            n.velocity = juce::jlimit (42, 118, n.velocity - 1);
    }

    removeDuplicateNotes (section.notes);
    cleanMelodyLine (section.notes);
}

float MidiForgeAudioProcessor::harmonicIntelligenceScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine)
        return 1.0f;

    const auto prog = progressionDegrees();
    if (prog.empty())
        return 0.55f;

    auto pc = [] (int n) { return (n % 12 + 12) % 12; };
    auto isChordToneAt = [&] (int bar, int pitch)
    {
        bool found = false;
        for (const auto& n : section.notes)
        {
            if (n.channel == 1 && n.step / 16 == bar && pc (n.note) == pc (pitch))
            {
                found = true;
                break;
            }
        }
        if (found) return true;

        const int degree = prog[(size_t) (bar % (int) prog.size())];
        for (int offset : { 0, 2, 4 })
            if (pc (degreeToPitch (degree + offset, octave)) == pc (pitch))
                return true;
        return false;
    };

    std::vector<const NoteEvent*> mel;
    for (const auto& n : section.notes)
        if (n.channel == 3)
            mel.push_back (&n);
    std::stable_sort (mel.begin(), mel.end(),
        [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });

    if (mel.size() < 2)
        return 0.45f;

    int anchors = 0, anchorHits = 0, anticipations = 0, anticipationHits = 0;
    int resolutions = 0, resolved = 0, transitions = 0, smooth = 0;
    int chordToneCount = 0;

    for (size_t i = 0; i < mel.size(); ++i)
    {
        const auto* n = mel[i];
        const int bar = n->step / 16;
        const int local = n->step % 16;
        const bool anchor = (local % 4) == 0 || n->length >= 3;
        const bool late = local >= 12 && bar + 1 < section.bars;

        if (anchor)
        {
            ++anchors;
            if (isChordToneAt (bar, n->note)) ++anchorHits;
        }
        if (isChordToneAt (bar, n->note))
            ++chordToneCount;

        if (late)
        {
            ++anticipations;
            bool pointsForward = false;
            if (i + 1 < mel.size())
            {
                const int nextBar = mel[i + 1]->step / 16;
                if (nextBar > bar || mel[i + 1]->step - n->step >= 1)
                    pointsForward = isChordToneAt (bar + 1, mel[i + 1]->note);
            }
            if (pointsForward) ++anticipationHits;
        }

        if (i + 1 < mel.size())
        {
            const auto* next = mel[i + 1];
            if (next->step - n->step <= 4 && !isChordToneAt (bar, n->note))
            {
                ++resolutions;
                if (isChordToneAt (next->step / 16, next->note))
                    ++resolved;
            }

            if (next->step / 16 != bar)
            {
                ++transitions;
                if (std::abs (next->note - n->note) <= 7)
                    ++smooth;
            }
        }
    }

    const float anchorFit = anchors > 0 ? (float) anchorHits / (float) anchors : 0.55f;
    const float anticipationFit = anticipations > 0
        ? (float) anticipationHits / (float) anticipations : 0.55f;
    const float resolutionFit = resolutions > 0
        ? (float) resolved / (float) resolutions : 0.55f;
    const float voiceFit = transitions > 0
        ? (float) smooth / (float) transitions : 0.60f;

    const float chordRatio = (float) chordToneCount / (float) mel.size();
    // Reward a useful harmonic backbone, but penalize an arpeggio-like wall of
    // chord tones. Expressive melodies usually mix anchors with color tones.
    const float distributionFit = 1.0f
        - juce::jlimit (0.0f, 1.0f, std::abs (chordRatio - 0.58f) / 0.38f);

    std::array<int, 12> used {};
    int uniqueChordTones = 0;
    for (const auto* n : mel)
    {
        const int bar = n->step / 16;
        if (!isChordToneAt (bar, n->note))
            continue;
        const int key = pc (n->note);
        if (used[(size_t) key] == 0)
        {
            used[(size_t) key] = 1;
            ++uniqueChordTones;
        }
    }
    const float diversityFit = juce::jlimit (0.0f, 1.0f, (float) uniqueChordTones / 3.0f);

    return juce::jlimit (
        0.0f, 1.0f,
        0.22f * anchorFit
        + 0.17f * resolutionFit
        + 0.17f * anticipationFit
        + 0.18f * voiceFit
        + 0.16f * distributionFit
        + 0.10f * diversityFit);
}



float MidiForgeAudioProcessor::phraseMemory4Score (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 8)
        return 0.52f;

    auto collectBar = [&] (int bar)
    {
        std::vector<const NoteEvent*> out;
        for (const auto& n : section.notes)
            if (n.channel == 3 && n.step / 16 == bar)
                out.push_back (&n);

        std::stable_sort (out.begin(), out.end(),
            [] (const NoteEvent* a, const NoteEvent* b) { return a->step < b->step; });
        return out;
    };

    std::array<std::vector<const NoteEvent*>, 4> reference;
    for (int b = 0; b < 4; ++b)
        reference[(size_t) b] = collectBar (b);

    for (const auto& v : reference)
        if (v.size() < 2)
            return 0.45f;

    auto contourFit = [] (const std::vector<const NoteEvent*>& a,
                          const std::vector<const NoteEvent*>& b,
                          bool inverted)
    {
        if (a.size() < 2 || b.size() < 2)
            return 0.0f;

        const size_t n = juce::jmin (a.size(), b.size());
        int hits = 0;
        for (size_t i = 1; i < n; ++i)
        {
            const int da = a[i]->note - a[i - 1]->note;
            const int db = b[i]->note - b[i - 1]->note;
            const bool same = inverted ? ((da > 0 && db < 0)
                                       || (da < 0 && db > 0)
                                       || (da == 0 && db == 0))
                                       : ((da > 0 && db > 0)
                                       || (da < 0 && db < 0)
                                       || (da == 0 && db == 0));
            if (same) ++hits;
        }
        return (float) hits / (float) juce::jmax<size_t> (1, n - 1);
    };

    auto rhythmFit = [] (const std::vector<const NoteEvent*>& a,
                         const std::vector<const NoteEvent*>& b)
    {
        if (a.empty() || b.empty()) return 0.0f;
        const size_t n = juce::jmin (a.size(), b.size());
        int hits = 0;
        for (size_t i = 0; i < n; ++i)
            if (std::abs ((a[i]->step % 16) - (b[i]->step % 16)) <= 1)
                ++hits;
        return (float) hits / (float) juce::jmax (a.size(), b.size());
    };

    float scoreSum = 0.0f;
    int count = 0;
    for (int phrase = 1; phrase < section.bars / 4; ++phrase)
    {
        float phraseShape = 0.0f;
        float phraseRhythm = 0.0f;
        int barsCompared = 0;

        for (int localBar = 0; localBar < 4; ++localBar)
        {
            const auto cur = collectBar (phrase * 4 + localBar);
            if (cur.size() < 2)
                continue;

            const float direct = contourFit (reference[(size_t) localBar], cur, false);
            const float inverse = contourFit (reference[(size_t) localBar], cur, true);
            const auto composerPlan = section.hasMelodyIntent
                ? section.melodyIntent.grammar
                : midiforge::ComposerGrammar::makePlan (
                    section.bars, energy, complexity, melodyType, mood,
                    hash32 (generationSeed ^ 0xC0A70970u));
            const auto composerState = composerPlan.stateFor (phrase);
            const bool expectedInverse =
                composerState.role == midiforge::ComposerGrammar::Contrast
                || composerState.role == midiforge::ComposerGrammar::Peak;

            phraseShape += expectedInverse ? juce::jmax (inverse, direct * 0.68f)
                                           : juce::jmax (direct, inverse * 0.72f);
            phraseRhythm += rhythmFit (reference[(size_t) localBar], cur);
            ++barsCompared;
        }

        if (barsCompared > 0)
        {
            phraseShape /= (float) barsCompared;
            phraseRhythm /= (float) barsCompared;

            float variation = 0.52f;
            if (phrase >= 1)
            {
                int literal = 0;
                for (int localBar = 0; localBar < 4; ++localBar)
                {
                    const auto cur = collectBar (phrase * 4 + localBar);
                    if (cur.size() == reference[(size_t) localBar].size()
                        && rhythmFit (reference[(size_t) localBar], cur) > 0.92f
                        && contourFit (reference[(size_t) localBar], cur, false) > 0.95f)
                        ++literal;
                }
                variation = 1.0f - juce::jlimit (0.0f, 1.0f, (float) literal / 4.0f);
            }

            scoreSum += 0.52f * phraseShape
                      + 0.28f * phraseRhythm
                      + 0.20f * variation;
            ++count;
        }
    }

    return count > 0
        ? juce::jlimit (0.0f, 1.0f, scoreSum / (float) count)
        : 0.50f;
}

float MidiForgeAudioProcessor::composerGrammarScore (const Section& section) const
{
    if (section.notes.empty() || soundProfileFor (soundTarget).soloLine || section.bars < 4)
        return 0.52f;

    const auto plan = section.hasMelodyIntent
        ? section.melodyIntent.grammar
        : midiforge::ComposerGrammar::makePlan (
            section.bars, energy, complexity, melodyType, mood,
            hash32 (generationSeed ^ 0xC0A70970u));

    struct PhraseObs
    {
        int count = 0;
        float meanPitch = 60.0f;
        float meanVelocity = 80.0f;
    };

    std::vector<PhraseObs> observed (plan.phrases.size());

    for (const auto& n : section.notes)
    {
        if (n.channel != 3)
            continue;

        const int phrase = juce::jlimit (0, (int) observed.size() - 1, n.step / 64);
        auto& o = observed[(size_t) phrase];
        const float w = (float) o.count;
        o.meanPitch = (o.meanPitch * w + (float) n.note) / (w + 1.0f);
        o.meanVelocity = (o.meanVelocity * w + (float) n.velocity) / (w + 1.0f);
        ++o.count;
    }

    const float basePitch = observed.front().meanPitch;
    const float baseVelocity = observed.front().meanVelocity;

    float total = 0.0f;
    int used = 0;

    for (size_t i = 0; i < observed.size(); ++i)
    {
        const auto& o = observed[i];
        if (o.count < 2)
            continue;

        const auto& target = plan.phrases[i];
        const float density = juce::jlimit (0.0f, 1.0f,
            (float) o.count / 24.0f);
        const float densityFit = 1.0f
            - juce::jlimit (0.0f, 1.0f,
                std::abs (density - target.density) / 0.46f);

        const float registerLift = o.meanPitch - basePitch;