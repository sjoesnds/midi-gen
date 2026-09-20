# MIDI Forge 0.44 - chord comping + Drums

## Chords as a rhythm (new dropdown: Chords: Auto / Held / Comping)
Chords used to be one held block per bar. Now each genre has comping cells:
house / techno offbeat stabs, DnB, trap long-short, jersey, afro 3-3-2, boom-bap and RnB long-short-long, pop quarter stabs.
The cell repeats (A A' B A'') like the melody. Same voicing on every hit, velocity accents per hit.
- Auto: comping for rhythmic genres; Universal / Pop / Cinematic / Ambient / Experimental stay held.
- Held: the old sustained block. Comping: force the genre pattern.
- Sounds that already stab (Pluck / Brass / Guitar) keep their own two hits.

## Drums layer (DRUMS button, off by default)
Internal channel 5, written to the MIDI file as General MIDI channel 10 (kick 36, snare 38, clap 39, closed hat 42,
open hat 46, crash 49, toms 43-50, shaker 70). Genre grooves (Trap, Drill, House, Techno, DnB, Boom-bap, RnB / Lofi,
Afro, Jersey, Pop ...), kick cell repeats (A A' B A''), hats with drop-outs / open hats, ghost snares, fills at the end of
a phrase (snare roll or tom run, chance and length follow Fill Amount), occasional crash on bar 1.
- `Drag Drums` handle exports only the drums (drop it on a drum sampler / FPC mapped to GM).
- 808 + Drums: the 808 line plays where the kick plays.
- MUTATE / EVOLVE only change drum velocities, never the groove.

## Fixes found by the new quality checks
- Piano-roll edits / MUTATE clamped channels to 1-4 (the drums would have turned into arp notes) - fixed.
- Chord features (judge, taste, quality checks) now work per bar / first chord hit, not only step 0.

## Quality checks (tests/qa_main.cpp): 8 new checks
Held vs Comping hit counts, drums off = no drum notes, kick on beat 1 of every bar, GM pitches only,
channel 10 in the file (never 5), 808 locked to the kick, MUTATE keeps the groove, new state fields + old projects load.
