# MIDI Forge 0.12 — Russian Vocal Topline Engine

The melody engine now treats the Russian-vocal idea as a phrase-level topology,
not as a note pattern pasted onto individual bars.

## Core behavior

- Selects the vocal DNA once per 4-bar phrase.
- Keeps a speech-like anchor through most of bars 1–3.
- Uses 5/4/3 scale-degree vocabulary in minor keys.
- Reserves the strongest movement for the end of the phrase.
- Supports 5-4-3 and related variants such as 5-5-4-3 and 4-5-4-3.
- Keeps harmonic gravity, but allows controlled non-chord tension.
- Uses deterministic phrase selection so regeneration changes musical decisions
  rather than accidentally changing the architecture from bar to bar.
- Restricts this particular vocal prior mainly to Universal, Trap and BoomBap,
  leaving other genres freer.

The goal is a recognizable topline behavior: **speech-like anchor → short movement →
landing**, rather than a generic random MIDI melody.
