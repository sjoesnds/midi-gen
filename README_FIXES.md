# Verification / fixes applied

Based on the uploaded current project:

- Kept JUCE 8.0.14 and VST3-only compatibility fix.
- Added shallow JUCE fetch to reduce first-time GitHub Actions download time.
- Fixed `currentBpm` access to be atomic between audio and GUI/export paths.
- Fixed `File::createOutputStream()` ownership usage in MIDI export.
- Made async MIDI export callbacks use `Component::SafePointer`.
- Kept the existing Piano Roll, Hook Mode, Smart Lock and per-layer MIDI drag features.

Local CMake configuration could not complete in this environment because outbound
DNS/network access to github.com is unavailable, so the final compilation must be
confirmed by the project's GitHub Actions Windows runner.
