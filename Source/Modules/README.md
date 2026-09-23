# MIDI Forge Modules

Эта папка содержит модульную архитектуру для рефакторинга монолитного `PluginProcessor.cpp`.

## Структура модулей

### 1. MusicTheory.h
Музыкальная теория и утилиты:
- Определения гамм (Major, Minor, Dorian, etc.)
- Прогрессии аккордов (Pop, Dark, Emotional, etc.)
- Ритмические паттерны (Straight, Syncopated, Broken, Euclidean)
- Жанр-специфичные настройки по умолчанию

**Использование:**
```cpp
#include "Modules/MusicTheory.h"

auto semitones = music::getScaleSemitones(music::ScaleType::Minor);
auto degrees = music::getProgressionDegrees(music::ProgressionType::Pop);
bool hit = music::shouldRhythmHit(step, music::RhythmPattern::Syncopated);
auto defaults = music::getGenreDefaults(music::Genre::Trap);
```

### 2. MidiTypes.h
Типы данных MIDI и утилиты:
- `NoteEvent` - событие ноты с позицией, длиной, velocity
- `ArticulationInfo` - информация об артикуляции (slides, vibrato)
- `VisibleNote` - нота для отображения в UI
- `PendingNoteOff` - отложенные note-off для точного тайминга
- Утилиты: квантование, слияние дубликатов, проверка перекрытий

**Использование:**
```cpp
#include "Modules/MidiTypes.h"

midi::NoteEvent note{0, 4, 60, 100, 0};
midi::quantizeNotes(notes, 4); // Квантовать к 1/16
midi::removeDuplicateNotes(notes);
bool overlaps = midi::notesOverlap(note1, note2);
```

### 3. Generators.h
Генераторы музыки и параметры:
- `SoundProfile` - профиль инструмента (Pluck, Lead, Pad, 808, etc.)
- `MagicDNA` - параметры Magic DNA для когерентной генерации
- `VariationCandidate` - кандидат вариации с оценкой качества
- Метрики: density, velocity, leap, rhythm, motif strength

**Использование:**
```cpp
#include "Modules/Generators.h"

auto profile = generators::getSoundProfile(2); // Synth Lead
generators::MagicDNA dna;
dna.randomize(rng);
float quality = generators::calculateDensity(notes, bars);
```

### 4. PresetManager.h
Система пресетов:
- `PresetData` - структура всех параметров плагина
- `PresetManager` - сохранение/загрузка пресетов
- Factory presets: Trap Banger, Ambient Dreamscape, House Groove, Lofi Chill, Cinematic Epic
- Экспорт/импорт JSON

**Использование:**
```cpp
#include "Modules/PresetManager.h"

presets::PresetManager manager;
presets::PresetData preset;
preset.name = "My Preset";
preset.energy = 0.8f;
manager.savePreset(preset);

auto names = manager.listPresets();
auto loaded = manager.loadPreset("Trap Banger");
```

## План рефакторинга PluginProcessor.cpp

### Этап 1: Извлечение музыкальной логики
- [ ] Переместить `scaleSemitones()` → `music::getScaleSemitones()`
- [ ] Переместить `progressionDegrees()` → `music::getProgressionDegrees()`
- [ ] Переместить `degreeToPitch()` → `music::degreeToPitch()`
- [ ] Переместить `rhythmHit()` → `music::shouldRhythmHit()`
- [ ] Переместить `soundProfileFor()` → `generators::getSoundProfile()`

### Этап 2: Извлечение генераторов партий
- [ ] Выделить `addChords()` в отдельный класс `ChordGenerator`
- [ ] Выделить `addBass()` в отдельный класс `BassGenerator`
- [ ] Выделить `addMelody()` в отдельный класс `MelodyGenerator`
- [ ] Выделить `addArp()` в отдельный класс `ArpGenerator`
- [ ] Выделить `addDrums()` в отдельный класс `DrumGenerator`

### Этап 3: Система пресетов
- [ ] Интегрировать `PresetManager` в `PluginProcessor`
- [ ] Добавить UI для управления пресетами
- [ ] Реализовать hotkey для быстрого сохранения/загрузки

### Этап 4: Тестирование
- [ ] Unit tests для `MusicTheory`
- [ ] Unit tests для `Generators` метрик
- [ ] Integration tests для генерации партий
- [ ] Benchmark производительности

## Следующие модули (TODO)

### DrumPatterns.h
- Генерация drum patterns по жанрам
- Humanization для барабанов
- Ghost notes, swing, velocity variation

### ChordVoicing.h
- Продвинутые воинги аккордов
- Inversions, extensions, alterations
- Voice leading между аккордами

### MelodicMotifs.h
- Библиотека мелодических мотивов
- Трансформации: inversion, retrograde, augmentation
- Pattern matching для variations

### AudioAnalysis.h
- Анализ входящего MIDI
- Detection темпа, тональности, гармонии
- Smart quantize и correction

## Интеграция в PluginProcessor.h

Заменить текущие объявления функций на использование модулей:

```cpp
// Было:
std::vector<int> scaleSemitones() const;
std::vector<int> progressionDegrees() const;
int degreeToPitch(int degree, int baseOctave) const;
bool rhythmHit(int stepInBar) const;

// Стало:
inline std::vector<int> scaleSemitones() const {
    return music::getScaleSemitones(static_cast<music::ScaleType>(scale));
}
inline std::vector<int> progressionDegrees() const {
    return music::getProgressionDegrees(static_cast<music::ProgressionType>(progression));
}
inline int degreeToPitch(int degree, int baseOctave) const {
    return music::degreeToPitch(degree, rootPc, 
        static_cast<music::ScaleType>(scale), baseOctave);
}
inline bool rhythmHit(int stepInBar) const {
    return music::shouldRhythmHit(stepInBar, 
        static_cast<music::RhythmPattern>(rhythm));
}
```

## Преимущества модуляризации

1. **Разделение ответственности** - каждый модуль отвечает за свою область
2. **Тестируемость** - можно писать unit tests для каждого модуля отдельно
3. **Повторное использование** - модули можно использовать в других проектах
4. **Читаемость** - код становится понятнее и проще для навигации
5. **Расширяемость** - легко добавлять новые функции без изменения ядра
