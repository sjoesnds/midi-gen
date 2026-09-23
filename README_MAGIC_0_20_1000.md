# MIDI Forge 0.20 — 1000 Candidate Magic Search

MAGIC now searches **1000 complete loop candidates** instead of 96 before selecting the final 8.

The final workflow remains:

`1000 candidates -> musical judge -> similarity/diversity selection -> 8 variations`

This is a search-space expansion, not a claim of a trained ML model. The current judge remains deterministic/heuristic and uses melody, rhythm, motif, seam, register, surprise, density, genre DNA and diversity features.

The UI still exposes only the final 8 variations. The larger candidate pool is an internal quality/search improvement.

## Important

Candidate generation happens when the variation bank is rebuilt. A larger pool can increase generation CPU time, so profiling should be done in the target DAW after building a Release version.
