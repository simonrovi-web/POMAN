#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace poman
{
// Live looper (up to 60 s, stereo) with overdub and single-level undo/redo.
//
// The first pass records into `base`. Each overdub writes a new generation into `dub`; every
// 1024-sample block carries the generation it holds. When an overdub first touches a block that
// still holds an older (kept) generation, that block is merged into `base` right there — so no
// whole-loop copy ever happens on the audio thread. Undo only marks the latest generation as
// discarded (instant, and redo-able until the next overdub); its blocks are dropped when reused.
//
// Commands arrive through a lock-free queue (UI buttons and MIDI footswitches) and are applied at
// the start of the next block.
class Looper
{
public:
    enum class State { empty, recording, playing, overdubbing, stopped };
    enum class Command { main, stop, undo, clear };
    static constexpr double maxSeconds = 60.0;
    static constexpr int tagBlock = 1024;

    // Allocates once per sample rate; a device restart at the same rate keeps the loop.
    void prepare (double newSampleRate)
    {
        if (newSampleRate == sampleRate && ! baseL.empty())
            return;
        sampleRate = newSampleRate;
        capacity = (int) (sampleRate * maxSeconds);
        for (auto* b : { &baseL, &baseR, &dubL, &dubR })
            b->assign ((size_t) capacity, 0.0f);
        tags.assign ((size_t) (capacity / tagBlock + 1), noTag);
        clearNow();
    }

    // Any thread (UI, MIDI dispatcher).
    void post (Command c)
    {
        if (commands.getFreeSpace() > 0)
            commands.write (1).forEach ([this, c] (int index) { commandStorage[(size_t) index] = c; });
    }

    void setLevel (float gain) { level.store (gain); }
    float getLevel() const { return level.load(); }

    State getState() const { return state.load(); }
    double getLengthSeconds() const { return lengthSamples.load() / sampleRate; }
    double getPositionSeconds() const { return position.load() / sampleRate; }
    double getMaxSeconds() const { return maxSeconds; }
    bool canUndo() const { return undoable.load(); }
    bool canRedo() const { return redoable.load(); }

    // Audio thread: records/overdubs the incoming signal and adds the loop to it.
    void process (float* left, float* right, int numSamples) noexcept
    {
        applyCommands();
        auto s = state.load();
        if (s == State::empty || s == State::stopped)
            return;

        const float gain = level.load();
        for (int i = 0; i < numSamples; ++i)
        {
            if (s == State::recording)
            {
                baseL[(size_t) pos] = left[i];
                baseR[(size_t) pos] = right[i];
                if (++pos >= capacity) // full: close the loop automatically
                {
                    length = capacity;
                    pos = 0;
                    s = State::playing;
                    state.store (s);
                }
                continue;
            }

            const int block = pos / tagBlock;
            if (s == State::overdubbing && tags[(size_t) block] != generation)
                commitBlock (block);

            // The loop as it was before this sample's input: live input is already in left/right.
            float outL = baseL[(size_t) pos], outR = baseR[(size_t) pos];
            const int tag = tags[(size_t) block];
            if (tag != noTag && tag != undoneGeneration)
            {
                outL += dubL[(size_t) pos];
                outR += dubR[(size_t) pos];
            }

            if (s == State::overdubbing)
            {
                dubL[(size_t) pos] += left[i];
                dubR[(size_t) pos] += right[i];
            }

            left[i] += gain * outL;
            right[i] += gain * outR;
            if (++pos >= length)
                pos = 0;
        }

        position.store (pos);
        lengthSamples.store (s == State::recording ? pos : length); // also covers the automatic close at the limit
    }

private:
    static constexpr int noTag = -1;

    void applyCommands() noexcept
    {
        commands.read (commands.getNumReady()).forEach ([this] (int index) { apply (commandStorage[(size_t) index]); });
    }

    void apply (Command c) noexcept
    {
        const auto s = state.load();
        switch (c)
        {
            case Command::main:
                if (s == State::empty)
                    startRecording();
                else if (s == State::recording)
                    closeLoop (State::playing);
                else if (s == State::playing)
                    startOverdub();
                else if (s == State::overdubbing)
                    state.store (State::playing);
                else if (s == State::stopped)
                {
                    pos = 0;
                    state.store (State::playing);
                }
                break;

            case Command::stop:
                if (s == State::recording)
                    closeLoop (State::stopped);
                else if (s == State::playing || s == State::overdubbing)
                {
                    pos = 0;
                    state.store (State::stopped);
                }
                break;

            case Command::undo:
                if (generation > 0 && undoneGeneration != generation) // undo the latest overdub
                {
                    undoneGeneration = generation;
                    if (s == State::overdubbing)
                        state.store (State::playing);
                }
                else if (generation > 0 && undoneGeneration == generation) // redo it
                {
                    undoneGeneration = noTag - 1;
                }
                publishUndo();
                break;

            case Command::clear:
                clearNow();
                break;
        }
        lengthSamples.store (length);
    }

    void startRecording() noexcept
    {
        pos = 0;
        length = 0;
        state.store (State::recording);
    }

    void closeLoop (State next) noexcept
    {
        if (pos < (int) (sampleRate * 0.1)) // too short to be intentional: start over
        {
            clearNow();
            return;
        }
        length = pos;
        pos = 0;
        state.store (next);
    }

    void startOverdub() noexcept
    {
        ++generation;
        state.store (State::overdubbing);
        publishUndo();
    }

    // Folds an older kept generation into base (or drops an undone one) before reusing the block.
    void commitBlock (int block) noexcept
    {
        const int tag = tags[(size_t) block];
        const bool keep = tag != noTag && tag != undoneGeneration;
        const int from = block * tagBlock, to = juce::jmin (from + tagBlock, capacity);
        for (int k = from; k < to; ++k)
        {
            if (keep)
            {
                baseL[(size_t) k] += dubL[(size_t) k];
                baseR[(size_t) k] += dubR[(size_t) k];
            }
            dubL[(size_t) k] = dubR[(size_t) k] = 0.0f;
        }
        tags[(size_t) block] = generation;
    }

    void clearNow() noexcept
    {
        std::fill (tags.begin(), tags.end(), noTag);
        generation = 0;
        undoneGeneration = noTag - 1;
        pos = length = 0;
        position.store (0);
        lengthSamples.store (0);
        state.store (State::empty);
        publishUndo();
    }

    void publishUndo() noexcept
    {
        undoable.store (generation > 0 && undoneGeneration != generation);
        redoable.store (generation > 0 && undoneGeneration == generation);
    }

    double sampleRate = 0.0;
    int capacity = 0, pos = 0, length = 0;
    std::vector<float> baseL, baseR, dubL, dubR;
    std::vector<int> tags;
    int generation = 0, undoneGeneration = noTag - 1;

    juce::AbstractFifo commands { 32 };
    std::array<Command, 32> commandStorage {};
    std::atomic<State> state { State::empty };
    std::atomic<int> position { 0 }, lengthSamples { 0 };
    std::atomic<bool> undoable { false }, redoable { false };
    std::atomic<float> level { 1.0f };
};
} // namespace poman
