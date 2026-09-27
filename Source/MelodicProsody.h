#pragma once

#include "ComposerGrammar.h"
#include <cstdint>

namespace midiforge
{
class MelodicProsody
{
public:
    enum Role
    {
        Anchor = 0,
        Pickup,
        Approach,
        Connect,
        Accent,
        Peak,
        Release
    };

    struct Context
    {
        Role role = Connect;
        int scaleMotion = 0;     // -2..+2 scale steps toward a target
        float velocityBias = 0.0f;
        float sustainBias = 0.0f;
    };

    static Context classify (int index,
                             int count,
                             int localStep,
                             int nextLocalStep,
                             int absNextInterval,
                             bool finalOfPhrase,
                             ComposerGrammar::Role phraseRole,
                             float tension,
                             uint32_t identity);

    static const char* roleName (Role role);
};
}
