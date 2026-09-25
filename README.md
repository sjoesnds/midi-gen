# MIDI Forge fixed project

## 0.47 Human Phrase Engine
- The melody generator now treats each four-bar idea as a phrase instead of four independent bars.
- The first bar becomes a compact motif fingerprint; A' preserves its contour with controlled variation.
- B contrasts/inverts the motif and peaks in the middle; A'' returns toward the motif and resolves the final note to a scale-safe chord tone.
- The engine stays grid-safe and sits on top of the existing MAGIC search, Taste ML, Smart Locks, and sound profiles.

## 0.46 Performance / State
- Slider-driven generation is debounced for 250 ms, so dragging a control no longer runs the full 1000-candidate search on every mouse tick.
- The duplicate MIDI export entry was removed; the main EXPORT .MID action is now the single UI path.
- Fixed Section Mode setter so the selected enum value is no longer discarded.
- Persisted SoundCloud lead mode, Smart Locks and Taste ML enable state in plugin state, while keeping older saved states backward-compatible.
- Added visible plugin version **0.46.0**.

Fixes:
- JUCE VST3-only compatibility (`JUCE_VST3_CAN_REPLACE_VST2=0`)
- Song enum/type name collision
- realtime variation snapshot
- non-const RNG velocity helper
- scale-safe chord/arp generation
- MIDI `.mid` export button

Push the project to GitHub and run the existing Windows Actions workflow.

## 0.18 Magic Composition Engine
See `README_MAGIC_0_18.md` for the expanded rhythm, motif, phrase and candidate-diversity engine.


## 0.36 Stability & QA
- Clears realtime note state during host release.
- Avoids holding the active-note lock while emitting MIDI.
- Uses a fixed-size audio-thread note snapshot (no per-block vector allocation).
- Temporary MIDI drag files are deleted after the native drag operation returns.


### 0.36.1 Drag-to-FL Studio fix
The temporary MIDI file is kept alive until the external drag operation completion callback, preventing it from being deleted before FL Studio can consume it.
