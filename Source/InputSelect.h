#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace poman
{
// Which interface input is the guitar. The rig is mono: it takes one input channel (default the
// first), or the average of inputs 1 and 2 for a guitar that arrives on both. Other inputs (a mic, a
// second instrument) are ignored. A choice the current device does not have falls back to input 1.
struct InputSelect
{
    static constexpr int sumOfFirstTwo = -1;
    static constexpr int maxInputs = 32;

    // Writes the selected guitar signal for n samples starting at `start` into out.
    static void read (const float* const* inputs, int numInputs, int choice, int start, float* out, int n) noexcept
    {
        if (numInputs <= 0)
        {
            juce::FloatVectorOperations::clear (out, n);
            return;
        }
        if (choice == sumOfFirstTwo && numInputs >= 2)
        {
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (inputs[0][start + i] + inputs[1][start + i]);
            return;
        }
        const int channel = choice >= 0 && choice < numInputs ? choice : 0;
        juce::FloatVectorOperations::copy (out, inputs[channel] + start, n);
    }

    static juce::String name (int choice) { return choice == sumOfFirstTwo ? juce::String ("IN 1+2") : "IN " + juce::String (choice + 1); }
};
} // namespace poman
