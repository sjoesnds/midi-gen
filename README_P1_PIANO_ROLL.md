# MIDI Forge — P1 Piano Roll Editing

The Piano Roll is now editable instead of display-only.

## Controls
- Left click empty space: add a note.
- Left drag a note: move it in time/pitch, snapped to 1/16 steps.
- Drag the right edge of a note: change note length.
- Alt + vertical drag: change velocity.
- Right click / Delete: remove the selected note.
- `1`–`4`: choose layer for newly added notes (Chords/Bass/Melody/Arp).
- `Ctrl+C` / `Ctrl+V`: copy/paste selected note.
- `Ctrl+Z` / `Ctrl+Y`: undo/redo Piano Roll edits.
- `Q`: quantize note starts to 1/8-note grid.
- Mouse wheel: vertical pitch scroll.
- Shift + wheel: horizontal time scroll.
- Ctrl + wheel: zoom.

Edits are written back to the selected variation, so MIDI export and layer drag-and-drop use the edited notes.
