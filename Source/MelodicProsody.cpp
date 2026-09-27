#include "MelodicProsody.h"

#include <algorithm>
#include <cmath>

namespace
{
static uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
}

namespace midiforge
{
const char* MelodicProsody::roleName (Role role)
{
    switch (role)
    {
        case Anchor:   return "ANCHOR";
        case Pickup:   return "PICKUP";
        case Approach: return "APPROACH";
        case Connect:  return "CONNECT";
        case Accent:   return "ACCENT";
        case Peak:     return "PEAK";
        case Release:  return "RELEASE";
        default:       return "CONNECT";
    }
}

MelodicProsody::Context MelodicProsody::classify (
    int index,
    int count,
    int localStep,
    int nextLocalStep,
    int absNextInterval,
    bool finalOfPhrase,
    ComposerGrammar::Role phraseRole,
    float tension,
    uint32_t identity)
{
    Context c;
    if (count <= 0)
        return c;

    const float progress = (float) index / (float) std::max (1, count - 1);
    const uint32_t h = mix32 (
        identity ^ (uint32_t) (index + 1) * 0x9e3779b9u
        ^ (uint32_t) (localStep + 3) * 0x85ebca6bu);

    // A final phrase note is a release unless the composer explicitly asks for
    // a peak. A strong cadence needs a destination, not another random accent.
    if (finalOfPhrase && phraseRole != ComposerGrammar::Peak)
    {
        c.role = Release;
        c.scaleMotion = 1;
        c.velocityBias = -0.03f;
        c.sustainBias = 0.16f;
        return c;
    }

    // The macro peak gets a dedicated note-level peak role. Identity jitter
    // decides whether the peak arrives a little before or after the midpoint.
    const bool contrastRole =
        phraseRole == ComposerGrammar::Contrast || phraseRole == ComposerGrammar::Peak;
    const float peakWindow = 0.24f + 0.08f * tension;
    const float peakCentre = 0.56f + ((h & 1u) ? 0.05f : -0.04f);
    if (contrastRole
        && std::abs (progress - peakCentre) < peakWindow
        && tension > 0.54f)
    {
        c.role = Peak;
        c.scaleMotion = 1;
        c.velocityBias = 0.075f;
        c.sustainBias = -0.045f;
        return c;
    }

    // Strong beats are anchors/accent points. We do not sacrifice their identity
    // just because the following note is a leap; the harmony layer can resolve
    // the destination later.
    if ((localStep % 4) == 0)
    {
        c.role = (localStep == 0 || localStep == 8) ? Anchor : Accent;
        c.scaleMotion = 0;
        c.velocityBias = c.role == Anchor ? 0.045f : 0.03f;
        c.sustainBias = c.role == Anchor ? 0.05f : 0.0f;
        return c;
    }

    // Late sixteenths before the next strong event act as pickups. They are
    // intentionally light and short, so they create forward motion into the
    // following anchor instead of competing with it.
    if (localStep >= 12 && nextLocalStep >= 0 && nextLocalStep - localStep <= 3
        && progress < 0.98f)
    {
        c.role = Pickup;
        c.scaleMotion = 0;
        c.velocityBias = -0.045f;
        c.sustainBias = -0.05f;
        return c;
    }

    // Large jumps are not deleted: the next note becomes an intentional target.
    // The current note gets a scale-step approach toward that target.
    if (absNextInterval >= 5 && nextLocalStep >= 0)
    {
        c.role = Approach;
        c.scaleMotion = absNextInterval > 9 ? 2 : 1;
        c.velocityBias = -0.008f;
        c.sustainBias = -0.015f;
        return c;
    }

    c.role = Connect;
    c.scaleMotion = absNextInterval >= 4 ? 1 : 0;
    c.velocityBias = -0.005f;
    c.sustainBias = 0.0f;
    return c;
}
}
