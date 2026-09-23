# MIDI Forge 0.30 — Evolution Engine

Evolution now treats the selected loop as a parent and searches eight nearby
offspring. Every offspring starts from the same parent, then uses the existing
layer-aware Musical Mutations system. A lightweight musical fitness function
scores density, pitch variety, contour turns, controlled leaps, rhythm movement,
and register. The parent remains eligible, so evolution cannot replace a loop
just because it changed. Repeated Evolve calls gradually increase mutation
pressure up to a safe cap.
