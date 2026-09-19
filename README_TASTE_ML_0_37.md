# 0.37 Taste ML

Adds a local online logistic-regression taste model trained from Like/Dislike feedback.

Features: density, velocity, leap, rhythm identity, repetition, pitch variety, register, note length.
The model is persisted in the existing preferences JSON and contributes a capped score to candidate judging.
No network calls or external ML services are used.
