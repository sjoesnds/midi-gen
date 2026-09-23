# MIDI Forge 0.13 — Loop-only architecture

The song/arrangement generator is removed from the user workflow. MIDI Forge now focuses on generating a single coherent loop.

- One loop only, 1–16 bars.
- Melody is generated from the first bar instead of being suppressed in the old intro section.
- Chords, bass, melody and arp no longer depend on intro/verse/chorus section indices.
- Legacy sectionMode state is still read/written for preset compatibility, but it is forced to Loop and hidden from the UI.
- The goal is a strong, reusable musical loop rather than a pre-arranged “school competition” song.
