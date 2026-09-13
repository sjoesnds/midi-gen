#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
MidiForgeAudioProcessor::MidiForgeAudioProcessor()
: AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    preferencesFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("MidiForge").getChildFile ("taste.json");
    loadPreferences();
    regenerate();
}
bool MidiForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void MidiForgeAudioProcessor::prepareToPlay(double sr, int)
{
sampleRate = sr; lastGlobalStep.store (-1);
samplePosition = 0;
pendingOffs.clear();
}
void MidiForgeAudioProcessor::setRoot(int v){rootPc=juce::jlimit(0,11,v);regenerate();}
void MidiForgeAudioProcessor::setGenre(int v){genre=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setScale(int v){scale=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setProgression(int v){progression=juce::jlimit(0,6,v);regenerate();}
void MidiForgeAudioProcessor::setRhythm(int v){rhythm=juce::jlimit(0,3,v);regenerate();}
void MidiForgeAudioProcessor::setBars(int v){bars=juce::jlimit(1,16,v);regenerate();}
void MidiForgeAudioProcessor::setSeed(int v){seed=v;regenerate();}
void MidiForgeAudioProcessor::setOctave(int v){octave=juce::jlimit(2,6,v);regenerate();}
void MidiForgeAudioProcessor::setSectionMode(int v){sectionMode=juce::jlimit(0,2,v);regenerate();}
void MidiForgeAudioProcessor::setChordDensity(float v){chordDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setBassDensity(float v){bassDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setMelodyDensity(float v){melodyDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setArpDensity(float v){arpDensity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setSwing(float v){swing=juce::jlimit(0.f,.75f,v);}
void MidiForgeAudioProcessor::setHumanize(float v){humanize=juce::jlimit(0.f,1.f,v);}
void MidiForgeAudioProcessor::setComplexity(float v){complexity=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setMelodyLength(float v){melodyLength=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setPauseChance(float v){pauseChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setLeapChance(float v){leapChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setGhostChance(float v){ghostChance=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setArpRate(int v){arpRate=juce::jlimit(1,8,v);regenerate();}
void MidiForgeAudioProcessor::setVoicingWidth(float v){voicingWidth=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setChordExtensions(bool v){chordExtensions=v;regenerate();}
void MidiForgeAudioProcessor::setInversions(bool v){inversions=v;regenerate();}
void MidiForgeAudioProcessor::setMotifStrength(float v){motifStrength=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setVariationAmount(float v){variationAmount=juce::jlimit(0.f,1.f,v);regenerateVariations();}
void MidiForgeAudioProcessor::setFillAmount(float v){fillAmount=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setEnergy(float v){energy=juce::jlimit(0.f,1.f,v);regenerate();}
void MidiForgeAudioProcessor::setChordsEnabled(bool v){chordsEnabled=v;}
void MidiForgeAudioProcessor::setBassEnabled(bool v){bassEnabled=v;}
void MidiForgeAudioProcessor::setMelodyEnabled(bool v){melodyEnabled=v;}
void MidiForgeAudioProcessor::setArpEnabled(bool v){arpEnabled=v;}
void MidiForgeAudioProcessor::setHookMode(bool v){hookMode=v;regenerate();}
std::vector<int> MidiForgeAudioProcessor::scaleSemitones() const
{
switch(scale){
case Major:return{0,2,4,5,7,9,11};
case Minor:return{0,2,3,5,7,8,10};
case Dorian:return{0,2,3,5,7,9,10};
case Phrygian:return{0,1,3,5,7,8,10};
case HarmonicMinor:return{0,2,3,5,7,8,11};
case MelodicMinor:return{0,2,3,5,7,9,11};
default:return{0,2,4,7,9};
}
}
std::vector<int> MidiForgeAudioProcessor::progressionDegrees() const
{
if(progression==Pop)return{0,4,5,3};
if(progression==Dark)return{0,5,2,6};
if(progression==Emotional)return{5,3,0,4};
if(progression==CinematicProg)return{0,3,4,5};
if(progression==JazzLike)return{1,4,0,3};
if(progression==Looping)return{0,5,3,4};
switch(genre){
case Trap:return{0,5,2,6};
case House:return{0,4,5,3};
case Techno:return{0,5,3,4};
case BoomBap:return{0,5,3,4};
case Ambient:return{0,3,5,4};
case Cinematic:return{0,3,4,5};
default:return{0,5,3,4};
}
}
int MidiForgeAudioProcessor::degreeToPitch(int degree,int baseOctave) const
{
const auto s=scaleSemitones(); int count=(int)s.size();
if(!count)return 60;
int oct=degree/count, idx=degree%count;
if(idx<0){idx+=count;--oct;}
return 12*(baseOctave+oct)+rootPc+s[(size_t)idx];
}
int MidiForgeAudioProcessor::snapToScale(int midi) const
{
int best=midi,bestDist=999;
for(int o=1;o<=9;++o)for(int p:scaleSemitones()){
int n=12*o+rootPc+p,d=std::abs(n-midi);
if(d<bestDist){bestDist=d;best=n;}
}
return juce::jlimit(0,127,best);
}
bool MidiForgeAudioProcessor::rhythmHit(int x) const
{
x=((x%16)+16)%16;
if(rhythm==Straight)return true;
if(rhythm==Syncopated)return(x%4==0)||(x%4==3)||(x==6)||(x==14);
if(rhythm==Broken)return(x%8==0)||x==3||x==6||x==10||x==13;
return((x*7)%16)<7;
}
// --- Learning -----------------------------------------------------------
void MidiForgeAudioProcessor::sampleVariationFeatures(int vi,float& d,float& e,float& c) const
{
const juce::ScopedLock sl(variationsLock);
if(vi<0||vi>=(int)variations.size()){d=e=c=0.5f;return;}
const auto& notes=variations[(size_t)vi].notes;
const int barsN=juce::jmax(1,variations[(size_t)vi].bars);
int mel=0,velSum=0,velN=0,prevNote=-1,intSum=0,intN=0;
for(const auto& n:notes){
velSum+=n.velocity; ++velN;
if(n.channel==3){
++mel;
if(prevNote>=0){ intSum+=std::abs(n.note-prevNote); ++intN; }
prevNote=n.note;
}
}
d=juce::jlimit(0.f,1.f,(float)mel/(float)(barsN*8));
e=juce::jlimit(0.f,1.f,velN>0?(float)velSum/(float)velN/127.f:0.5f);
c=juce::jlimit(0.f,1.f,intN>0?(float)intSum/(float)intN/12.f:0.5f);
}
void MidiForgeAudioProcessor::likeVariation(int vi)
{
if(vi<0||vi>7)return;
likeCounts[(size_t)vi]++;
float d,e,c; sampleVariationFeatures(vi,d,e,c);
likedD=(likedD*likedN+d)/(likedN+1);
likedE=(likedE*likedN+e)/(likedN+1);
likedC=(likedC*likedN+c)/(likedN+1);
likedN++;
savePreferences();
applyLearnedWeights();
}
void MidiForgeAudioProcessor::dislikeVariation(int vi)
{
if(vi<0||vi>7)return;
dislikeCounts[(size_t)vi]++;
float d,e,c; sampleVariationFeatures(vi,d,e,c);
disD=(disD*disN+d)/(disN+1);
disE=(disE*disN+e)/(disN+1);
disC=(disC*disN+c)/(disN+1);
disN++;
savePreferences();
applyLearnedWeights();
}
int MidiForgeAudioProcessor::getLikeCount(int vi) const { return (vi>=0&&vi<8)?likeCounts[(size_t)vi]:0; }
int MidiForgeAudioProcessor::getDislikeCount(int vi) const { return (vi>=0&&vi<8)?dislikeCounts[(size_t)vi]:0; }
int MidiForgeAudioProcessor::getVariationScore(int vi) const { return getLikeCount(vi)-getDislikeCount(vi); }
void MidiForgeAudioProcessor::applyLearnedWeights()
{
// Направление = что вкусу нравится против того, что он отвергает.
float dirD=0,dirE=0,dirC=0; int w=0;
if(likedN>0&&disN>0){ dirD=likedD-disD; dirE=likedE-disE; dirC=likedC-disC; w=juce::jmin(likedN,disN); }
else if(likedN>0){ dirD=likedD-melodyDensity; dirE=likedE-energy; dirC=likedC-complexity; w=likedN; }
else if(disN>0){ dirD=melodyDensity-disD; dirE=energy-disE; dirC=complexity-disC; w=disN; }
if(w<=0)return;
const float g=0.15f*juce::jlimit(0.f,1.f,(float)w/6.f);
melodyDensity=juce::jlimit(0.f,1.f,melodyDensity+dirD*g);
energy=juce::jlimit(0.f,1.f,energy+dirE*g);
complexity=juce::jlimit(0.f,1.f,complexity+dirC*g);
}
void MidiForgeAudioProcessor::savePreferences()
{
juce::DynamicObject* o=new juce::DynamicObject();
o->setProperty("likedN",likedN); o->setProperty("likedD",(double)likedD);
o->setProperty("likedE",(double)likedE); o->setProperty("likedC",(double)likedC);
o->setProperty("disN",disN); o->setProperty("disD",(double)disD);
o->setProperty("disE",(double)disE); o->setProperty("disC",(double)disC);
juce::Array<juce::var> lk,dk;
for(int i=0;i<8;++i){ lk.add(likeCounts[(size_t)i]); dk.add(dislikeCounts[(size_t)i]); }
o->setProperty("likes",lk); o->setProperty("dislikes",dk);
preferencesFile.getParentDirectory().createDirectory();
preferencesFile.replaceWithText(juce::JSON::toString(juce::var(o)));
}
void MidiForgeAudioProcessor::loadPreferences()
{
if(!preferencesFile.existsAsFile())return;
juce::var v=juce::JSON::parse(preferencesFile.loadFileAsString());
if(auto* o=v.getDynamicObject()){
likedN=(int)o->getProperty("likedN"); disN=(int)o->getProperty("disN");
likedD=(float)o->getProperty("likedD"); likedE=(float)o->getProperty("likedE"); likedC=(float)o->getProperty("likedC");
disD=(float)o->getProperty("disD"); disE=(float)o->getProperty("disE"); disC=(float)o->getProperty("disC");
if(auto* la=o->getProperty("likes").getArray())
for(int i=0;i<8&&i<la->size();++i) likeCounts[(size_t)i]=(int)(*la)[i];
if(auto* da=o->getProperty("dislikes").getArray())
for(int i=0;i<8&&i<da->size();++i) dislikeCounts[(size_t)i]=(int)(*da)[i];
}
}
// --- Motif / Phrase Engine methods ----------------------------------------
MidiForgeAudioProcessor::Motif MidiForgeAudioProcessor::generateMotif(juce::Random& random, int baseNote, int scaleDegree)
{
    Motif motif;
    motif.baseNote = baseNote;
    
    // Создаём короткий мотив из 3-8 нот для БОЛЬШОГО разнообразия
    const int motifLength = 3 + random.nextInt(6); // 3-8 нот
    motif.length = motifLength;
    
    // Значительно больше ритмических паттернов (32 вместо 8)
    std::vector<std::vector<int>> rhythmPatterns = {
        {0, 4, 8, 12},           // Четверти
        {0, 6, 12},              // Дактиль
        {0, 3, 7, 12},           // Синкопа
        {0, 2, 5, 8, 11, 14},    // Шестнадцатые с пропусками
        {0, 8},                  // Половинные
        {0, 5, 10, 15},          // Триоли
        {0, 2, 8, 10},           // Двойная синкопа
        {0, 4, 6, 10, 14},       // Сложный паттерн
        {0, 3, 6, 9, 12},        // Пунктирные
        {0, 1, 4, 7, 10, 13},    // Быстрые пробежки
        {0, 4, 7, 11},           // Swing
        {0, 2, 4, 6, 8, 10, 12, 14}, // Все восьмые
        {0, 8, 12},              // Long-short
        {0, 2, 3, 8, 9, 10},     // Burst
        {0, 6, 8, 14},           // Off-beat
        {0, 1, 2, 8, 9, 10},     // Double burst
        {0, 4, 5, 6, 12},        // Triplet feel
        {0, 3, 4, 5, 11, 12},    // Syncopated burst
        {0, 2, 6, 8, 12, 14},    // Walking
        {0, 1, 8, 9},            // Call response pairs
        {0, 4, 10, 12},          // Trapezoid
        {0, 3, 8, 11},           // Diamond
        {0, 2, 4, 9, 11, 13},    // Alternating
        {0, 5, 6, 7, 14, 15},    // End-heavy
        {0, 1, 2, 3, 8},         // Start-heavy
        {0, 4, 8, 9, 10, 11},    // Back-loaded
        {0, 2, 5, 7, 10, 13},    // Euclidean-ish
        {0, 3, 5, 8, 10, 13},    // Minor swing
        {0, 1, 5, 6, 10, 11},    // Chromatic approach
        {0, 4, 6, 8, 14},        // Open feel
        {0, 2, 7, 9, 14},        // Pentatonic rhythm
        {0, 3, 6, 10, 13}        // Jazz-like
    };
    
    // Выбираем случайный ритмический паттерн
    int patternIdx = random.nextInt((int)rhythmPatterns.size());
    std::vector<int> availableSteps = rhythmPatterns[(size_t)patternIdx];
    
    // Для Trap/House добавляем характерные паттерны
    if (genre == Trap || genre == BoomBap)
    {
        std::vector<std::vector<int>> trapPatterns = {
            {0, 3, 8, 12}, {0, 4, 9, 14}, {0, 2, 6, 10, 14},
            {0, 3, 6, 10, 13}, {0, 2, 8, 11}, {0, 4, 7, 11, 14}
        };
        if (random.nextBool())
            availableSteps = trapPatterns[(size_t)random.nextInt((int)trapPatterns.size())];
    }
    else if (genre == House || genre == Techno)
    {
        std::vector<std::vector<int>> housePatterns = {
            {0, 4, 8, 12}, {0, 2, 6, 10, 14}, {0, 8},
            {0, 3, 6, 9, 12}, {0, 1, 4, 7, 10, 13}, {0, 2, 5, 8, 11}
        };
        if (random.nextBool())
            availableSteps = housePatterns[(size_t)random.nextInt((int)housePatterns.size())];
    }
    else if (genre == Ambient)
    {
        std::vector<std::vector<int>> ambientPatterns = {
            {0, 8}, {0, 6, 12}, {0, 4, 10}, {0, 8, 12}, {0, 5, 10, 15}
        };
        if (random.nextBool())
            availableSteps = ambientPatterns[(size_t)random.nextInt((int)ambientPatterns.size())];
    }
    
    // Выбираем шаги для нот мотива (не все, а с вероятностью)
    for (int step : availableSteps)
    {
        if (motif.steps.size() < (size_t)motifLength && 
            (motif.steps.empty() || random.nextFloat() > 0.25f)) // Увеличили шанс добавления
        {
            motif.steps.push_back(step);
        }
    }
    
    // Если нот слишком мало, добавляем ещё
    while (motif.steps.size() < (size_t)juce::jmax(3, motifLength - 1))
    {
        int newStep = random.nextInt(16);
        if (std::find(motif.steps.begin(), motif.steps.end(), newStep) == motif.steps.end())
            motif.steps.push_back(newStep);
    }
    
    std::sort(motif.steps.begin(), motif.steps.end());
    
    // Генерируем интервалы мотива (в полутонах) с ОГРОМНЫМ разнообразием
    const auto scaleIntervals = scaleSemitones();
    const int scaleSize = (int)scaleIntervals.size();
    
    // Первый интервал - базовая нота (0)
    motif.intervals.push_back(0);
    
    // Разнообразные стратегии выбора интервалов (7 вместо 4)
    int intervalStrategy = random.nextInt(7);
    
    // Иногда смешиваем две стратегии для уникальности
    int secondaryStrategy = -1;
    if (random.nextFloat() < 0.3f && complexity > 0.3f)
        secondaryStrategy = random.nextInt(7);
    
    for (size_t i = 1; i < motif.steps.size(); ++i)
    {
        int degree = 0;
        int currentStrategy = (secondaryStrategy >= 0 && random.nextFloat() < 0.4f) 
                              ? secondaryStrategy 
                              : intervalStrategy;
        
        switch (currentStrategy)
        {
            case 0: // Консонансная мелодия (терции, квинты, октавы)
                degree = random.nextInt(4) * 2; // 0, 2, 4, 6
                break;
                
            case 1: // Шаговая мелодия (преимущественно секунды)
                degree = (int)i % scaleSize;
                if (random.nextFloat() < 0.3f)
                    degree = (degree + random.nextInt(2)) % scaleSize;
                break;
                
            case 2: // Скачкообразная мелодия (квинты, сексты, октавы)
                degree = 4 + random.nextInt(4); // 4-7 степени
                break;
                
            case 3: // Хроматическая/джазовая (с проходами)
                degree = random.nextInt(scaleSize);
                if (random.nextFloat() < complexity * 0.5f)
                    degree = (degree + random.nextInt(5) - 2 + scaleSize) % scaleSize;
                break;
                
            case 4: // Пентатоника/блюз (акценты на характерные интервалы)
                degree = random.nextInt(5) * 2; // 0, 2, 4, 6, 8
                if (random.nextFloat() < 0.2f)
                    degree += 1; // Добавляем блюзовую ноту
                break;
                
            case 5: // Восходящая/нисходящая дуга
            {
                float arcPos = (float)i / (float)motif.steps.size();
                if (arcPos < 0.5f)
                    degree = (int)(arcPos * 2.0f * 8); // Восхождение
                else
                    degree = (int)((1.0f - arcPos) * 2.0f * 8); // Нисхождение
                degree = juce::jlimit(0, 10, degree);
                break;
            }
                
            case 6: // Рандомная с акцентом на крайние значения
                if (random.nextFloat() < 0.3f)
                    degree = random.nextBool() ? 0 : 10; // Крайние значения
                else
                    degree = random.nextInt(8);
                break;
        }
        
        // Добавляем октавные смещения для разнообразия
        int octaveShift = 0;
        if (random.nextFloat() < 0.3f) // Увеличили шанс
            octaveShift = random.nextBool() ? 1 : -1;
        
        const int semitone = scaleIntervals[(size_t)(degree % scaleSize)];
        const int finalInterval = semitone + (degree / scaleSize + octaveShift) * 12;
        motif.intervals.push_back(juce::jlimit(-36, 36, finalInterval)); // Расширили диапазон
    }
    
    // Сохраняем характерный ритмический паттерн
    motif.rhythmPattern = (float)random.nextInt(100) / 100.0f;
    
    return motif;
}

void MidiForgeAudioProcessor::developMotif(Motif& motif, float variationAmt, juce::Random& random)
{
    // Развиваем мотив: варьируем ритм и интервалы
    const float variation = juce::jlimit(0.0f, 1.0f, variationAmt);
    
    // Вариация ритма
    for (size_t i = 0; i < motif.steps.size(); ++i)
    {
        if (random.nextFloat() < variation * 0.5f)
        {
            // Сдвигаем шаг на ±1 или ±2
            const int shift = (random.nextInt(5) - 2); // -2..+2
            motif.steps[i] = juce::jlimit(0, 15, motif.steps[i] + shift);
        }
    }
    
    // Вариация интервалов
    for (size_t i = 1; i < motif.intervals.size(); ++i)
    {
        if (random.nextFloat() < variation * 0.4f)
        {
            // Изменяем интервал на ±1-2 полутона
            const int shift = (random.nextInt(5) - 2);
            motif.intervals[i] += shift;
        }
    }
    
    // Иногда добавляем или убираем ноту
    if (motif.steps.size() > 2 && random.nextFloat() < variation * 0.3f)
    {
        const size_t removeIdx = (size_t)random.nextInt((int)motif.steps.size());
        motif.steps.erase(motif.steps.begin() + (int)removeIdx);
        if (removeIdx < motif.intervals.size())
            motif.intervals.erase(motif.intervals.begin() + (int)removeIdx);
    }
}

MidiForgeAudioProcessor::PhraseState::Phase MidiForgeAudioProcessor::getPhrasePhase(int barInPhrase, int phraseLength)
{
    if (barInPhrase == 0)
        return PhraseState::PhraseStart;
    else if (barInPhrase < phraseLength / 2)
        return PhraseState::Development;
    else if (barInPhrase < phraseLength - 1)
        return PhraseState::Tension;
    else
        return PhraseState::Resolution;
}

// --- Motif Helper Methods -------------------------------------------------
MidiForgeAudioProcessor::Motif MidiForgeAudioProcessor::generateMotifVariation(
    const Motif& baseMotif, PhraseState::Phase phase, float variationAmt, juce::Random& random)
{
    Motif developedMotif = baseMotif;
    
    switch (phase)
    {
        case PhraseState::Development:
            // Лёгкая вариация
            developMotif(developedMotif, variationAmt * 0.3f, random);
            break;
        case PhraseState::Tension:
            // Более сильная вариация, добавляем напряжение
            developMotif(developedMotif, variationAmt * 0.6f, random);
            break;
        case PhraseState::Resolution:
            // Возврат к оригиналу с лёгкой вариацией
            developMotif(developedMotif, variationAmt * 0.2f, random);
            break;
        default:
            break;
    }
    
    return developedMotif;
}

void MidiForgeAudioProcessor::applyMotifToMelody(Section& s, int barOffset, const Motif& motif, 
                                                  float strength, juce::Random& random, int baseNote)
{
    const int barStart = barOffset * 16;
    
    // Сначала удаляем старые ноты мелодии в этом такте (channel 3), чтобы не было наложения
    s.notes.erase(
        std::remove_if(s.notes.begin(), s.notes.end(),
            [barStart](const NoteEvent& n) {
                return n.channel == 3 && n.step >= barStart && n.step < barStart + 16;
            }),
        s.notes.end()
    );
    
    // Для большей мелодичности иногда сдвигаем весь мотив на октаву вверх или вниз
    int octaveShift = 0;
    if (random.nextFloat() < 0.4f)
        octaveShift = random.nextBool() ? 12 : -12;
    
    // Применяем мотив с заданной силой (strength)
    for (size_t i = 0; i < motif.steps.size() && i < motif.intervals.size(); ++i)
    {
        // Сила определяет вероятность применения ноты мотива
        if (random.nextFloat() > strength)
            continue;
        
        const int step = motif.steps[i];
        
        // Добавляем вариативность: иногда меняем интервал на +/- 1-2 полутона для хроматизма
        int interval = motif.intervals[i];
        if (random.nextFloat() < 0.25f && complexity > 0.4f)
        {
            interval += (random.nextInt(5) - 2); // -2..+2 полутона
        }
        
        // Базовая нота + интервал + октавный сдвиг
        // Делаем мелодию более независимой от аккорда, добавляя случайный сдвиг
        int noteBase = baseNote;
        if (random.nextFloat() < 0.3f)
        {
            // 30% шанс взять ноту не от аккорда, а из гаммы независимо
            const auto scaleIntervals = scaleSemitones();
            int scaleIdx = random.nextInt((int)scaleIntervals.size());
            noteBase = degreeToPitch(1, octave) + scaleIntervals[(size_t)scaleIdx]; // Берём от тоники + степень гаммы
        }
        
        const int note = juce::jlimit(24, 108, noteBase + interval + octaveShift);
        
        // Humanization velocity: первая нота мотива громче, остальные тише
        int velocity = 75 + random.nextInt(25);
        if (i == 0) velocity += 20; // Акцент на первую ноту
        
        // Длина ноты: варьируется для ритмичности
        int length = 2 + random.nextInt(4);
        if (i == motif.steps.size() - 1 && random.nextFloat() < 0.5f)
            length = 6 + random.nextInt(4); // Последняя нота может быть длиннее
        
        s.notes.push_back({barStart + step, length, note, velocity, 3, false});
    }
}

void MidiForgeAudioProcessor::applyCallAndResponse(Section& s, int barOffset, const Motif& motif,
                                                    float strength, juce::Random& random, int baseNote)
{
    // Call & Response: нечётные такты отвечают на терцию ниже
    Motif responseMotif = motif;
    for (size_t i = 0; i < responseMotif.intervals.size(); ++i)
        responseMotif.intervals[i] -= 3; // Терция ниже
    
    applyMotifToMelody(s, barOffset, responseMotif, strength * 0.7f, random, baseNote);
}

// --- Humanization Engine methods ------------------------------------------

MidiForgeAudioProcessor::VelocityProfile::Shape 
MidiForgeAudioProcessor::chooseVelocityShapeForPhrase(PhraseState::Phase phase, juce::Random& random)
{
    switch (phase)
    {
        case PhraseState::PhraseStart:
            // Начало фразы: сильный акцент, затем спад
            return VelocityProfile::Decrescendo;
        case PhraseState::Development:
            // Развитие: нарастание
            return VelocityProfile::Crescendo;
        case PhraseState::Tension:
            // Напряжение: арка (сильные края, слабая середина)
            return VelocityProfile::Arch;
        case PhraseState::Resolution:
            // Разрешение: мягкое завершение
            return VelocityProfile::Decrescendo;
        default:
            return VelocityProfile::Flat;
    }
}

MidiForgeAudioProcessor::RhythmVariation 
MidiForgeAudioProcessor::generateRhythmVariation(float humanizeAmount, bool isAccent, juce::Random& random)
{
    RhythmVariation variation;
    
    // Микросдвиги времени (человеческая неточность)
    const float timingRange = isAccent ? 0.15f : 0.35f;  // акценты более точные
    variation.microTiming = (random.nextFloat() * 2.0f - 1.0f) * timingRange * humanizeAmount;
    
    // Небольшие паузы вместо некоторых нот
    if (!isAccent && random.nextFloat() < 0.08f * humanizeAmount)
        variation.isPause = true;
    
    // Удлинение/укорочение нот
    const float lengthVar = (random.nextFloat() * 2.0f - 1.0f) * 0.25f * humanizeAmount;
    variation.lengthMultiplier = 1.0f + lengthVar;
    
    return variation;
}

int MidiForgeAudioProcessor::constrainInterval(int prevNote, int nextNote, 
                                                const IntervalConstraint& constraint, 
                                                juce::Random& random)
{
    const int interval = std::abs(nextNote - prevNote);
    
    // Проверяем, является ли интервал большим скачком
    const bool isLeap = interval > constraint.maxStepSize;
    
    // Если это скачок, с некоторой вероятностью возвращаемся к более близкой ноте
    if (isLeap && random.nextFloat() < constraint.returnAfterLeap)
    {
        // Возвращаемся в диапазоне шага от предыдущей ноты
        const int direction = (nextNote > prevNote) ? -1 : 1;
        const int maxStep = (int)constraint.maxStepSize;
        const int stepSize = 1 + random.nextInt(maxStep);
        return prevNote + direction * stepSize;
    }
    
    // Для обычных нот ограничиваем размер шага
    if (!isLeap && interval > constraint.maxStepSize)
    {
        const int direction = (nextNote > prevNote) ? 1 : -1;
        const int maxStep = (int)constraint.maxStepSize;
        return prevNote + direction * (1 + random.nextInt(maxStep));
    }
    
    return nextNote;
}

void MidiForgeAudioProcessor::applyHumanization(Section& s, int startStep, int endStep, 
                                                 float humanizeAmount, juce::Random& random,
                                                 const VelocityProfile::Shape& velocityShape)
{
    const int totalNotes = endStep - startStep;
    if (totalNotes <= 0 || humanizeAmount <= 0.0f)
        return;
    
    int noteIndex = 0;
    for (auto& note : s.notes)
    {
        if (note.step < startStep || note.step >= endStep)
            continue;
        
        // Определяем позицию во фразе (0..1)
        const float phrasePosition = (float)(note.step - startStep) / (float)totalNotes;
        
        // Применяем профиль velocity
        float velocityModifier = 1.0f;
        switch (velocityShape)
        {
            case VelocityProfile::Crescendo:
                velocityModifier = 0.7f + 0.6f * phrasePosition;
                break;
            case VelocityProfile::Decrescendo:
                velocityModifier = 1.3f - 0.6f * phrasePosition;
                break;
            case VelocityProfile::Arch:
                velocityModifier = 0.7f + 1.2f * (1.0f - std::abs(2.0f * phrasePosition - 1.0f));
                break;
            case VelocityProfile::InvertedArch:
                velocityModifier = 1.3f - 1.2f * (1.0f - std::abs(2.0f * phrasePosition - 1.0f));
                break;
            default:
                velocityModifier = 1.0f;
                break;
        }
        
        // Акценты на сильных долях
        const bool isAccent = (note.step % 4 == 0);
        if (isAccent)
            velocityModifier *= (1.0f + 0.3f * humanizeAmount);
        
        // Применяем modifier к velocity
        note.velocity = juce::jlimit(20, 127, 
            (int)((float)note.velocity * velocityModifier * (0.8f + 0.4f * humanizeAmount)));
        
        // Генерируем ритмическую вариацию
        const auto rhythmVar = generateRhythmVariation(humanizeAmount, isAccent, random);
        
        // Применяем микросдвиги к позиции ноты (эмуляция через изменение длины)
        if (rhythmVar.isPause)
        {
            note.length = 0;  // Пауза
        }
        else
        {
            note.length = juce::jmax(1, (int)((float)note.length * rhythmVar.lengthMultiplier));
        }
        
        ++noteIndex;
    }
}

// --- Generation ---------------------------------------------------------
void MidiForgeAudioProcessor::addChords(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
std::vector<int> chordDegrees={degree,degree+2,degree+4};
if(chordExtensions && (complexity>0.45f || r.nextFloat()<0.4f)) chordDegrees.push_back(degree+6);
if(chordExtensions && complexity>0.75f && r.nextFloat()<0.45f) chordDegrees.push_back(degree+8);
int root=degreeToPitch(degree,octave-1);
int transpose=r.nextFloat()<voicingWidth*.25f?(r.nextBool()?12:-12):0;
for(int i=0;i<(int)chordDegrees.size();++i){
if(r.nextFloat()>juce::jlimit(0.f,1.f,chordDensity*(0.65f+0.35f*e)))continue;
int note=juce::jlimit(24,108,degreeToPitch(chordDegrees[(size_t)i],octave-1)+transpose);
if(inversions && barOffset>0 && r.nextFloat()<0.45f){
note=juce::jlimit(24,108,note+(r.nextBool()?12:-12));
}
s.notes.push_back({barOffset*16,16,note,72-i*4,1,false});
}
if((genre==House||genre==Techno) && r.nextFloat()<e)
s.notes.push_back({barOffset*16+8,4,juce::jlimit(24,108,root+12),63,1,false});
// FLAGSHIP: add9 sparkle сверху для современного колорита.
if(chordExtensions && r.nextFloat()<0.18f)
s.notes.push_back({barOffset*16+8,8,juce::jlimit(24,108,degreeToPitch(degree+8,octave)),60,1,false});
}
void MidiForgeAudioProcessor::addBass(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
int root=juce::jlimit(18,55,degreeToPitch(degree,octave-2));
std::vector<int> steps;
if(genre==Trap)steps={0,3,6,10,14};
else if(genre==House||genre==Techno)steps={0,4,8,12};
else steps={0,8,12};
for(int x:steps){
if(!rhythmHit(x)||r.nextFloat()>bassDensity*e)continue;
int note=root;
if(complexity>0.5f&&r.nextFloat()<0.22f)note+=r.nextBool()?7:-5;
note=juce::jlimit(18,60,note);
bool ghost=r.nextFloat()<ghostChance*.5f&&x!=0;
int len=(genre==House||genre==Techno)?3:(x==0?7:3);
int vel=juce::jlimit(1,127,92+(x==0?7:0)-(ghost?20:0));
s.notes.push_back({barOffset*16+x,len,note,vel,2,ghost});
}
// FLAGSHIP: sub drop октавой вниз на сильную долю для веса (trap/cinematic).
if((genre==Trap||genre==Cinematic) && e>0.5f && r.nextFloat()<0.35f)
s.notes.push_back({barOffset*16,8,juce::jlimit(18,60,root-12),100,2,false});
}
void MidiForgeAudioProcessor::addMelody(
Section& s, int barOffset, float e, juce::Random& r,
const std::vector<NoteEvent>* inherited, int variationSalt)
{
// --- PHASE 3: Humanization Engine Helpers ---
auto calculateHumanVelocity = [&](int noteIndex, int totalNotes, bool isAccent, bool isPhraseEnd, float baseVel = 78.0f) -> int
{
    float positionFactor = 1.0f;
    float normalizedPos = static_cast<float>(noteIndex) / std::max(1, totalNotes - 1);
    
    if (normalizedPos < 0.15f) positionFactor = 1.18f;
    else if (normalizedPos > 0.85f || isPhraseEnd) positionFactor = 0.82f;

    float velocity = baseVel * positionFactor;
    if (isAccent) velocity *= 1.25f;
    velocity += static_cast<float>((r.nextInt() % 12) - 6);

    return juce::jlimit(40, 120, static_cast<int>(velocity));
};

auto calculateMicroTiming = [&](int stepInBar, bool isDownbeat) -> float
{
    if (isDownbeat && r.nextFloat() < 0.35f) return -0.025f;
    if (!isDownbeat && r.nextFloat() < 0.45f) return 0.035f;
    return 0.0f;
};

// === НОВАЯ ЛОГИКА ГЕНЕРАЦИИ МЕЛОДИИ ===
// Один мотив на фразу (4 такта), развивается внутри фразы

const int phraseLength = 4;
const int barInPhrase = barOffset % phraseLength;
const PhraseState::Phase phase = getPhrasePhase(barInPhrase, phraseLength);

// Базовая нота от текущей степени прогрессии
const auto prog = progressionDegrees();
const int degree = prog[(size_t)(barOffset % (int)prog.size())];
const int baseNote = degreeToPitch(degree, octave);

// Seed для всей фразы (не для каждого такта!)
const int phraseSeed = seed + variationSalt + (barOffset / phraseLength) * 137;
juce::Random phraseRandom(phraseSeed);

// Генерируем ОДИН мотив в начале фразы
static Motif phraseMotif;
static int lastPhraseStart = -1;
int currentPhraseStart = (barOffset / phraseLength) * phraseLength;

if (currentPhraseStart != lastPhraseStart)
{
    // Новая фраза: создаём уникальный мотив
    phraseMotif = generateMotif(phraseRandom, baseNote, degree);
    lastPhraseStart = currentPhraseStart;
}

// Развиваем мотив для текущего такта
Motif currentMotif;
if (barInPhrase == 0)
{
    // Такт 1: оригинальный мотив
    currentMotif = phraseMotif;
}
else
{
    // Такты 2-4: вариации мотива
    currentMotif = generateMotifVariation(phraseMotif, phase, variationAmount, phraseRandom);
}

// Применяем мотив
applyMotifToMelody(s, barOffset, currentMotif, motifStrength, phraseRandom, baseNote);

// Call & Response для тактов 2 и 4
if (hookMode && (barInPhrase == 1 || barInPhrase == 3))
{
    if (phraseRandom.nextFloat() < 0.7f)
        applyCallAndResponse(s, barOffset, currentMotif, motifStrength, phraseRandom, baseNote);
}

// Дополнительные проходящие ноты (только если density > 0.4)
if (melodyDensity > 0.4f)
{
    const auto scaleSteps = scaleSemitones();
    const int extraCount = (int)((melodyDensity - 0.4f) * 8.0f);
    
    for (int i = 0; i < extraCount; ++i)
    {
        int step = phraseRandom.nextInt(16);
        
        // Проверяем, не занята ли позиция
        bool occupied = false;
        for (const auto& n : s.notes)
        {
            if (n.channel == 3 && n.step / 16 == barOffset && std::abs((n.step % 16) - step) < 2)
            {
                occupied = true;
                break;
            }
        }
        if (occupied) continue;
        
        // Выбираем ноту из гаммы (step-wise motion приоритетно)
        int scaleIdx = phraseRandom.nextInt((int)scaleSteps.size());
        int octaveVar = (phraseRandom.nextInt(100) < 70) ? 0 : (phraseRandom.nextBool() ? 1 : -1);
        int pitch = baseNote + scaleSteps[(size_t)scaleIdx] + octaveVar * 12;
        
        // Длина: преимущественно короткие (1/16, 1/8)
        int len = (phraseRandom.nextInt(100) < 60) ? 1 : (phraseRandom.nextInt(100) < 80 ? 2 : 4);
        
        // Velocity с humanization
        bool accent = (step % 4 == 0);
        int vel = calculateHumanVelocity(i, extraCount, accent, false, 62.0f);
        
        NoteEvent passingNote;
        passingNote.step = barOffset * 16 + step;
        passingNote.length = len;
        passingNote.note = juce::jlimit(36, 108, pitch);
        passingNote.velocity = vel;
        passingNote.channel = 3;
        
        s.notes.push_back(passingNote);
    }
}
} // конец addMelody

void MidiForgeAudioProcessor::addArp(Section& s,int barOffset,int degree,float e,juce::Random& r)
{
if(arpDensity<=0.001f)return;
std::array<int,4> c={degreeToPitch(degree,octave),degreeToPitch(degree+2,octave),degreeToPitch(degree+4,octave),degreeToPitch(degree+6,octave)};
int stride=juce::jmax(1,8/juce::jmax(1,arpRate));
for(int x=0;x<16;x+=stride){
if(r.nextFloat()>arpDensity*(0.55f+0.65f*e))continue;
int idx=(x/stride+barOffset)%4;
if((barOffset/2)%2==1)idx=3-idx;
s.notes.push_back({barOffset*16+x,1,snapToScale(c[(size_t)idx]),
64+(x%4==0?8:0),4,false});
}
}
void MidiForgeAudioProcessor::buildSection(Section& section,int sectionIndex,
const std::vector<int>& prog,juce::Random& r,
const std::vector<NoteEvent>* inherited, int variationSalt)
{
const juce::String names[]={"INTRO","VERSE","PRE-CHORUS","CHORUS","BREAK","DROP","OUTRO"};
section.name=names[std::min(sectionIndex,6)];
section.bars=bars;
float targetEnergy=energy;
if(sectionIndex==0)targetEnergy*=0.55f;
if(sectionIndex==2)targetEnergy=juce::jmin(1.f,targetEnergy+0.12f);
if(sectionIndex==3||sectionIndex==5)targetEnergy=juce::jmin(1.f,targetEnergy+0.28f);
if(sectionIndex==4)targetEnergy*=0.45f;
if(sectionIndex==6)targetEnergy*=0.40f;
section.energy=targetEnergy;
section.densityMultiplier=0.55f+0.65f*targetEnergy;
for(int bar=0;bar<bars;++bar){
int deg=prog[(size_t)((bar+sectionIndex)%prog.size())];
if(chordsEnabled && !(sectionIndex==0&&bar>0&&r.nextFloat()<.35f))
addChords(section,bar,deg,targetEnergy,r);
if(bassEnabled && sectionIndex!=4)
addBass(section,bar,deg,targetEnergy,r);
if(melodyEnabled && sectionIndex!=0)
addMelody(section,bar,targetEnergy,r,inherited,variationSalt);
if(arpEnabled && (sectionIndex>=2 || genre==Ambient))
addArp(section,bar,deg,targetEnergy,r);
if(fillAmount>0.01f && bar==bars-1 && r.nextFloat()<fillAmount){
for(int x=12;x<16;++x){
int n=snapToScale(72+r.nextInt (juce::Range<int> (-6, 7)));
section.notes.push_back({bar*16+x,1,n,70+x-12,3,false});
}
}
}
if(sectionIndex==3||sectionIndex==5){
for(auto& e:section.notes){
if(e.channel==3) e.velocity=juce::jlimit(1,127,e.velocity+10);
}
}
if(sectionIndex==4){
for(auto& e:section.notes) e.velocity=juce::jlimit(1,127,e.velocity-18);
}
}
void MidiForgeAudioProcessor::buildBaseSong(SongData& song,juce::Random& r, int variationSalt)
{
song.sections.clear();
const auto prog=progressionDegrees();
int sectionCount=1; // Только Loop режим
for(int i=0;i<sectionCount;++i){
Section sec;
const std::vector<NoteEvent>* inherited=nullptr;
if(i>0 && !song.sections.empty()){
for(const auto& ev:song.sections[i-1].notes)
if(ev.channel==3){ inherited=&song.sections[i-1].notes; break; }
}
buildSection(sec,i,prog,r,inherited,variationSalt);
song.sections.push_back(std::move(sec));
}
}
void MidiForgeAudioProcessor::buildVariationBank()
{
Section previousSelected;
{
const juce::ScopedLock sl (variationsLock);
if (!variations.empty())
previousSelected = variations[(size_t) selectedVariation];
}
std::vector<Section> result;
result.reserve (8);
for(int v=0;v<8;++v){
juce::Random local((juce::int64)seed + 7919LL*(v+1));
SongData s;
float oldVariation=variationAmount;
variationAmount=juce::jlimit(0.f,1.f,oldVariation + (v-3.5f)*0.08f);
buildBaseSong(s,local,v);
variationAmount=oldVariation;
Section flat;
flat.name="VARIATION "+juce::String(v+1);
flat.bars=0;
for(const auto& sec:s.sections){
flat.bars+=sec.bars;
const int sectionBarsBefore=flat.bars-sec.bars;
for(auto n:sec.notes){
n.step+=sectionBarsBefore*16;
if(v%4==1 && n.channel==3 && local.nextFloat()<0.20f)n.note+=12;
if(v%4==2 && n.channel==3 && local.nextFloat()<0.20f)n.note-=12;
if(v%4==3 && n.channel==2 && local.nextFloat()<0.20f)n.velocity-=10;
flat.notes.push_back(n);
}
}
auto applyLock = [&](int channel, bool locked)
{
if (!locked) return;
flat.notes.erase(std::remove_if(flat.notes.begin(), flat.notes.end(),
[channel](const NoteEvent& n){ return n.channel == channel; }), flat.notes.end());
for (const auto& n : previousSelected.notes)
if (n.channel == channel && n.step < flat.bars * 16)
flat.notes.push_back(n);
};
applyLock(1, lockChordsLayer);
applyLock(2, lockBassLayer);
applyLock(3, lockMelodyLayer);
applyLock(4, lockArpLayer);
result.push_back(std::move(flat));
}
const juce::ScopedLock sl (variationsLock);
variations = std::move (result);
// Контент новый — счётчики лайков по слотам сбрасываем (профиль вкуса живёт отдельно).
likeCounts.fill(0);
dislikeCounts.fill(0);
}
void MidiForgeAudioProcessor::regenerate()
{
buildVariationBank();
chooseVariation (0);
}
void MidiForgeAudioProcessor::regenerateVariations()
{
int keep = 0;
{
const juce::ScopedLock sl (variationsLock);
keep = selectedVariation;
}
buildVariationBank();
chooseVariation (keep);
}
void MidiForgeAudioProcessor::chooseVariation(int index)
{
std::vector<NoteEvent> selectedNotes;
int selectedBars = bars;
{
const juce::ScopedLock sl(variationsLock);
if (variations.empty())
return;
selectedVariation=juce::jlimit(0,(int)variations.size()-1,index);
selectedNotes = variations[(size_t)selectedVariation].notes;
selectedBars = variations[(size_t)selectedVariation].bars;
}
{
const juce::ScopedLock sl(activeNotesLock);
activeNotes = std::move(selectedNotes);
activeBars = selectedBars;
}
lastGlobalStep.store (-1);
}
MidiForgeAudioProcessor::Section MidiForgeAudioProcessor::mergedSelectedSong() const
{
if(variations.empty())return {};
return variations[(size_t)selectedVariation];
}
void MidiForgeAudioProcessor::emitNote(const NoteEvent& e,juce::MidiBuffer& midi,
int sampleOffset,int velocityBias)
{
if(e.step<0)return;
int velocity=juce::jlimit(1,127,e.velocity+velocityBias);
midi.addEvent(juce::MidiMessage::noteOn(e.channel,e.note,(juce::uint8)velocity),sampleOffset);
const double currentBpmValue = (tempoMode == DAW_Sync) ? currentBpm.load() : manualBpm;
const double stepSamples = sampleRate * 60.0 / juce::jmax (20.0, currentBpmValue) / 4.0;
const juce::int64 offGlobal = samplePosition + sampleOffset
+ (juce::int64) juce::jmax (1.0, e.length * stepSamples);
pendingOffs.push_back ({ offGlobal, e.channel, e.note });
}
bool MidiForgeAudioProcessor::exportMidi(const juce::File& targetFile) const
{
const juce::ScopedLock sl(variationsLock);
if (variations.empty())
return false;
const auto& song = variations[(size_t)juce::jlimit(0, (int)variations.size() - 1, selectedVariation)];
constexpr int ppq = 960;
constexpr int ticksPerStep = ppq / 4;
juce::MidiFile file;
file.setTicksPerQuarterNote(ppq);
juce::MidiMessageSequence conductor;
const double currentBpmValue = (tempoMode == DAW_Sync) ? currentBpm.load() : manualBpm;
const int microsecondsPerQuarterNote = juce::roundToInt (60000000.0 / juce::jmax (20.0, currentBpmValue));
conductor.addEvent (juce::MidiMessage::tempoMetaEvent (microsecondsPerQuarterNote), 0.0);
conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
const int totalSteps = juce::jmax(1, song.bars * 16);
const double endTick = (double) totalSteps * ticksPerStep;
conductor.addEvent(juce::MidiMessage::endOfTrack(), endTick + ppq);
file.addTrack(conductor);
for (int channel = 1; channel <= 4; ++channel)
{
juce::MidiMessageSequence track;
for (const auto& e : song.notes)
{
if (e.channel != channel)
continue;
const double onTick = (double) e.step * ticksPerStep;
const double offTick = onTick + (double) juce::jmax(1, e.length) * ticksPerStep;
const int velocity = juce::jlimit(1, 127, e.velocity);
track.addEvent(juce::MidiMessage::noteOn(channel, e.note, (juce::uint8) velocity), onTick);
track.addEvent(juce::MidiMessage::noteOff(channel, e.note), offTick);
}
track.updateMatchedPairs();
track.addEvent(juce::MidiMessage::endOfTrack(), endTick + ppq);
file.addTrack(track);
}
juce::File output = targetFile.withFileExtension(".mid");
auto stream = output.createOutputStream();
if (stream == nullptr)
return false;
return file.writeTo (*stream, 1);
}
void MidiForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi)
{
audio.clear();
juce::MidiBuffer out;
for(const auto m:midi)out.addEvent(m.getMessage(),m.samplePosition);
if(auto* ph=getPlayHead()){
if(auto pos=ph->getPosition()){
currentBpm.store (pos->getBpm().orFallback (120.0));
const double ppq=pos->getPpqPosition().orFallback(0.0);
const int globalStep=(int)std::floor(ppq*4.0);
if (globalStep != lastGlobalStep.load())
{
lastGlobalStep.store (globalStep);
const juce::ScopedLock sl(activeNotesLock);
if(!activeNotes.empty()){
const int period=juce::jmax(16,activeBars*16);
int local=globalStep%period;
if(local<0)local+=period;
uiCurrentStep.store (local);
int offset=0;
const double currentBpmValue = (tempoMode == DAW_Sync) ? currentBpm.load() : manualBpm;
if((local%2)==1)
offset=(int)(swing*sampleRate*60.0/juce::jmax(20.0,currentBpmValue)/8.0);
int velBias=(int)((realtimeRng.nextFloat()*2.f-1.f)*14.f*humanize);
for(const auto& e:activeNotes){
if(e.step==local) emitNote(e,out,offset,velBias);
}
}
}
}
}
const juce::int64 blockEnd = samplePosition + audio.getNumSamples();
for (size_t i = 0; i < pendingOffs.size(); )
{
auto& p = pendingOffs[i];
if (p.globalSample < blockEnd)
{
const juce::int64 local = juce::jlimit<juce::int64> (0, juce::jmax (0, audio.getNumSamples() - 1),
p.globalSample - samplePosition);
out.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), (int) local);
p = pendingOffs.back();
pendingOffs.pop_back();
}
else
{
++i;
}
}
samplePosition += audio.getNumSamples();
midi.swapWith(out);
}
void MidiForgeAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
juce::MemoryOutputStream o(dest,false);
o.writeInt(rootPc);o.writeInt(genre);o.writeInt(scale);o.writeInt(progression);
o.writeInt(rhythm);o.writeInt(bars);o.writeInt(seed);o.writeInt(octave);o.writeInt(sectionMode);
o.writeFloat(chordDensity);o.writeFloat(bassDensity);o.writeFloat(melodyDensity);o.writeFloat(arpDensity);
o.writeFloat(swing);o.writeFloat(humanize);o.writeFloat(complexity);
o.writeFloat(melodyLength);o.writeFloat(pauseChance);o.writeFloat(leapChance);o.writeFloat(ghostChance);
o.writeInt(arpRate);o.writeFloat(voicingWidth);o.writeBool(chordExtensions);o.writeBool(inversions);
o.writeFloat(motifStrength);o.writeFloat(variationAmount);o.writeFloat(fillAmount);o.writeFloat(energy);
o.writeBool(chordsEnabled);o.writeBool(bassEnabled);o.writeBool(melodyEnabled);o.writeBool(arpEnabled);o.writeBool(hookMode);
o.writeInt(selectedVariation);
o.writeInt((int)tempoMode);o.writeFloat(manualBpm);
}
void MidiForgeAudioProcessor::setStateInformation(const void* data,int size)
{
if(!data||size<=0)return;
juce::MemoryInputStream i(data,(size_t)size,false);
rootPc=i.readInt();genre=i.readInt();scale=i.readInt();progression=i.readInt();
rhythm=i.readInt();bars=i.readInt();seed=i.readInt();octave=i.readInt();sectionMode=i.readInt();
chordDensity=i.readFloat();bassDensity=i.readFloat();melodyDensity=i.readFloat();arpDensity=i.readFloat();
swing=i.readFloat();humanize=i.readFloat();complexity=i.readFloat();
melodyLength=i.readFloat();pauseChance=i.readFloat();leapChance=i.readFloat();ghostChance=i.readFloat();
arpRate=i.readInt();voicingWidth=i.readFloat();chordExtensions=i.readBool();inversions=i.readBool();
motifStrength=i.readFloat();variationAmount=i.readFloat();fillAmount=i.readFloat();energy=i.readFloat();
chordsEnabled=i.readBool();bassEnabled=i.readBool();melodyEnabled=i.readBool();arpEnabled=i.readBool();hookMode=i.readBool();
int savedSelection=i.readInt();
if (i.getNumBytesRemaining() >= (int)sizeof(int)) tempoMode = (TempoMode)i.readInt();
if (i.getNumBytesRemaining() >= (int)sizeof(float)) manualBpm = i.readFloat();
regenerate();
chooseVariation (savedSelection);
}
// --- MIDI export --------------------------------------------------------
juce::MidiFile MidiForgeAudioProcessor::buildMidiFile (int channelFilter) const
{
juce::MidiFile midiFile;
constexpr int ticksPerQuarter = 960;
midiFile.setTicksPerQuarterNote (ticksPerQuarter);
const int ticksPerStep = ticksPerQuarter / 4;
Section pattern;
{
const juce::ScopedLock sl (variationsLock);
pattern = mergedSelectedSong();
}
static const char* trackNames[5] = { "", "Chords", "Bass", "Melody", "Arp" };
for (int channel = 1; channel <= 4; ++channel)
{
if (channelFilter != 0 && channel != channelFilter) continue;
juce::MidiMessageSequence track;
track.addEvent (juce::MidiMessage::textMetaEvent (3, trackNames[channel]), 0.0);
for (const auto& n : pattern.notes)
{
if (n.channel != channel) continue;
const double onTick  = n.step * (double) ticksPerStep;
const double offTick = onTick + juce::jmax (1, n.length) * (double) ticksPerStep;
auto on = juce::MidiMessage::noteOn (channel, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity));
auto off = juce::MidiMessage::noteOff (channel, n.note);
track.addEvent (on, onTick);
track.addEvent (off, offTick);
}
track.updateMatchedPairs();
midiFile.addTrack (track);
}
return midiFile;
}
bool MidiForgeAudioProcessor::exportMidiFileTo (const juce::File& file) const
{
auto midiFile = buildMidiFile (0);
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
bool MidiForgeAudioProcessor::exportMidiFileToChannel (const juce::File& file, int channel) const
{
auto midiFile = buildMidiFile (channel);
if (auto stream = file.createOutputStream())
return midiFile.writeTo (*stream);
return false;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFile() const
{
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileTo (file);
return file;
}
juce::File MidiForgeAudioProcessor::writeTemporaryMidiFileForChannel (int channel) const
{
static const char* names[5] = { "All", "Chords", "Bass", "Melody", "Arp" };
auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
.getChildFile ("MidiForge_" + juce::String (names[juce::jlimit (0, 4, channel)])
+ "_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".mid");
exportMidiFileToChannel (file, channel);
return file;
}
std::vector<MidiForgeAudioProcessor::VisibleNote> MidiForgeAudioProcessor::getVisibleNotes() const
{
const juce::ScopedLock sl (activeNotesLock);
std::vector<VisibleNote> out;
out.reserve (activeNotes.size());
for (const auto& n : activeNotes)
out.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
return out;
}
int MidiForgeAudioProcessor::getVisibleBars() const
{
const juce::ScopedLock sl (activeNotesLock);
return activeBars;
}

void MidiForgeAudioProcessor::syncEditedNotesToSelectedVariation (const std::vector<VisibleNote>& notes)
{
    const juce::ScopedLock sl (variationsLock);
    if (selectedVariation < 0 || selectedVariation >= static_cast<int> (variations.size()))
        return;

    auto& section = variations[static_cast<size_t> (selectedVariation)];
    section.notes.clear();
    section.notes.reserve (notes.size());
    for (const auto& n : notes)
        section.notes.push_back ({ n.step, n.length, n.note, n.velocity, n.channel, false });
}

bool MidiForgeAudioProcessor::addVisibleNote (int step, int note, int length, int velocity, int channel)
{
    VisibleNote created { juce::jmax (0, step), juce::jmax (1, length),
                          juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity),
                          juce::jlimit (1, 4, channel) };
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        activeNotes.push_back ({ created.step, created.length, created.note, created.velocity, created.channel, false });
        snapshot.reserve (activeNotes.size());
        for (const auto& n : activeNotes)
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
    }
    syncEditedNotesToSelectedVariation (snapshot);
    return true;
}

bool MidiForgeAudioProcessor::editVisibleNote (int index, int step, int note, int length, int velocity)
{
    std::vector<VisibleNote> snapshot;
    bool changed = false;
    {
        const juce::ScopedLock sl (activeNotesLock);
        if (index < 0 || index >= static_cast<int> (activeNotes.size()))
            return false;
        auto& n = activeNotes[static_cast<size_t> (index)];
        n.step = juce::jmax (0, step);
        n.note = juce::jlimit (0, 127, note);
        n.length = juce::jmax (1, length);
        n.velocity = juce::jlimit (1, 127, velocity);
        snapshot.reserve (activeNotes.size());
        for (const auto& item : activeNotes)
            snapshot.push_back ({ item.step, item.length, item.note, item.velocity, item.channel });
        changed = true;
    }
    if (changed)
        syncEditedNotesToSelectedVariation (snapshot);
    return changed;
}

bool MidiForgeAudioProcessor::deleteVisibleNote (int index)
{
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        if (index < 0 || index >= static_cast<int> (activeNotes.size()))
            return false;
        activeNotes.erase (activeNotes.begin() + index);
        snapshot.reserve (activeNotes.size());
        for (const auto& n : activeNotes)
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
    }
    syncEditedNotesToSelectedVariation (snapshot);
    return true;
}

void MidiForgeAudioProcessor::replaceVisibleNotes (const std::vector<VisibleNote>& notes)
{
    std::vector<VisibleNote> cleaned;
    cleaned.reserve (notes.size());
    {
        const juce::ScopedLock sl (activeNotesLock);
        activeNotes.clear();
        for (const auto& n : notes)
        {
            const auto step = juce::jlimit (0, juce::jmax (0, activeBars * 16 - 1), n.step);
            const auto length = juce::jmax (1, n.length);
            const auto note = juce::jlimit (0, 127, n.note);
            const auto velocity = juce::jlimit (1, 127, n.velocity);
            const auto channel = juce::jlimit (1, 4, n.channel);
            activeNotes.push_back ({ step, length, note, velocity, channel, false });
            cleaned.push_back ({ step, length, note, velocity, channel });
        }
    }
    syncEditedNotesToSelectedVariation (cleaned);
}

void MidiForgeAudioProcessor::quantizeVisibleNotes (int gridSteps)
{
    const int grid = juce::jmax (1, gridSteps);
    std::vector<VisibleNote> snapshot;
    {
        const juce::ScopedLock sl (activeNotesLock);
        snapshot.reserve (activeNotes.size());
        for (auto& n : activeNotes)
        {
            n.step = juce::jmax (0, (n.step + grid / 2) / grid * grid);
            snapshot.push_back ({ n.step, n.length, n.note, n.velocity, n.channel });
        }
    }
    syncEditedNotesToSelectedVariation (snapshot);
}

juce::AudioProcessorEditor* MidiForgeAudioProcessor::createEditor(){return new MidiForgeAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MidiForgeAudioProcessor();}
