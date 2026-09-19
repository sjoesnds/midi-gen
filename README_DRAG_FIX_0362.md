# 0.36.2 Drag source fix

The native JUCE external MIDI drag now explicitly passes the top-level plugin
editor as `sourceComponent`. This avoids relying on host/VST wrapper hit
testing when starting the OS drag from the embedded plugin UI.

The temporary MIDI file is kept after the drag operation instead of being
deleted immediately, so FL Studio can finish reading it reliably.
