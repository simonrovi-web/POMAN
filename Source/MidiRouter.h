#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>

namespace poman
{
// MIDI control: CC learn for parameters and actions, and Program Change for presets.
//
// The audio thread only copies incoming CC / Program Change messages into a lock-free FIFO
// (push()). Everything else runs on the message thread: dispatch() drains the FIFO, completes a
// pending learn, and reports each mapped event through a callback, so parameter changes and preset
// loads never happen on the audio thread. A target is a parameter ID or an "action:..." ID.
class MidiRouter
{
public:
    static constexpr const char* actionPrefix = "action:";

    // Audio thread.
    void push (const juce::MidiBuffer& midi) noexcept
    {
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isController())
                pushEvent ({ Event::controller, m.getControllerNumber(), m.getControllerValue() });
            else if (m.isProgramChange())
                pushEvent ({ Event::programChange, m.getProgramChangeNumber(), 0 });
        }
    }

    struct Event
    {
        enum Type { controller, programChange } type;
        int number, value;
    };

    // Message thread ---------------------------------------------------------------------------
    // onTarget (target, normalisedValue 0..1, risingPress) for mapped CCs; onProgram (program) for PCs.
    template <typename TargetFn, typename ProgramFn>
    void dispatch (TargetFn&& onTarget, ProgramFn&& onProgram)
    {
        // Drained in batches until empty, so a burst (e.g. a fast expression pedal sweep) loses nothing.
        while (fifo.getNumReady() > 0)
        {
            Event events[64];
            int count = 0;
            fifo.read (juce::jmin (fifo.getNumReady(), (int) std::size (events))).forEach ([&] (int index)
            {
                events[count++] = storage[(size_t) index];
            });
            for (int i = 0; i < count; ++i)
                handle (events[i], onTarget, onProgram);
        }
    }

private:
    template <typename TargetFn, typename ProgramFn>
    void handle (const Event& e, TargetFn& onTarget, ProgramFn& onProgram)
    {
        if (e.type == Event::programChange)
        {
            onProgram (e.number);
            return;
        }

        if (learnTarget.isNotEmpty())
        {
            assign (learnTarget, e.number);
            learnTarget.clear();
            return; // the learning gesture itself does not move the target
        }

        // Footswitches send 127/0: an action fires once when the value rises past the middle.
        const bool wasDown = lastValue[(size_t) e.number] >= 64;
        const bool isDown = e.value >= 64;
        lastValue[(size_t) e.number] = e.value;
        for (const auto& [target, cc] : mappings)
            if (cc == e.number)
                onTarget (target, (float) e.value / 127.0f, isDown && ! wasDown);
    }

public:

    void learn (const juce::String& target) { learnTarget = target; }
    void cancelLearn() { learnTarget.clear(); }
    bool isLearning() const { return learnTarget.isNotEmpty(); }
    juce::String getLearnTarget() const { return learnTarget; }

    // One CC per target and one target per CC: assigning replaces any previous use of either.
    void assign (const juce::String& target, int cc)
    {
        for (auto it = mappings.begin(); it != mappings.end();)
            it = (it->second == cc || it->first == target) ? mappings.erase (it) : std::next (it);
        mappings[target] = cc;
    }

    int getCcFor (const juce::String& target) const
    {
        const auto it = mappings.find (target);
        return it != mappings.end() ? it->second : -1;
    }

    void forget (const juce::String& target) { mappings.erase (target); }
    void forgetAll() { mappings.clear(); }
    bool hasMappings() const { return ! mappings.empty(); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("MidiMap");
        for (const auto& [target, cc] : mappings)
        {
            juce::ValueTree entry ("Map");
            entry.setProperty ("target", target, nullptr);
            entry.setProperty ("cc", cc, nullptr);
            tree.appendChild (entry, nullptr);
        }
        return tree;
    }

    void fromValueTree (const juce::ValueTree& tree)
    {
        if (! tree.hasType ("MidiMap"))
            return;
        mappings.clear();
        for (const auto& entry : tree)
        {
            const auto target = entry.getProperty ("target").toString();
            const int cc = entry.getProperty ("cc", -1);
            if (target.isNotEmpty() && cc >= 0 && cc < 128)
                assign (target, cc);
        }
    }

private:
    void pushEvent (const Event& e) noexcept
    {
        if (fifo.getFreeSpace() > 0)
            fifo.write (1).forEach ([this, &e] (int index) { storage[(size_t) index] = e; });
    }

    juce::AbstractFifo fifo { 256 };
    std::array<Event, 256> storage {};
    std::map<juce::String, int> mappings;
    std::array<int, 128> lastValue {};
    juce::String learnTarget;
};
} // namespace poman
