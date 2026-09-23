# MIDI Forge 0.16 — Magic Candidate Engine

## What changed

0.16 changes the generation philosophy from **"make 8 random variations"** to **"search a larger musical space and select diverse candidates"**.

### Candidate search
- 48 complete loop candidates are generated per MAGIC generation.
- Candidates receive different variation identities and spread across the generator's existing musical controls.
- Each candidate is evaluated as a complete melody/loop rather than note-by-note.

### Heuristic judging
The first candidate scorer considers:
- hook/repetition balance;
- intentional space;
- contour changes;
- controlled leaps;
- pitch variety;
- density fit;
- learned taste nudges.

This is deliberately a foundation, not a claim that a mathematical score can identify a guaranteed hit.

### Diversity selection
The final 8 slots are selected greedily with a similarity penalty. Similar pitch-class sequences and rhythmic entrances are discouraged, so the eight visible variations should represent different ideas rather than octave copies.

### Layer locks
Existing Smart Lock behaviour is preserved after candidate selection.

## Next logical stage
A future version can replace the heuristic judge with a richer musical feature vector and/or a local ML scoring model. The current 48-candidate search gives us the correct architecture for that upgrade without requiring a neural model inside the plugin yet.

## QA target
Generate repeatedly and inspect the MIDI/Piano Roll. The important test is not simply "different random notes", but whether each MAGIC press explores noticeably different **musical ideas** while remaining playable and loopable.
