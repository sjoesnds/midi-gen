#pragma once

#include <string>

namespace midiforge
{
namespace qa
{
#ifdef MIDIFORGE_HEADLESS
// This selector exists only in the headless QA target. Production/VST3 builds
// compile the no-op implementations below, so the shipping scorer is untouched.
inline std::string& activeAblationName()
{
    static std::string name;
    return name;
}
inline void setActiveAblation (const std::string& name) { activeAblationName() = name; }
inline bool hasActiveAblation() { return ! activeAblationName().empty(); }
inline bool isAblated (const char* name)
{
    return name != nullptr && activeAblationName() == name;
}
inline float scoreOrZero (const char* name, float score)
{
    return isAblated (name) ? 0.0f : score;
}
#else
inline void setActiveAblation (const std::string&) {}
inline bool hasActiveAblation() { return false; }
inline bool isAblated (const char*) { return false; }
inline float scoreOrZero (const char*, float score) { return score; }
#endif
} // namespace qa
} // namespace midiforge
