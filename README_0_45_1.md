# MIDI Forge 0.45.1 - fixes from the code review

- **EXPORT .MID over an existing file**: JUCE `FileOutputStream` appends to an existing file, so the new loop was written
  behind the old one and the DAW read the OLD loop. The target is now deleted before writing (both export buttons).
- **MUTATE / EVOLVE**: all notes of one chord hit now share the timing / length roll (a triad is never torn apart),
  every press has its own random rolls (before: same loop + same press = same result), steps are clamped to the loop.
- **SWING in exported / dragged MIDI**: odd 16ths are late by swing/2 of a step (same rule as the live output).
  Swing is a monotonic time warp (note-on and note-off are each swung by their own step), so notes never start to overlap.
- **Live output**: note-on and note-off go through ONE queue. A swung note-on can no longer land beyond the block
  (invalid for VST3); note-offs are sent before note-ons at the same sample; a stale note-off of the same pitch is pulled
  forward so it can never cut a retriggered note. No allocation in `processBlock` for the queue (reserved in `prepareToPlay`).

## Quality checks (tests/qa_main.cpp): 7 new checks, all fail on 0.45.0 and pass now
export overwrite (both buttons), chords stay together after MUTATE, two MUTATE presses differ, swing in exported MIDI,
live MIDI stays inside the block at swing 0.75, on / off pair up (depth 0..1).
