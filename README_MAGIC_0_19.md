# MIDI Forge 0.19 — Musical DNA Engine

This release expands the loop generator beyond a small set of genre-specific switches.
Genre now acts as a soft musical DNA layer that influences rhythm identity, space,
density, register, leap behaviour, motif selection, bass phrasing and the candidate judge.

## Genre DNA

The existing genres remain compatible, and new profiles are added:

- Universal
- Trap
- House
- Techno
- Boom Bap
- Ambient
- Cinematic
- R&B
- Pop
- Drill
- DnB
- Jersey
- Afro
- Hyperpop
- Experimental
- Lo-Fi

The profiles are intentionally soft rather than hard templates. MAGIC can still explore
unusual candidates instead of producing the same pattern every time.

## Generator changes

- generationSeed participates in genre/archetype identity
- rhythm family is biased by genre DNA
- deliberate off-grid syncopation remains available only to suitable profiles
- melody space, leap behaviour, register and motif behaviour receive genre-specific bias
- bass phrasing now differs for several new genre families
- progression selection recognizes the new genres
- the candidate judge rewards candidates that actually express the selected genre DNA
- the generic Magic Judge remains dominant, so genre preference does not become a rigid rule

## Important

This release has been source-reviewed and packaged, but it has not been claimed as a
successful JUCE/Visual Studio build in this environment. The external JUCE dependency
was not available for a complete build test here.
