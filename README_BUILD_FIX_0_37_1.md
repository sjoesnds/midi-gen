# 0.37.1 Build fix

- `DragHandle` was missing the members `lastDragStart` and `activeDragFile` (C2065 / C3079).
  Both are now declared in `PluginEditor.h`.
- `activeDragFile = {}` is ambiguous for `juce::File`; replaced with `juce::File()`.
- The previous temporary .mid is deleted when the next drag starts (no temp-file leak).
- `LayerDragHandle` now uses itself as the native drag source (same as `DragHandle`), not the whole editor.
- CMake project version bumped to 0.37.1.

Syntax-checked with GCC against JUCE 8.0.14 headers; full MSVC build must be confirmed by GitHub Actions.
