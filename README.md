# MIDI Forge

**MIDI Forge** is a JUCE/VST3 MIDI composition plugin for FL Studio. The goal is not to spray random notes, but to generate coherent loops with rhythm, motif, harmony, phrasing, register, dynamics, variation and editable MIDI.

**Current version: 0.75.0**

### Composer Judge 2.0 — 0.75.0
- Adds a top-level coherence judge over the already-generated musical candidate instead of another note generator.
- Checks phrase-arc shape, Composer Grammar role consistency, motif development, expression/prosody agreement, harmonic coherence, density/space, rhythm, register, loop closure, novelty balance and groove.
- Adds a low-weight cross-system agreement check so contradictory local decisions are less likely to dominate a candidate ranking.
- Runs after the existing musical judges and before archetype-specific ranking; it does not rewrite MIDI and adds no UI controls.
- Keeps the existing MAGIC search architecture intact while giving it one whole-composition signal.
- Project version is now 0.75.0.
- No non-MIDI features were added.

### Cadence & Loop Closure 2.0 — 0.74.0
- Adds a dedicated boundary planner for the transition **end of loop → start of loop**.
- The closure can choose between direct return, answer, pickup, sustain/release, unresolved and deceptive seam behavior.
- Final notes can target the opening note, answer its opening interval, continue the opening gesture, or land on a context-aware harmonic tone.
- Pickup timing and release length are varied intentionally instead of applying one universal cadence recipe.
- A controlled unresolved bias allows some loops to remain slightly open so the next cycle supplies the resolution.
- Adds a Loop Closure Judge to Loop Forge, rewarding a coherent seam without forcing every ending into the tonic.
- Adds headless QA for deterministic closure planning, seam quality, return gesture and ending diversity.
- No new UI controls and no non-MIDI features.

### Motif Semantics 2.0 — 0.73.0
- Adds a semantic motif planner that treats a four-bar idea as **Rhythmic Core, Interval Core, Starting Anchor, Peak Gesture, Ending Gesture, Signature Leap** and **Answer Cell** instead of treating the motif as one undivided fingerprint.
- \`A'\` preserves the core while deliberately mutating one semantic component.
- \`B\` mutates a second component and raises contrast instead of simply inverting or copying the whole phrase.
- \`A''\` restores the recognizable motif identity while giving the ending a fresh gesture, so the loop can return without sounding pasted.
- The semantic plan is deterministic per composition identity and combines freely with Creative Range rather than replacing it.
- Adds a Motif Semantics Judge to Loop Forge so candidate search rewards recognizable development, controlled contrast and meaningful return.
- Adds headless QA for deterministic semantic planning, language diversity and actual four-bar A/A'/B/A'' behavior.
- No new UI controls and no non-MIDI features.

### Creative Range Engine — 0.72.0
- Adds a dedicated pre-generation creative-language planner instead of relying on independent random melody mutations.
- Each identity chooses a coherent combination of **contour**, **interval vocabulary**, **rhythm family**, **repetition style**, **register journey**, **harmonic personality** and **duration language**.
- Expands contour grammar from 12 to 18 shapes and interval vocabulary from 10 to 12 controlled motion languages.
- Repetition styles can emphasize motif identity, call/response, evolving ideas, loose development, A' variation, sequence, contrast or hook persistence.
- Register is treated as a journey across the phrase rather than only an octave clamp, so wide pitch space is used selectively.
- Harmonic personality now shifts the balance between grounded chord tones and deliberate color/tension tones.
- Duration language can make the same melodic idea feel flowing, punchy, declarative or long/short without adding timing noise.
- Adds a Creative Range Judge to Loop Forge so the candidate search rewards internally coherent variety instead of collapsing everything toward the average.
- Adds headless QA proving deterministic planning and a broad distribution of creative language combinations.
- No new UI controls and no non-MIDI features.

### Melodic Range Expansion — 0.71.1
- Expands the practical melody lane from the old compact ~2-octave window to a much wider register before profile caps.
- Piano can now explore roughly C3–E7 at the default octave setting, with sound-target biases for pluck, lead, bell, pad, brass and guitar.
- Wider profiles can use the additional register without forcing every generated phrase to span the full range; existing Composer/Prosody rules still control phrase shape and register movement.
- Extends bar-level octave placement search by one additional octave in both directions.
- Keeps the dedicated 808/Sub Lead register unchanged.
- Adds headless QA that checks the expanded range on actual generated Piano MIDI.
- No new UI controls and no non-MIDI features.

### Melodic Prosody — 0.71.0
- Adds note-level melodic intent: **Anchor**, **Pickup**, **Approach**, **Connect**, **Accent**, **Peak** and **Release**.
- Large intervals are treated as destinations: approach notes move toward the following target instead of remaining as arbitrary leaps.
- Peak notes receive a small controlled register lift and stronger accent; pickup notes become lighter/shorter; release notes settle and breathe.
- Prosody reads the 0.70 Composer Grammar role, so note-level intention follows the macro phrase arc instead of fighting it.
- Prosody runs before Harmonic Intelligence, allowing the harmony layer to resolve intentional gestures onto the active chord while preserving passing motion.
- Adds a Prosody Judge for approach direction, peak placement, release behavior and accent hierarchy.
- No new UI controls and no non-MIDI features.

### Composer Grammar — 0.70.0
- Adds a dedicated macro-level composition planner instead of another independent post-processing layer.
- Four-bar cells receive explicit roles such as **Statement**, **Develop**, **Build**, **Contrast/Peak**, **Release** and **Return** according to loop length, energy and complexity.
- The plan supplies soft targets for tension, density, register movement, sustain, velocity and cadence; the existing melody, rhythm, phrase-memory and harmony engines interpret those targets.
- Phrase Memory 4.0 now follows the Composer role when choosing development, contrast, fragmentation and return, so long-form memory and phrase arc share one structural intention.
- Adds a Composer Grammar Judge for macro tension, density, register movement and return/cadence behavior.
- Adds headless QA for deterministic role planning and identity-driven micro-variation.
- No new UI controls and no non-MIDI features.

### Phrase Memory 4.0 — 0.69.0
- Adds long-form memory across complete four-bar phrase cells instead of only inside one A/A'/B/A'' phrase.
- The opening four-bar idea becomes a persistent macro fingerprint reused by later phrase cells.
- Later cells can re-state, invert, fragment/lift or return to the remembered contour with deterministic variation.
- Rhythm is preserved while the macro pitch idea develops; Harmonic Intelligence then re-resolves transformed notes against the destination chords.
- Adds a Phrase Memory 4.0 Judge signal for contour retention, transformed contrast and non-literal variation.
- Adds 12-bar Piano-only QA so long-form memory is tested directly on MIDI.
- Keeps 808/Sub Lead outside the memory pass and adds no new UI controls.

### Harmonic Intelligence 2.0 — 0.68.0
- Adds a post-expression harmony pass that uses the actual chord voicings as destinations instead of only the scale.
- Strong and sustained melody notes are grounded toward active chord tones, while weak notes retain passing/color-tone freedom.
- Late-bar notes can anticipate the next chord and phrase seams use soft voice-leading gravity.
- Root notes are not blindly preferred; thirds and fifths receive context-aware preference to avoid turning the melody into a scale-safe arpeggio.
- Adds a harmonic Judge covering anchor stability, anticipation, resolutions, voice-leading and chord/color-tone balance.
- Adds Piano-only QA for harmonic anchoring, smooth phrase transitions and retained non-chord color.
- 808/Sub Lead remains outside the post-process.

### Expressive Melody Engine — 0.67.0
- Separates rhythmic grammar from melodic expression: Rhythm Grammar chooses phrase timing, while the new expression pass shapes contour, peaks, answers, sustain and velocity.
- Adds deterministic A / A' / B / A'' melodic gestures with controlled contour reuse and transformation instead of literal copying.
- Gives the B bar a real phrase peak and A'' a softer return so four-bar loops have an audible rise-and-release arc.
- Uses meaningful sustain and velocity dynamics even with Humanize OFF, so the Piano profile exposes musical expression directly.
- Adds an expression judge signal covering interval variety, useful leaps, contour turns, velocity range and phrase shape.
- Keeps the 808/sub-lead path excluded and adds QA coverage using Piano-only MIDI.

### Musical Quality & Phrase Intelligence — 0.66.0
- **Contextual Development** chooses Repeat/Alter, reduction, expansion, inversion, fragmentation, call/response or return from the phrase's actual density, motif identity, contour, tension, groove and loop state.
- **Chord-aware development** softly steers developed notes toward the real voicing of their destination bar instead of blindly snapping transformed pitches to the scale.
- **Cadence-aware closure** lets A'' resolve toward the loop-start harmony while preserving part of the developed contour, with a longer final release.
- **Structural invariant repair** preserves beat-1 bass anchors, beat-1 kicks and the E1–D3 808 lane after archetype/transform passes.
- **808 kick lock** repairs missing kick matches rather than rewriting the 808 rhythm.
- **Register guard** only trims pathological octave-leap density in ordinary profiles; Riff, Experimental, Cinematic and 808 languages keep their wider ranges.
- **Legacy state hygiene** resets newly added optional fields before reading them, so old presets cannot inherit stale UI state from a previous processor instance.


### Motif Development Engine — 0.64.0

The phrase engine now develops a motif as a deliberate four-bar statement instead of relying only on recurrence or diversity pressure. Each four-bar phrase keeps the first bar as its identity anchor and assigns one development grammar to A' / B / A'':
- Repeat + Alter
- Rhythmic Reduction
- Rhythmic Expansion
- Interval Expansion
- Inversion
- Fragmentation
- Call → Response
- Return

The development pass is scale-safe and preserves the generator's existing harmony, register and groove systems. It runs before the final MAGIC archetype/groove processing, so the new phrase logic stays inside the existing search-and-judge pipeline.

The MAGIC judge now includes a **Development Coherence** signal that evaluates A↔A' similarity, A↔B contrast, A↔A'' return, and the ordering between contrast and return. The score is deliberately soft so unconventional but strong phrases are not rejected.

### Melodic Memory 3.0 — 0.63.0
- Adds a transposition-safe idea fingerprint for every MAGIC candidate using onset rhythm, relative pitch contour and interval shape.
- The final variation bank now keeps an explicit memory of already selected musical ideas, not only note-level similarity and broad behavioral features.
- Near-clone motifs receive less selection pressure even when they come from different archetypes or are transposed/rephrased.
- Eight lightweight idea families (chant, wide leap, angular, rising, falling, pickup, conversational, balanced) provide an additional soft anti-clone signal.
- Musical quality remains the primary score; Idea Memory only influences variation selection and does not rewrite good candidates.
- Loop-centric only; no new UI controls are introduced.

### Humanize Mode — 0.62.0
- Human Performance is now an explicit opt-in mode and defaults to OFF for tight electronic MIDI.
- When enabled, the Humanize Amount controls phrase-aware timing movement, repeated-note articulation, velocity variation and small sustain changes.
- Timing changes are deliberately constrained to musical 16th-grid decisions such as anticipation and delayed resolution; every note is not randomly shifted.
- Realtime output and exported MIDI both respect the mode, while Swing remains an independent feel control.
- Existing presets remain backward-compatible and load with Humanize OFF unless the new flag is present.

### Tempo Feel Engine — 0.61.0
- Makes BPM a temporal context rather than a density penalty.
- Keeps musical density comparatively stable as tempo rises instead of heavily thinning melodies above 120-160 BPM.
- Increases the use of 1/16/offbeat positions at faster tempos so 170-220 BPM can produce genuinely fast-feeling melodies.
- Shortens ordinary lead note occupancy at fast tempos while preserving long-register pad behavior.
- The Musical Judge now evaluates note-rate, 1/16 usage and offbeat activity in addition to density/space, preventing MAGIC from re-ranking fast patterns back into slow-feeling loops.
- The system remains loop-centric and adds no new UI controls.

### Loop Forge — 0.60.0
- Adds a final integration pass after MAGIC, Musical Judge, Taste ML, Diversity Gate and Loop Transformations.
- Every transformed loop is re-checked after transformation and Smart Locks are restored.
- The Forge score combines Loop Quality, Context, Phrase Tension, Phrase Arc, Motif Memory, Groove, density health, rhythmic intent and layer presence.
- A deterministic cleanup keeps melody notes scale-safe, removes duplicates and normalizes final MIDI ranges.
- When final cleanup materially reduces the coherence score, the pre-cleanup loop is retained.
- Loop-centric only; no song/arrangement system is introduced.
### Judge Diversity Gate — 0.59.1
- The final eight variations now pass through a behavioral diversity gate after the Musical Judge.
- Diversity compares density, space, rhythm, motif behavior, leaps, register, surprise, context, loop quality, groove, memory, phrase arc and tension.
- Note-level similarity still matters, so two loops must differ in both their surface pattern and musical behavior to comfortably occupy separate slots.
- A hard diversity floor is attempted first; a controlled relaxation pass only activates when it would otherwise prevent the bank from reaching eight variations.
- Musical quality remains the primary ranking signal; the gate prevents near-clone variations without turning the bank into a collection of random outliers.
- Loop-centric only; no song/arrangement system is added.

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
### 0.75.0
- Composer Judge 2.0 adds a whole-composition coherence pass over existing MAGIC candidates.


The repository previously contained many small README files created for individual milestones. Their useful information is consolidated here; the source code and current version are the source of truth.

### 0.74.0
- Cadence & Loop Closure 2.0
- End-to-start seam planning, pickup/release behavior and controlled unresolved returns
- Loop Closure candidate judge and QA

### 0.73.0
- Motif Semantics 2.0
- Semantic A/A'/B/A'' development with component-level mutation
- Motif Semantics candidate judge and QA

### 0.72.0
- Creative Range Engine
- Coherent contour, interval, rhythm, repetition, register and harmony language planning

### 0.71.1
- Melodic Range Expansion

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