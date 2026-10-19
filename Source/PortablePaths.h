#pragma once

#include <juce_core/juce_core.h>

namespace abyssal
{
// Folder that holds Abyssal.exe in the standalone build; the portable root for settings and IRs.
inline juce::File portableRoot()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
}
} // namespace abyssal
