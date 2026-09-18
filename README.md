# MIDI Forge fixed project

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
