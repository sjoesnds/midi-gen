# MIDI Forge 0.45 - Drum section

Problem: all drums went out as one channel-10 track with General-MIDI pitches, so on a single sampler
kick / snare / hats came out as one jumble of pitched copies of the same sample.

## DRUM VIEW (button next to DRUMS; opens automatically when DRUMS is switched on)
A step grid with one labelled, coloured row per instrument: KICK, SNARE, CLAP, HAT, OPEN HAT, TOMS, CRASH, SHAKER.
- click a step = add / remove a hit; `<` `>` = pages of 2 bars
- `M` = mute an instrument (silent in the live output and left out of the full export)
- `DRAG` on a row = drag only that instrument into FL (its own channel / sampler)
- `DRAG ALL` and the bottom `Drag Drums` = one file, one named track per instrument
- Pitch mode: **all on C5** (default, every hit plays its sample unpitched on its own sampler channel; toms keep relative pitches)
  or **General MIDI** (kit pitches 36 / 38 / 39 / 42 ... for a GM drum kit / FPC).
- Drums no longer clutter the piano roll; MUTATE / EVOLVE only touch drum velocities.
- The project remembers mute mask and pitch mode (older projects load fine).

## Quality checks (tests/qa_main.cpp)
one named track per instrument, single pitch (C5) per track, GM mode keeps kit pitches, `Drag Kick` writes only the kick,
mute leaves the instrument out of the full file but it is still draggable alone, clicking a step adds / removes a hit,
state round-trip incl. mute mask and pitch mode.
