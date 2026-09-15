# MIDI Forge 0.11 — Russian Vocal DNA

The melody engine now uses the musical observations from the supplied analysis
of contemporary Russian vocal/rap toplines as a phrase-level prior.

Core ideas:
- speech-like melodic anchors: a line can sit on one pitch for much of a phrase;
- compact neighboring-scale-degree motion;
- a short descending minor-scale gesture near the phrase tail, commonly 5-4-3;
- related variants such as 5-5-4-3, 4-5-4-3 and 5-4-2-3;
- the gesture is rotated and probabilistic, so it does not become the entire
  song's formula;
- development/peak bars still provide wider movement and contrast;
- the final note remains subject to harmonic cadence rules.

This is a stylistic prior, not a claim that every Russian song uses the same
pattern. It is intentionally applied at phrase level to make the generator
feel more like a modern vocal topline rather than a generic scale exercise.
