#pragma once
// MIDI Forge 0.57 - Taste ML 2.0
//
// A small, local, online logistic-regression model that learns which loops the
// user likes.  No network, no external library.
//
//  * extractFeatures(): 17 musical features of a whole loop (melody + chords + bass)
//  * Model: p(like) = sigmoid(b + (w + w_sound + w_genre) . z), z = standardised features
//           - global weights w learn the general taste
//           - small, strongly regularised residuals per Sound target and per Genre
//             learn context-specific taste without overfitting a handful of ratings
//           - trained online by SGD; explicit LIKE / DISLIKE = weight 1,
//             implicit "dragged / exported to the DAW" = positive sample, weight 0.5
//
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace taste
{
    constexpr int kDim = 17;
    constexpr int kSounds = 8;
    constexpr int kGenres = 16;
    using Vec = std::array<float, kDim>;

    inline const char* featureName (int i)
    {
        static const char* const names[kDim] =
        {
            "dense",  "loud",  "dynamic",  "stepwise",  "skippy",  "leapy",  "repeated notes",
            "offbeat", "repeating rhythm", "colourful", "wide range", "high register", "long notes",
            "legato", "full chords", "busy bass", "in-chord melody"
        };
        return names[juce::jlimit (0, kDim - 1, i)];
    }

    inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }

    // NoteT needs: step, length, note, velocity, channel (1 chords, 2 bass, 3 melody).
    template <typename NoteT>
    inline Vec extractFeatures (const std::vector<NoteT>& notes, int bars)
    {
        Vec f;
        f.fill (0.0f);
        const int barsN = std::max (1, bars);

        std::vector<const NoteT*> mel;
        int bassNotes = 0;
        std::vector<std::vector<int>> chordPcs ((size_t) barsN);
        std::vector<int> firstChordStep ((size_t) barsN, 1 << 30);
        for (const auto& n : notes)
        {
            if (n.channel == 3) mel.push_back (&n);
            else if (n.channel == 2) ++bassNotes;
            else if (n.channel == 1)
            {
                // comping patterns do not always start on step 0: use the bar's first chord hit
                const int b = std::min (barsN - 1, std::max (0, n.step / 16));
                chordPcs[(size_t) b].push_back (((n.note % 12) + 12) % 12);
                firstChordStep[(size_t) b] = std::min (firstChordStep[(size_t) b], n.step);
            }
        }
        int chordHits = 0;
        for (const auto& n : notes)
            if (n.channel == 1)
            {
                const int b = std::min (barsN - 1, std::max (0, n.step / 16));
                if (n.step == firstChordStep[(size_t) b]) ++chordHits;
            }
        std::stable_sort (mel.begin(), mel.end(), [] (const NoteT* a, const NoteT* b) { return a->step < b->step; });

        if (! mel.empty())
        {
            const float n = (float) mel.size();
            f[0] = clamp01 (n / (barsN * 8.0f));

            double sum = 0.0, sum2 = 0.0;
            for (auto* m : mel) { sum += m->velocity; sum2 += (double) m->velocity * m->velocity; }
            const double mean = sum / n;
            const double var = std::max (0.0, sum2 / n - mean * mean);
            f[1] = clamp01 ((float) mean / 127.0f);
            f[2] = clamp01 ((float) std::sqrt (var) / 25.0f);

            int steps = 0, skips = 0, leaps = 0, repeats = 0;
            for (size_t i = 1; i < mel.size(); ++i)
            {
                const int d = std::abs (mel[i]->note - mel[i - 1]->note);
                if (d == 0) ++repeats;
                else if (d <= 4) ++steps;
                else if (d <= 7) ++skips;
                else ++leaps;
            }
            if (mel.size() > 1)
            {
                const float den = (float) (mel.size() - 1);
                f[3] = (float) steps / den;  f[4] = (float) skips / den;
                f[5] = (float) leaps / den;  f[6] = (float) repeats / den;
            }

            int offbeats = 0;
            for (auto* m : mel) if (m->step % 4 != 0) ++offbeats;
            f[7] = (float) offbeats / n;

            // repeated bar rhythm (a hook repeats)
            if (barsN >= 2)
            {
                std::vector<std::vector<int>> sig ((size_t) barsN);
                for (auto* m : mel)
                    sig[(size_t) std::min (barsN - 1, std::max (0, m->step / 16))].push_back (m->step % 16);
                int repeated = 0, counted = 0;
                for (int b = 1; b < barsN; ++b)
                {
                    if (sig[(size_t) b].empty()) continue;
                    ++counted;
                    for (int a = 0; a < b; ++a)
                        if (sig[(size_t) a] == sig[(size_t) b]) { ++repeated; break; }
                }
                f[8] = counted > 0 ? (float) repeated / (float) counted : 0.0f;
            }

            std::array<bool, 12> pcSeen {};
            int lo = 127, hi = 0;
            std::vector<int> pitches;
            for (auto* m : mel)
            {
                pcSeen[(size_t) (((m->note % 12) + 12) % 12)] = true;
                lo = std::min (lo, m->note); hi = std::max (hi, m->note);
                pitches.push_back (m->note);
            }
            int distinct = 0; for (bool b : pcSeen) distinct += b ? 1 : 0;
            f[9] = clamp01 ((float) distinct / 7.0f);
            f[10] = clamp01 ((float) (hi - lo) / 24.0f);
            std::nth_element (pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end());
            f[11] = clamp01 ((float) (pitches[pitches.size() / 2] - 48) / 48.0f);

            double lenSum = 0.0;
            std::vector<bool> covered ((size_t) barsN * 16, false);
            for (auto* m : mel)
            {
                lenSum += m->length;
                for (int s = m->step; s < m->step + std::max (1, m->length) && s < barsN * 16; ++s)
                    if (s >= 0) covered[(size_t) s] = true;
            }
            f[12] = clamp01 ((float) (lenSum / n) / 8.0f);
            int cov = 0; for (bool c : covered) cov += c ? 1 : 0;
            f[13] = (float) cov / (float) (barsN * 16);

            int strong = 0, agree = 0;
            for (auto* m : mel)
            {
                if (m->step % 8 != 0) continue;
                const int b = std::min (barsN - 1, std::max (0, m->step / 16));
                const auto& pcs = chordPcs[(size_t) b];
                if (pcs.empty()) continue;
                ++strong;
                if (std::find (pcs.begin(), pcs.end(), ((m->note % 12) + 12) % 12) != pcs.end()) ++agree;
            }
            f[16] = strong > 0 ? (float) agree / (float) strong : 0.5f;
        }
        f[14] = clamp01 ((float) chordHits / (float) barsN / 6.0f);
        f[15] = clamp01 ((float) bassNotes / (float) barsN / 4.0f);
        return f;
    }

    class Model
    {
    public:
        void reset() { *this = Model(); }

        float logit (const Vec& z, int sound, int genre) const
        {
            const int s = std::min (kSounds - 1, std::max (0, sound));
            const int g = std::min (kGenres - 1, std::max (0, genre));
            float a = bias;
            for (int i = 0; i < kDim; ++i)
                a += (w[(size_t) i] + ws[(size_t) s][(size_t) i] + wg[(size_t) g][(size_t) i]) * z[(size_t) i];
            return a;
        }
        float predict (const Vec& z, int sound, int genre) const
        {
            return 1.0f / (1.0f + std::exp (-std::min (20.0f, std::max (-20.0f, logit (z, sound, genre)))));
        }

        // y = 1 like, 0 dislike. Taste ML 2.0 balances classes so a long run of
        // one-sided feedback cannot drown the less frequent signal.
        void update (const Vec& z, int sound, int genre, float y, float weight)
        {
            const int s = std::min (kSounds - 1, std::max (0, sound));
            const int g = std::min (kGenres - 1, std::max (0, genre));
            const float safeWeight = std::max (0.05f, weight);
            const float sameMass = y > 0.5f ? juce::jmax (0.5f, pos) : juce::jmax (0.5f, neg);
            const float otherMass = y > 0.5f ? juce::jmax (0.5f, neg) : juce::jmax (0.5f, pos);
            const float balanceScale = juce::jlimit (0.55f, 1.80f,
                std::sqrt (otherMass / sameMass));
            const float effectiveWeight = safeWeight * balanceScale;
            const float err = (y - predict (z, sound, genre)) * effectiveWeight;
            const float lr = 0.14f / (1.0f + 0.03f * n);

            for (size_t i = 0; i < (size_t) kDim; ++i)
            {
                w[i] = clampW (w[i] + lr * (err * z[i] - 0.010f * w[i]));
                ws[(size_t) s][i] = clampW (ws[(size_t) s][i]
                    + 0.6f * lr * (err * z[i] - 0.060f * ws[(size_t) s][i]));
                wg[(size_t) g][i] = clampW (wg[(size_t) g][i]
                    + 0.6f * lr * (err * z[i] - 0.060f * wg[(size_t) g][i]));
            }

            bias += lr * 0.5f * err;
            n += safeWeight;
            (y > 0.5f ? pos : neg) += safeWeight;

            // Short-term memory: a small exponential prototype follows the latest
            // feedback while remaining bounded, so recent sessions can steer the
            // search without erasing the long-term model.
            const float alphaBase = safeWeight >= 1.0f ? 0.34f : 0.20f;
            Vec& center = y > 0.5f ? recentLike : recentDislike;
            float& mass = y > 0.5f ? recentLikeMass : recentDislikeMass;
            const float alpha = mass <= 0.0f ? 1.0f : alphaBase;
            for (int i = 0; i < kDim; ++i)
                center[(size_t) i] = center[(size_t) i] * (1.0f - alpha) + z[(size_t) i] * alpha;
            mass = juce::jmin (12.0f, mass + safeWeight);
        }

        // 0..1: how much the search may trust the model. Balanced positive and
        // negative evidence is stronger than a one-sided history of equal size.
        float confidence() const
        {
            float c = std::min (1.0f, std::max (0.0f, (n - 1.5f) / 8.0f));
            const float total = pos + neg;
            if (total > 0.0f)
            {
                const float balance = 2.0f * juce::jmin (pos, neg) / total;
                c *= 0.72f + 0.28f * balance;
            }
            if (recentLikeMass <= 0.0f || recentDislikeMass <= 0.0f)
                c *= 0.86f;
            return juce::jlimit (0.0f, 1.0f, c);
        }
        float samples() const { return n; }

        // 0..1 confidence-adjusted short-term preference signal. Positive values
        // mean the candidate is closer to recently liked loops; negative values
        // mean it is closer to recently disliked loops. This is deliberately a
        // soft reranking signal, not a replacement for the learned classifier.
        float recentPreference (const Vec& z) const
        {
            const float likeMass = recentLikeMass;
            const float dislikeMass = recentDislikeMass;
            if (likeMass <= 0.0f && dislikeMass <= 0.0f) return 0.0f;

            auto similarity = [] (const Vec& a, const Vec& b)
            {
                float d = 0.0f;
                for (int i = 0; i < kDim; ++i)
                {
                    const float delta = a[(size_t) i] - b[(size_t) i];
                    d += delta * delta;
                }
                return std::exp (-d / (float) kDim / 2.25f);
            };

            const float likeSim = likeMass > 0.0f ? similarity (z, recentLike) : 0.0f;
            const float dislikeSim = dislikeMass > 0.0f ? similarity (z, recentDislike) : 0.0f;

            if (likeMass > 0.0f && dislikeMass > 0.0f)
                return clamp01 (0.5f + 0.5f * (likeSim - dislikeSim)) * 2.0f - 1.0f;
            if (likeMass > 0.0f)
                return 0.45f * likeSim;
            return -0.45f * dislikeSim;
        }

        // strongest learned preferences (global weights): positive = likes, negative = avoids
        void topPreferences (int& likeIdx, int& avoidIdx) const
        {
            likeIdx = avoidIdx = -1; float best = 0.25f, worst = -0.25f;
            for (int i = 0; i < kDim; ++i)
            {
                if (w[(size_t) i] > best)  { best = w[(size_t) i];  likeIdx = i; }
                if (w[(size_t) i] < worst) { worst = w[(size_t) i]; avoidIdx = i; }
            }
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("v", 2);
            o->setProperty ("dim", kDim);
            o->setProperty ("b", (double) bias);
            o->setProperty ("n", (double) n);
            o->setProperty ("pos", (double) pos);
            o->setProperty ("neg", (double) neg);
            juce::Array<juce::var> jw, jws, jwg;
            for (int i = 0; i < kDim; ++i) jw.add ((double) w[(size_t) i]);
            for (int s = 0; s < kSounds; ++s) for (int i = 0; i < kDim; ++i) jws.add ((double) ws[(size_t) s][(size_t) i]);
            for (int g = 0; g < kGenres; ++g) for (int i = 0; i < kDim; ++i) jwg.add ((double) wg[(size_t) g][(size_t) i]);
            o->setProperty ("w", jw); o->setProperty ("ws", jws); o->setProperty ("wg", jwg);
            juce::Array<juce::var> jrl, jrd;
            for (int i = 0; i < kDim; ++i) { jrl.add ((double) recentLike[(size_t) i]); jrd.add ((double) recentDislike[(size_t) i]); }
            o->setProperty ("recentLike", jrl);
            o->setProperty ("recentDislike", jrd);
            o->setProperty ("recentLikeMass", (double) recentLikeMass);
            o->setProperty ("recentDislikeMass", (double) recentDislikeMass);
            return juce::var (o);
        }
        bool fromVar (const juce::var& v)
        {
            auto* o = v.getDynamicObject();
            if (o == nullptr || (int) o->getProperty ("dim") != kDim) return false;
            auto* jw = o->getProperty ("w").getArray();
            auto* jws = o->getProperty ("ws").getArray();
            auto* jwg = o->getProperty ("wg").getArray();
            if (jw == nullptr || jws == nullptr || jwg == nullptr
                || jw->size() != kDim || jws->size() % kDim != 0 || jws->size() / kDim < 1
                || jws->size() / kDim > kSounds || jwg->size() != kGenres * kDim)
                return false;
            const int savedSounds = jws->size() / kDim;     // models saved by older versions had fewer sounds
            Model m;
            m.bias = (float) (double) o->getProperty ("b");
            m.n = (float) (double) o->getProperty ("n");
            m.pos = (float) (double) o->getProperty ("pos");
            m.neg = (float) (double) o->getProperty ("neg");
            for (int i = 0; i < kDim; ++i) m.w[(size_t) i] = (float) (double) (*jw)[i];
            for (int s = 0; s < savedSounds; ++s) for (int i = 0; i < kDim; ++i) m.ws[(size_t) s][(size_t) i] = (float) (double) (*jws)[s * kDim + i];
            for (int g = 0; g < kGenres; ++g) for (int i = 0; i < kDim; ++i) m.wg[(size_t) g][(size_t) i] = (float) (double) (*jwg)[g * kDim + i];

            // Taste ML 1.x files have no short-term memory; load them normally
            // and start the 2.0 recent-memory layer empty.
            auto* jrl = o->getProperty ("recentLike").getArray();
            auto* jrd = o->getProperty ("recentDislike").getArray();
            if (jrl != nullptr && jrd != nullptr && jrl->size() == kDim && jrd->size() == kDim)
            {
                for (int i = 0; i < kDim; ++i)
                {
                    m.recentLike[(size_t) i] = (float) (double) (*jrl)[i];
                    m.recentDislike[(size_t) i] = (float) (double) (*jrd)[i];
                }
                m.recentLikeMass = juce::jlimit (0.0f, 12.0f, (float) (double) o->getProperty ("recentLikeMass"));
                m.recentDislikeMass = juce::jlimit (0.0f, 12.0f, (float) (double) o->getProperty ("recentDislikeMass"));
            }
            *this = m;
            return true;
        }

    private:
        static float clampW (float v) { return std::min (3.0f, std::max (-3.0f, v)); }
        Vec w {};
        std::array<Vec, kSounds> ws {};
        std::array<Vec, kGenres> wg {};
        Vec recentLike {};
        Vec recentDislike {};
        float recentLikeMass = 0.0f;
        float recentDislikeMass = 0.0f;
        float bias = 0.0f, n = 0.0f, pos = 0.0f, neg = 0.0f;
    };
}
