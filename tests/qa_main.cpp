        a.regenerate ();

        std::vector<int> second;
        for (int k = 0; k < a.getVariationCount(); ++k)
            second.push_back (a.getVariationMelodyComplexityClass (k));

        report ("melody intent: complexity assignment is deterministic",
                first == second,
                first == second ? "first/second class vectors match"
                                 : "first/second class vectors differ");

        std::set<int> observedClasses;
        for (int seed = 1; seed <= 32; ++seed)
        {
            MidiForgeAudioProcessor b;
            b.setFeedbackLogFile (juce::File());