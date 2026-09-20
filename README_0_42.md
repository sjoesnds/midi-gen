# MIDI Forge 0.42

## New Sound profiles
- **808 / Sub Lead**: melody in the 808 register (median ~D#2..), long legato notes, 2-3 notes per bar, slides;
  the separate bass layer is switched off (the melody is the bass voice).
- **Guitar**: E2..E5 range, medium-short notes, picking dynamics, half-bar chord strums, occasional slides.

## Articulation (new dropdown: Off / Slides / Slides + Vibrato; default Off)
Works for Synth Lead and 808 (Guitar: slides). Decided from the melody line when a MIDI file is written (drag / export),
so it survives piano-roll edits.
- **Slide** = the note runs one step into the next one (legato overlap). Turn the synth to mono / legato with portamento
  and it glides; a polyphonic patch just overlaps the notes.
- **Vibrato** = mod wheel (CC1) 0 -> 45 -> 85 -> 0 on long notes. Map CC1 to vibrato depth in the synth.
- Not applied to live MIDI output (only to exported / dragged MIDI). Piano / Pluck / Bell / Pad / Brass ignore it.

## AUTO-NEXT
DISLIKE now moves to the next loop automatically (the 8 variations; after the last one a new MAGIC). Toggle `AUTO-NEXT`.

## Automatic quality checks (GitHub Actions: .github/workflows/quality.yml)
`tests/` builds the generator headless on Linux and fails the build when the music gets worse:
every loop has full triads, a bass anchor on beat 1 (no bass in 808), a one-line melody, bass >= E1;
over 120 MAGIC loops: density, smooth intervals, repeating groove, groove variety, plain triads, no clusters;
each Sound profile really differs; articulation only where it should be; Taste ML learns and survives restart/reset;
project state round-trip and old-project compatibility; AUTO-NEXT.
Run locally: `cmake -S tests -B build-qa -DCMAKE_BUILD_TYPE=Release && cmake --build build-qa && ./build-qa/MidiForgeQA_artefacts/Release/MidiForgeQA`
(Linux needs: libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libasound2-dev).
