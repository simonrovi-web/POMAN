#pragma once

#include <juce_graphics/juce_graphics.h>

// Shared UI colours: a rock-amp theme from the POMAN logo. Black tolex and leather, aged brass and
// copper hardware, amber tube glow. The names come from the first (ocean) theme; what each one is now:
namespace palette
{
inline const juce::Colour abyss { 0xff0b0806 };     // deepest black: wells, tracks, meters
inline const juce::Colour trench { 0xff16100b };    // background / button face
inline const juce::Colour panel { 0xff1e1611 };     // section panels (dark leather)
inline const juce::Colour panelEdge { 0xff5c4229 }; // bronze edges and trims
inline const juce::Colour text { 0xfff3e6c8 };      // cream lettering
inline const juce::Colour dimText { 0xffa8916b };   // aged-brass labels
inline const juce::Colour biolume { 0xfff0a830 };   // amber gold: the main accent (amp, cab, tabs)
inline const juce::Colour pressure { 0xffd07a3c };  // copper
inline const juce::Colour ember { 0xffff6a1f };     // hot orange: drive, clip warnings
inline const juce::Colour kelp { 0xff8fc456 };      // jewel-lamp green: OK / in tune
inline const juce::Colour aurora { 0xffc0503a };    // oxblood red
inline const juce::Colour coral { 0xffe0482c };     // red-copper
inline const juce::Colour sunlight { 0xffffd25a };  // bright brass

// Metal for knobs, screws and plates.
inline const juce::Colour brassLight { 0xffe8c98a };
inline const juce::Colour brassDark { 0xff6e5030 };
inline const juce::Colour steel { 0xffb9b3a6 };
} // namespace palette
