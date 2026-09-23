# MIDI Forge — P2 Undo / Redo

P2 adds a compact edit-history workflow to the Piano Roll.

- `Ctrl+Z` — Undo
- `Ctrl+Y` or `Ctrl+Shift+Z` — Redo
- `CLEAR` — remove all visible notes (undoable)
- history keeps up to 64 Piano Roll states
- history is reset when switching/generating variations, so undo never restores notes into the wrong variation
- visible `UNDO` / `REDO` buttons show whether an action is available

Undoable Piano Roll operations include note add, move, resize, velocity edits, delete, paste and quantize.
