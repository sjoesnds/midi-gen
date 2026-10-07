#include "RhythmGrammar.h"

namespace midiforge
{
void RhythmGrammar::apply (std::vector<Note>& melody,
                            int bars,
                            double bpm,
                            int baseRhythm,
                            float complexity,
                            float energy,
                            uint32_t seed,
                            int melodyType )
{
    bars = std::max (1, bars);
    bpm = std::clamp (bpm, 40.0, 240.0);
    if (melody.empty())
        return;

    std::sort (melody.begin(), melody.end(),
        [] (const Note& a, const Note& b)
        {
            if (a.step != b.step) return a.step < b.step;
            return a.velocity > b.velocity;
        });

    std::vector<std::vector<size_t>> barNotes ((size_t) bars);
    for (size_t i = 0; i < melody.size(); ++i)
    {
        const int bar = std::clamp (melody[i].step / 16, 0, bars - 1);
        barNotes[(size_t) bar].push_back (i);
    }

    const int phraseCount = (bars + 3) / 4;
    for (int phrase = 0; phrase < phraseCount; ++phrase)
    {
        const int phraseStart = phrase * 4;
        std::vector<int> anchorPositions;
        if (phraseStart < bars)
        {
            for (const auto index : barNotes[(size_t) phraseStart])
                anchorPositions.push_back (melody[index].step - phraseStart * 16);
            std::sort (anchorPositions.begin(), anchorPositions.end());
        }

        if (anchorPositions.empty())
            continue;

        for (int localBar = 0; localBar < 4 && phraseStart + localBar < bars; ++localBar)
        {
            const int bar = phraseStart + localBar;
            auto& indices = barNotes[(size_t) bar];
            if (indices.empty())
                continue;

            const Intent intent = intentForBar (localBar);
            const Shape shape = chooseShape (intent, baseRhythm, complexity, energy, bpm,
                                              hash32 (seed ^ (uint32_t) (phrase * 131 + localBar * 17 + 7)),
                                              melodyType);

            auto targetPositions = makePositions ((int) indices.size(), shape, bpm, baseRhythm,
                                                  complexity, energy,
                                                  hash32 (seed ^ (uint32_t) (phrase * 977 + localBar * 53 + 11)));

            if (intent == Intent::Contrast && targetPositions.size() >= 4 && complexity > 0.58f)
            {
                // Deliberate breathing space: keep a strong entrance/exit and
                // remove one middle candidate rather than creating random holes.
                targetPositions.erase (targetPositions.begin() + (long) targetPositions.size() / 2);
                if (targetPositions.empty())
                    targetPositions.push_back (0);
            }

            std::vector<int> finalPositions;
            finalPositions.reserve (indices.size());
            for (size_t n = 0; n < indices.size(); ++n)
            {
                const int anchor = anchorPositions[n % anchorPositions.size()];
                const int generated = targetPositions[n % targetPositions.size()];

                float targetWeight = 0.55f;
                if (intent == Intent::Anchor) targetWeight = 0.08f;
                else if (intent == Intent::Build) targetWeight = 0.45f;
                else if (intent == Intent::Contrast) targetWeight = 0.88f;
                else targetWeight = 0.30f;

                if (intent == Intent::Contrast && shape == Shape::CallResponse)
                {
                    const int response = (generated + 8) & 15;
                    finalPositions.push_back (nearestFree (
                        (int) std::lround (blendOnset (anchor, response, targetWeight)),
                        finalPositions));
                }
                else
                {
                    finalPositions.push_back (nearestFree (
                        (int) std::lround (blendOnset (anchor, generated, targetWeight)),
                        finalPositions));
                }
            }

            // Return bar restores the core identity and deliberately ends with
            // either a pickup or anticipation when the phrase is complete.
            if (intent == Intent::Release && ! finalPositions.empty())
            {
                const int desiredEnd = (bpm >= 170.0) ? 15 : 14;
                finalPositions.back() = nearestFree (desiredEnd, std::vector<int> (finalPositions.begin(), finalPositions.end() - 1));
                std::sort (finalPositions.begin(), finalPositions.end());
            }

            std::vector<std::pair<size_t, int>> remap;
            for (size_t n = 0; n < indices.size(); ++n)
                remap.push_back ({ indices[n], std::clamp (finalPositions[n], 0, 15) });
            std::sort (remap.begin(), remap.end(),
                [] (const auto& a, const auto& b) { return a.second < b.second; });

            const int maxLength = bpm >= 200.0 ? 2 : (bpm >= 170.0 ? 3 : (bpm >= 140.0 ? 5 : 8));
            for (size_t n = 0; n < remap.size(); ++n)
            {
                const size_t noteIndex = remap[n].first;
                auto& note = melody[noteIndex];
                const int localStep = remap[n].second;
                note.step = bar * 16 + localStep;
                const int nextStep = (n + 1 < remap.size())
                    ? remap[n + 1].second
                    : 16;
                const int gap = std::max (1, nextStep - localStep);
                note.length = std::clamp (note.length, 1, std::min (maxLength, gap));
                if ((localStep % 4) == 0) note.velocity = std::min (118, note.velocity + 2);
                else if ((localStep & 1) != 0) note.velocity = std::max (42, note.velocity - 1);
            }

            // 0.100 Musical Expression: complexity is expressed in the rhythm
            // itself. Simple material gets a small, intentional event budget;
            // medium material has room to develop; complex material keeps the
            // wider vocabulary. Preserve the entrance/exit and strongest beats.
            const int expressionCap =
                complexity < 0.34f ? (intent == Intent::Contrast ? 3 : 4)
                                   : complexity < 0.55f ? 6 : 8;

            if ((int) remap.size() > expressionCap)
            {
                std::vector<size_t> keep;
                keep.reserve ((size_t) expressionCap);

                auto keepIndex = [&] (size_t index)
                {
                    if (std::find (keep.begin(), keep.end(), index) == keep.end())
                        keep.push_back (index);
                };

                keepIndex (remap.front().first);
                keepIndex (remap.back().first);

                if (expressionCap > 2)
                {
                    std::vector<size_t> ranked;
                    ranked.reserve (remap.size() - 2);
                    for (size_t n = 1; n + 1 < remap.size(); ++n)
                        ranked.push_back (remap[n].first);

                    std::stable_sort (ranked.begin(), ranked.end(),
                        [&] (size_t a, size_t b)
                        {
                            const auto& na = melody[a];
                            const auto& nb = melody[b];
                            const int sa = (na.step % 16 == 0 || na.step % 16 == 8) ? 40 : 0;
                            const int sb = (nb.step % 16 == 0 || nb.step % 16 == 8) ? 40 : 0;
                            const int pa = sa + na.velocity + std::min (na.length, 8) * 3;
                            const int pb = sb + nb.velocity + std::min (nb.length, 8) * 3;
                            if (pa != pb) return pa > pb;
                            return na.step < nb.step;
                        });

                    for (const auto index : ranked)
                    {
                        if ((int) keep.size() >= expressionCap)
                            break;
                        keepIndex (index);
                    }
                }

                for (const auto& item : remap)
                    if (std::find (keep.begin(), keep.end(), item.first) == keep.end())
                        melody[item.first].length = 0;
            }
        }
    }

    melody.erase (std::remove_if (melody.begin(), melody.end(),
        [] (const Note& n) { return n.length <= 0; }),
        melody.end());

    std::stable_sort (melody.begin(), melody.end(),
        [] (const Note& a, const Note& b)
        {
            if (a.step != b.step) return a.step < b.step;
            return a.velocity > b.velocity;
        });
}

float RhythmGrammar::score (const std::vector<Note>& melody,
                            int bars,
                            double bpm,
                            int baseRhythm,
                            float complexity,
                            float energy)
{
    if (melody.empty())
        return 0.35f;

    bars = std::max (1, bars);
    bpm = std::clamp (bpm, 40.0, 240.0);

    std::vector<std::vector<int>> onsets ((size_t) bars);
    for (const auto& n : melody)
    {
        const int b = std::clamp (n.step / 16, 0, bars - 1);
        onsets[(size_t) b].push_back (n.step % 16);
    }
    for (auto& v : onsets)
        std::sort (v.begin(), v.end());

    float identity = 0.0f;
    float contrast = 0.0f;
    float returnFit = 0.0f;
    float pickup = 0.0f;
    int identityBars = 0;
    int contrastBars = 0;
    int returnBars = 0;

    for (int phraseStart = 0; phraseStart + 3 < bars; phraseStart += 4)
    {
        const auto& a  = onsets[(size_t) phraseStart];
        const auto& ap = onsets[(size_t) phraseStart + 1];
        const auto& b  = onsets[(size_t) phraseStart + 2];
        const auto& ar = onsets[(size_t) phraseStart + 3];
        if (! a.empty())
        {
            identity += similarity (a, ap);
            contrast += 1.0f - similarity (a, b);
            returnFit += similarity (a, ar);
            ++identityBars;
            ++contrastBars;
            ++returnBars;

            if (! ar.empty())
            {
                const int last = ar.back();
                pickup += (last >= (bpm >= 170.0 ? 14 : 13)) ? 1.0f : 0.35f;
            }
            else
                pickup += 0.25f;
        }
    }

    if (identityBars > 0)
    {
        identity /= (float) identityBars;
        contrast /= (float) contrastBars;
        returnFit /= (float) returnBars;
        pickup /= (float) identityBars;
    }
    else
    {
        identity = 0.55f;
        contrast = 0.50f;
        returnFit = 0.55f;
        pickup = 0.50f;
    }

    int totalNotes = (int) melody.size();
    int sixteenth = 0;
    int offbeat = 0;
    int gaps = 0;
    int longGaps = 0;
    for (int b = 0; b < bars; ++b)
    {
        const auto& v = onsets[(size_t) b];
        for (int x : v)
        {
            if (x & 1) ++sixteenth;
            if ((x % 4) != 0) ++offbeat;
        }
        for (size_t i = 1; i < v.size(); ++i)
        {
            ++gaps;
            if (v[i] - v[i - 1] >= 4) ++longGaps;
        }
    }

    const float sixteenthRatio = (float) sixteenth / (float) std::max (1, totalNotes);
    const float offbeatRatio = (float) offbeat / (float) std::max (1, totalNotes);
    const float targetSixteenth = std::clamp (
        0.16f + 0.30f * std::clamp ((float) (bpm - 120.0) / 80.0f, 0.0f, 1.0f)
        + 0.08f * complexity, 0.12f, 0.58f);
    const float targetOffbeat = std::clamp (
        0.28f + 0.08f * std::clamp ((float) (bpm - 120.0) / 80.0f, 0.0f, 1.0f)
        + 0.08f * energy, 0.18f, 0.56f);

    const float subdivisionFit = 1.0f
        - std::clamp (std::abs (sixteenthRatio - targetSixteenth) / 0.42f, 0.0f, 1.0f);
    const float offbeatFit = 1.0f
        - std::clamp (std::abs (offbeatRatio - targetOffbeat) / 0.34f, 0.0f, 1.0f);

    const float density = (float) totalNotes / (float) std::max (1, bars);
    const float densityTarget = 3.8f + 1.6f * std::clamp (complexity, 0.0f, 1.0f);
    const float densityFit = 1.0f
        - std::clamp (std::abs (density - densityTarget) / 4.5f, 0.0f, 1.0f);

    const float silenceFit = std::clamp (
        0.45f + 0.55f * (float) longGaps / (float) std::max (1, gaps), 0.0f, 1.0f);

    return std::clamp (
        0.26f * identity
        + 0.19f * contrast
        + 0.20f * returnFit
        + 0.13f * pickup
        + 0.09f * subdivisionFit
        + 0.07f * offbeatFit
        + 0.04f * densityFit
        + 0.02f * silenceFit,
        0.0f, 1.0f);
}
}
