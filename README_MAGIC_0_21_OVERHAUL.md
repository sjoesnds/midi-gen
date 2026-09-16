# MIDI Forge 0.21 — Magic Overhaul

MAGIC is now a coherent whole-state exploration pass. It randomizes the musical state, then searches 1000 candidates and keeps 8 diverse winners.

## New musical axes
- Genre DNA
- Mood DNA
- Melody Type
- Era DNA
- existing complexity / density / rhythm / harmony controls

## Workflow
`MAGIC -> coherent state -> 1000 candidates -> musical judge -> similarity/diversity -> 8 variations`

Additional actions:
- REROLL / NEW SEED: same musical DNA, new generation identity
- MUTATE: edit the selected loop without replacing its identity
- EVOLVE: gentle mutation of the selected loop

The implementation remains algorithmic. It is not presented as a trained ML model.
