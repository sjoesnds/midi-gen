        double u = 0;
        for (int i = 0; i < presses; ++i) { p.magicRandomize(); u += utility (feats (p.getVisibleNotes(), p.getVisibleBars())); }
        return u / presses;
    }
}

int main()
{
    // Hermetic settings: never touch the real taste.json / feedback.csv (earlier runs used to leak learned taste into later ones).
    const auto qaSettings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("midiforge_qa_settings_" + juce::String (juce::Time::currentTimeMillis()));
    qaSettings.createDirectory();
    MidiForgeAudioProcessor::setSettingsDirectoryOverride (qaSettings);

    MidiForgeAudioProcessor p;
    p.resetTaste();

    // ------------------------------------------------------------------ Unified Melody Intent contract
    {
        const auto a = midiforge::MelodyIntent::makePlan (
            4, 0, 0, 0.65f, 0.55f, 0x1234ABCDu, 0);
        const auto b = midiforge::MelodyIntent::makePlan (
            4, 0, 0, 0.65f, 0.55f, 0x1234ABCDu, 0);

        const bool deterministic =
               a.characterIndex == b.characterIndex
            && a.language.contourFamily == b.language.contourFamily
            && a.language.intervalFamily == b.language.intervalFamily
            && a.language.rhythmFamily == b.language.rhythmFamily
            && a.language.repetitionStyle == b.language.repetitionStyle
            && a.language.registerJourney == b.language.registerJourney
            && a.grammar.phrases.size() == b.grammar.phrases.size()
            && a.motif.rhythmicCore == b.motif.rhythmicCore
            && a.motif.intervalCore == b.motif.intervalCore
            && a.motif.peakGesture == b.motif.peakGesture
            && a.motif.endingGesture == b.motif.endingGesture
            && a.motif.primaryMutation == b.motif.primaryMutation
            && a.motif.secondaryMutation == b.motif.secondaryMutation
            && a.simpleProbability == b.simpleProbability
            && a.complexProbability == b.complexProbability;

        std::set<std::string> languageSignatures;
        for (uint32_t i = 0; i < 16; ++i)
        {
            const auto plan = midiforge::MelodyIntent::makePlan (
                4, 0, 0, 0.65f, 0.55f, 0x9000u + i * 7919u, (int) (i % 8));
            languageSignatures.insert (
                std::to_string (plan.language.contourFamily) + "/"
                + std::to_string (plan.language.intervalFamily) + "/"
                + std::to_string (plan.language.rhythmFamily) + "/"
                + std::to_string (plan.characterIndex));
        }

        const bool probabilityContract =
               a.simpleProbability >= 0.18f
            && a.simpleProbability <= 0.68f
            && a.complexProbability >= 0.08f
            && a.complexProbability <= 0.38f
            && a.simpleProbability + a.complexProbability <= 0.90f;

        report ("Unified Melody Intent",
                deterministic && probabilityContract && languageSignatures.size() >= 4,
                fmt ("deterministic=%d signatures=%d simple=%.3f complex=%.3f",
                     deterministic, (double) languageSignatures.size(),
                     a.simpleProbability, a.complexProbability));
    }


    // ------------------------------------------------------------------ MAGIC Scale Coverage
    {
        std::set<int> seenScales;
        constexpr int scaleCount = 12;

        // MAGIC should be able to reach every declared scale, not only the
        // original first seven choices.
        for (int pass = 0; pass < 180; ++pass)
        {
            p.magicRandomize();
            seenScales.insert (p.getScale());
        }

        bool allScalesSeen = seenScales.size() == scaleCount;
        report ("MAGIC can reach every declared scale",
                allScalesSeen,
                fmt ("seen %d/12 scales", (double) seenScales.size()));
    }


    // ------------------------------------------------------------------ Melodic pleasantness safety
    // The final melody pass is intentionally conservative: generated lead notes stay
    // in the selected scale, avoid oversized default leaps, and break pathological
    // same-note runs without flattening the whole phrase.
    {
        int badScaleNotes = 0;
        int badLeaps = 0;
        int badRepeatRuns = 0;
        int checkedMelodyNotes = 0;

        for (int pass = 0; pass < 48; ++pass)
        {
            p.magicRandomize();
            const bool ostinato = p.getMelodyType() == MidiForgeAudioProcessor::OstinatoMelody;
            const int variationCount = p.getVariationCount();