                + 0.22f * f.leap
                + 0.18f * f.rhythmIdentity
                + 0.26f * (1.0f - f.repetition));
            // 0.86 Unified Melody Intent: simplicity is judged against the same
            // class that shaped the candidate. The UI Complexity control already
            // influences the class distribution, so the class remains the stronger
            // signal and the old free-floating target no longer contradicts it.
            const int intentClass = juce::jlimit (
                0, 2, sec.melodyComplexityClass < 0 ? 1 : sec.melodyComplexityClass);
            static constexpr float classSimplicityTarget[3] = { 0.28f, 0.52f, 0.74f };
            const float uiSimplicityTarget = juce::jlimit (
                0.18f, 0.72f, 0.56f - 0.26f * juce::jlimit (0.0f, 1.0f, complexity));
            const float simplicityTarget = juce::jlimit (
                0.18f, 0.76f,
                0.80f * classSimplicityTarget[intentClass] + 0.20f * uiSimplicityTarget);
            f.simplicity = 1.0f - juce::jlimit (