# Melody generation — 0.7.0

The melody generator uses a chart-informed statistical prior rather than copying individual songs.

It emphasizes:
- strong beat hierarchy and syncopated off-beat support;
- repeated motifs with controlled A/A' variation;
- mostly small scale-degree intervals with occasional larger leaps;
- longer phrase-ending notes;
- rests and lower density where appropriate;
- chord-tone attraction while remaining scale-safe;
- velocity phrasing;
- a substantially sparser profile for SOUNDCLOUD mode.

The model is deliberately a prior, not a claim that an exact note-level dataset of the
10,000 most popular songs from the last 12 months was available. Macro chart metadata
and symbolic MIDI/pop references inform the weighting without reproducing particular songs.
