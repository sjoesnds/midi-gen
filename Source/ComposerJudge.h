#pragma once

#include <array>

namespace midiforge
{
struct ComposerJudge
{
    struct Thresholds
    {
        float identity = 0.36f;
        float expression = 0.32f;
        float harmony = 0.50f;
        float rhythm = 0.38f;
        float macroArc = 0.34f;
        float loopClosure = 0.38f;
        float humanity = 0.28f;
    };

    struct Result
    {
        std::array<float, 7> axes {};
        float overall = 0.0f;
        int gatesPassed = 0;
        int gateFailures = 0;
        bool passed = false;
    };

    static Thresholds thresholdsFor (int bars, bool sparse);
    static Result evaluate (const std::array<float, 7>& axes,
                            int bars,
                            bool sparse);
};
} // namespace midiforge
