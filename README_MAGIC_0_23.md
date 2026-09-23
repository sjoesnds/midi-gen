# MIDI Forge 0.23 — Harmony Intelligence

This update extends the 0.22 Phrase Memory/Humanization engine.

## Added

- Harmony-aware candidate judging against the generated chord layer.
- Strong-beat chord-tone preference with controlled tension/release scoring.
- Melody/bass/arp register separation.
- Groove identity scoring based on rhythmic identity and velocity accents.
- Existing Phrase Memory and Humanization remain active.
- 1000-candidate search and diversity selection remain unchanged.

The implementation is intentionally heuristic/algorithmic, not ML.
Timing remains grid-safe; no arbitrary timing jitter is introduced.
