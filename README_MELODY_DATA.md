# MIDI Forge — chart-informed melody engine

## What changed

The melody generator now uses a statistical prior instead of uniform/random note placement. The prior controls:

- beat-position probability (strong beats are preferred, with selected off-beat movement);
- repetition / motif retention across bars;
- scale-degree interval distribution (mostly small steps, occasional larger skips);
- chord-tone attraction on accents and phrase endings;
- note-duration distribution, with longer endings;
- restrained rests and ornamentation;
- phrase-aware velocity shaping;
- a narrower, sparser variant for SOUNDCLOUD mode.

## Data basis

There is no public, authoritative dataset that exposes the exact note-by-note melody statistics of the "10,000 most popular songs released in the last 12 months". The available July-2025 top-10k Spotify snapshot contains 10,000 tracks and metadata/audio features such as popularity, tempo, key, mode, danceability and energy. Spotify's 2025 Wrapped also reports broad current-market trends. For symbolic melody structure, the 2025 Pop-K MIDI dataset provides a large modern-pop reference set containing lead melodies, chords and bass.

Therefore this implementation deliberately uses **broad statistical priors** rather than copying or reconstructing individual songs. It does not contain copyrighted melodies or lyrics.

Sources used during development:

- Spotify top-10k snapshot (July 2025): https://www.kaggle.com/datasets/serkantysz/annas-archive-top-10k-spotify-songs-metadata-2025
- Spotify 2025 Wrapped trends: https://newsroom.spotify.com/2025-12-03/wrapped-music-trends/
- Pop-K MIDI dataset (2025): https://github.com/patchbanks/Pop-K-MIDI-Dataset
- Pop-K Zenodo record: https://zenodo.org/records/14791511
- Current 2026 Spotify seasonal/chart context was checked separately; it was not treated as a 10k note-level dataset.

## Important limitation

The engine is **chart-informed, not trained on the top-10k audio itself**. If a licensed or otherwise permitted note-level corpus covering the exact target period becomes available, the prior can be recalibrated from measured note/rest/duration/interval/chord-tone histograms instead of hand-set broad priors.
