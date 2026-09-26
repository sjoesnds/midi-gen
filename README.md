# MIDI Forge

**MIDI Forge** is a JUCE/VST3 MIDI composition plugin for FL Studio. The goal is not to spray random notes, but to generate coherent loops with rhythm, motif, harmony, phrasing, register, dynamics, variation and editable MIDI.

**Current version: 0.58.4**

### Phrase Tension Engine — 0.58.4
- Gives each four-bar loop an explicit A → A' → B → A'' tension target instead of relying only on contour and cadence side effects.
- Peak bars can favor delayed/offbeat entries, wider pitch destinations, non-chord tension tones and shorter fragments.
- The return bar eases toward grounded accents and longer sustain so the loop can breathe before restarting.
- The Judge now measures per-bar tension from chord-tone stability, offbeat activity, interval pressure and late-bar movement, then scores the phrase against the intended tension curve.
- Mood, Surprise and the existing tension profile modify the curve without forcing every loop into the same cadence.
- Loop-centric only: no song/arrangement system is added.

### Context-Aware Generation — 0.58.3
- Melody generation now reads the musical material already present in the current bar before selecting its own onsets.
- Chords, bass, drums and existing arps contribute a soft occupancy map, so the melody preferentially fills genuine gaps instead of stacking on every layer.
- Hook/Vocal-like roles retain more intentional anchor alignment, while Sparse Lead and ambient contexts favor open space.
- Chord-voicing register is measured before melody placement; when the lead lane overlaps the chord stack, the melody receives a soft upper-voice separation bias.
- The candidate Judge now scores backing/lead interplay: meaningful shared accents, use of open space and register separation all influence MAGIC selection.
- The feature remains loop-centric and deterministic; it does not add song/arrangement generation.

## What it does

### Composition
- Root note, scale and progression control
- Major, Minor, Dorian, Phrygian, Harmonic Minor, Melodic Minor and Pentatonic scales
- Multiple progression styles: Auto, Pop, Dark, Emotional, Cinematic, Jazz-like, Looping
- Melody roles: Hook, Vocal-like, Riff, Ostinato, Arp, Counter, Sparse Lead, Phrase
- Genre DNA for Trap, House, Techno, BoomBap, Ambient, Cinematic, RnB, Pop, Drill, DnB, Jersey, Afro, Hyperpop, Experimental and Lofi
- Mood, era and energy controls
- Sound-target profiles for Piano, Pluck, Synth Lead, Bell/Mallet, Pad/Strings, Brass, 808/Sub Lead and Guitar

### MAGIC generation
MAGIC is a search/composition engine rather than a single randomization pass.

The generator:
1. builds candidate musical ideas;
2. scores density, space, repetition, contour, leaps, rhythmic identity, motif identity, phrase memory, phrase arc, register, surprise and other musical features;
3. uses Taste ML re-ranking when enabled;
4. selects a diverse variation bank instead of returning the first few candidates.

The current search uses a large candidate pool and deterministic seeds so generation remains reproducible.

### MAGIC 3 — 0.51
- MAGIC now searches through eight explicit archetypes instead of one generic candidate pool.
- **HOOK** favors memorable repeated cells and strong phrase identity.
- **GROOVE** emphasizes pocket, offbeats, accents and sustain.
- **HARMONY** pulls melody and bass destinations toward active chord tones.
- **MOTIF** repeats a two-bar fingerprint through the arrangement.
- **MINIMAL** removes lower-value notes and supporting clutter while keeping anchors.
- **WEIRD** introduces controlled leaps and asymmetric timing without leaving the scale.
- **EMOTIONAL** shapes register and dynamics into a rise, peak and release.
- **WILDCARD** deliberately mixes lighter mutations to escape the main archetypes.
- The final variation bank contains one winner from each archetype, then applies the existing diversity, Taste ML and Smart Lock systems.

### MAGIC 4 — Adaptive Search — 0.55
- Keeps the existing 1000-candidate budget, but splits the search into two phases instead of treating every candidate as equally blind exploration.
- The first 600 candidates explore the current musical space broadly.
- The best 48 first-pass candidates are rank-weighted to build an adaptive profile of density, space, rhythm, motif, leap, register, surprise, loop quality, groove and motif memory.
- The remaining 400 candidates are generated with their melody controls, phrasing, swing, register and variation amount nudged toward that discovered neighborhood.
- Second-pass candidates also receive a soft feature-distance bonus, so adaptive exploitation cannot erase the existing genre, DNA, archetype, Judge or diversity systems.
- Local deterministic jitter keeps the adaptive phase exploratory instead of collapsing all candidates into clones.
- The final eight-variation bank and one-winner-per-archetype behavior remain unchanged.

### Loop Transformation — 0.56

MAGIC 4 now feeds its strongest discovered loop into a dedicated transformation stage. The generator remains loop-centric: there is no song/arrangement layer.

Each generation produces eight standalone variants derived from the same musical source:

- **ORIGINAL** — untouched identity reference.
- **TIGHT** — cleaner timing and slightly tighter note tails.
- **SPARSE** — selective note removal while preserving melody anchors, bass and chord structure.
- **DARK** — lower register and softer dynamics while keeping scale/harmonic identity.
- **BIGGER** — wider register, longer phrases and expanded chord voicing.
- **WEIRD** — controlled contour inversions and micro-timing mutations.
- **TIGHT+WEIRD** — combination of the two transformation domains.
- **SPARSE+DARK** — reduced density with a darker register.

The source loop is transformed deterministically from its generation identity, so reruns remain reproducible while the variants stay recognizably related.


### Taste ML 2.0 — 0.57

The Taste ML layer now combines long-term learning with a bounded short-term preference memory.

Class-balanced training prevents repeated LIKE or DISLIKE feedback from dominating the opposite signal. A recent liked/disliked prototype follows the latest ratings with exponential decay, then contributes a deliberately small reranking bonus on the next generation.

Confidence is also balanced-aware: the model trusts histories with evidence on both sides more than equally large one-sided histories. Existing saved Taste ML data remains compatible; older models load with the new short-term layer initialized empty.


### Loop Editor 3.0 — 0.58

The piano roll is now a focused loop editor rather than only a note inspector.

The editor adds:

- **Alt-drag marquee selection** for rectangular multi-note selection.
- **BAR** and existing **PHRASE** selection for fast musical regions.
- **FRAME** to zoom/pan directly to the current selection.
- **DUP** to duplicate a selected region inside the current loop.
- **REV** to reverse the selected material in time while preserving note lengths.
- **2X / HALF** to compress or expand selected timing.
- **ROT** to rotate selected material by a quarter of its local region.
- **VEL 100** to normalize the selected velocities to their current average.

All destructive editor operations use the existing edit history, so they participate in Undo/Redo. The transformations stay inside the selected loop and do not introduce any song/arrangement architecture.


### Groove Engine — 0.54
- Builds one deterministic pocket profile per candidate and shares it across melody, bass, chords, arp and drums.
- Uses different layer strengths so the parts feel related without moving as a rigid block.
- Adds shared accent hierarchy on downbeats and selected offbeats instead of independent random velocities.
- Shapes sustain and note length around accents so phrasing changes with the groove rather than only timing.
- Re-evaluates the complete loop with a groove score covering accent contrast, offbeat pocket, layer interaction, occupancy and sustain.
- Keeps the system inside the loop: no song sections, arrangement layer or extra UI controls are introduced.

### Motif Memory 2.0 — 0.53
- Recognizes a loop's main motif from rhythm and relative pitch shape instead of absolute notes.
- Tracks a secondary motif so a loop can contain both a primary identity and a contrasting answer.
- Learns rhythmic fingerprints independently from pitch, so motif identity survives transposition and small pitch changes.
- Tracks a bass fingerprint from bar-to-bar movement, keeping the harmonic foundation part of the loop's identity.
- Rewards recurrence with controlled transformation instead of rewarding literal bar-for-bar copying.
- Penalizes mechanical copying when every later bar becomes nearly identical to the opening motif.
- Feeds the motif-memory score into the normal MAGIC Judge and gives the MOTIF archetype a stronger focus, without creating a separate generator or adding new UI controls.

### Loop Quality 2.0 — 0.52
- Judges the whole loop as one circular musical object instead of scoring only isolated notes or bars.
- Checks the loop seam for both pitch continuity and intentional breathing room at the wrap point.
- Measures motif recurrence using transposition-safe rhythm and contour fingerprints.
- Rewards controlled density movement between bars instead of flat repetition or chaotic changes.
- Evaluates melody/bass accent interaction so layers support each other without becoming mechanically locked.
- Adds a soft closure score for the final bar and final note so the loop hands naturally back to bar one.
- Keeps the existing MAGIC 3 archetype, DNA, diversity, Taste ML and Smart Lock systems in control; Loop Quality is an additional judge layer rather than a separate generator.

### Piano Roll 2.0 — 0.50
- Multi-select notes with Ctrl-click, Shift ranges and Ctrl+A.
- Group drag keeps relative timing and pitch relationships intact.
- Group transpose by octaves, scale-safe pitch snapping and selected-note quantization.
- Group humanize changes timing, velocity and sustain together instead of note-by-note drift.
- Four-bar PHRASE selection makes A/A' /B/A'' blocks easy to edit as one unit.
- Grid toolbar supports 1/16, 1/8, 1/4 and 1/2-step editing plus view reset.
- Selection state and edit history remain visible while working.

### Mutation 2.0 — 0.49
- MUTATE now chooses a musical mutation domain instead of independently randomizing notes.
- Motif mutations preserve the contour of the selected phrase while changing its destination or interval shape.
- Rhythm mutations move phrase segments together, keeping the rhythmic identity coherent.
- Cadence mutations reshape phrase endings toward scale-safe chord tones.
- Groove mutations reshape accents and sustain across melody, bass, chords, arp and drums.
- Smart Locks continue to freeze individual layers during mutation.
- EVOLVE uses the same engine at lower strength.

### Harmony 2.0 — 0.48
- Chord voice leading now compares individual voices between adjacent bars instead of only comparing chord centres.
- Common-tone retention is rewarded and unnecessary large voice jumps are penalized.
- Bass pitch mutations are snapped back into the active scale.
- Bass and late phrase notes can make controlled anticipation toward the next chord for stronger harmonic forward motion.

### Human Phrase Engine — 0.47
A four-bar idea is treated as a phrase with compositional roles:

**A → A' → B → A''**

- **A** establishes the melodic fingerprint.
- **A'** keeps the contour but changes sustain/accent detail.
- **B** contrasts/inverts the contour and provides the phrase peak.
- **A''** returns toward the identity and finishes with a scale-safe chord-tone resolution.

The phrase layer sits on top of the existing melody generator, so the underlying rhythm, sound profile, harmony and MAGIC search still participate in the result.

### Musical controls
- Chord, bass, melody and arp density
- Melody length, pause chance, leap chance and ghost notes
- Motif strength and variation amount
- Fill amount and energy
- Swing and humanization
- Complexity
- Chord extensions and inversions
- Arp rate
- Smart Locks for chords, bass, melody and arp
- SoundCloud lead mode
- Hook mode
- Optional articulation: off, slides, slides + vibrato

### Drums
The optional drum layer uses MIDI channel 10 with separate rows for:
- Kick
- Snare
- Clap
- Hat
- Open Hat
- Toms
- Crash
- Shaker

Rows can be muted independently and dragged separately.

### Piano roll
The built-in piano roll supports:
- add, move, resize and delete notes;
- velocity editing;
- grid snapping and quantization;
- zoom/scroll/playhead visualization;
- undo/redo and clear;
- editing of the currently selected variation.

### MIDI workflow
- Export the selected song as a standard MIDI file
- Drag the whole MIDI into FL Studio
- Drag individual layers
- Drag individual drum rows
- Live MIDI output from the plugin
- Host BPM tracking
- Swing-aware live scheduling and MIDI export

### Learning
**Taste ML** learns from LIKE/DISLIKE feedback and uses the accumulated preference profile to re-rank future candidates.

The plugin also supports:
- variation scoring;
- automatic advance after dislike;
- persistent taste data;
- enabling/disabling Taste ML;
- reset of learned taste data.

### Performance / stability — 0.46
Expensive generation triggered by sliders is debounced, so dragging a control does not launch the full candidate search on every mouse movement.

State handling also persists newer options such as:
- SoundCloud lead mode;
- Smart Locks;
- Taste ML enabled state.

Older saved states remain backward-compatible.

## Build

Requirements:
- CMake 3.22+
- a C++17 compiler
- JUCE 8.0.14 is fetched automatically by CMake

The project currently builds **VST3 only**.

Typical CMake flow:
```powershell
cmake -B build
cmake --build build --config Release
```

The generated VST3 can then be installed/copied into your FL Studio VST3 location.

## Project structure

```
CMakeLists.txt
Source/
  PluginProcessor.h
  PluginProcessor.cpp
  PluginEditor.h
  PluginEditor.cpp
tests/
.github/workflows/
```

## Architecture notes

The plugin keeps generation and realtime playback concerns separate:
- generation builds complete musical variations;
- the selected variation is copied into the realtime note state;
- realtime MIDI output uses a pending-event queue for note-on/note-off timing;
- active MIDI state is protected separately from the variation bank;
- the UI does not continuously trigger the expensive MAGIC search while a slider is being dragged.

## Version history

The repository previously contained many small README files created for individual milestones. Their useful information is consolidated here; the source code and current version are the source of truth.

### 0.47
- Human Phrase Engine
- Four-bar motif fingerprint
- A/A'/B/A'' phrase grammar
- Contrast/peak and scale-safe cadence

### 0.46
- Generation debounce
- Variation-selection preservation
- State persistence improvements
- Section Mode setter fix
- Duplicate export UI cleanup
- Visible version in the plugin UI

### 0.45.x
- Full drum section with per-instrument rows
- Chord comping style
- Improved 808/sub line
- MIDI swing and live note-on/off scheduling improvements
- Export overwrite handling
- Mutation/evolution improvements

### 0.42–0.44
- Articulation system
- Sound-oriented melodic behavior
- Musical-quality and harmony improvements
- Chord comping and richer musical arrangement controls

### 0.39–0.41
- Sound Profiles
- Taste ML improvements
- Fill Amount
- More humanized rhythmic and melodic behavior

### 0.36–0.38
- Realtime stability and QA work
- Drag-to-FL Studio fixes
- Taste ML foundation
- Register and generation-state foundations
- Windows/MSVC build fixes

### 0.27–0.35
- Rhythm Engine
- Harmony work
- Mutation / evolution
- Genre DNA and hybrid DNA
- Advanced MAGIC composition
- Larger candidate search and diversity improvements

### 0.13–0.26
- Loop-only architecture
- Melody/phrase evolution
- MAGIC composition engine
- Candidate judging and variation generation
- Melody quality refinements

### 0.08–0.12
- Early melody engine iterations
- Grid-aware note generation
- Basic loop composition foundations

## Design direction

MIDI Forge is being pushed through a clear progression:

**random notes → musical patterns → motifs → phrases → intention**

Future work should improve musical structure and editing quality rather than simply adding more controls.

## Source

https://github.com/sjoesnds/midi-gen