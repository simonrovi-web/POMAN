#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace poman
{
// Four-track recorder for the finished rig sound.
//
// Threading: process() runs on the audio thread and never blocks. It streams the armed track to a
// WAV file through a ThreadedWriter (disk I/O on a background thread) and mixes the existing takes
// under a try-lock. Everything else runs on the message thread; takes are only swapped while
// holding that lock, and old takes are destroyed outside it.
class MultitrackRecorder
{
public:
    static constexpr int numTracks = 4;
    static constexpr int samplesPerPeak = 512;
    enum class State { stopped, playing, recording };

    explicit MultitrackRecorder (juce::File recordingsRoot);
    ~MultitrackRecorder();

    // Audio thread ---------------------------------------------------------------------------
    void prepare (double sampleRate);
    // Records the incoming rig output (when recording), then adds track playback and the click.
    void process (float* left, float* right, int numSamples) noexcept;

    // Message thread -------------------------------------------------------------------------
    void play();
    void stop();
    juce::Result record();
    void rewind() { setPlayhead (0); }
    void setPlayhead (juce::int64 sample);

    void armTrack (int track); // exclusive; -1 disarms
    int getArmedTrack() const { return armedTrack.load(); }
    void setMuted (int track, bool muted) { trackMuted[(size_t) track].store (muted); }
    bool isMuted (int track) const { return trackMuted[(size_t) track].load(); }
    void setSolo (int track, bool solo) { trackSolo[(size_t) track].store (solo); }
    bool isSolo (int track) const { return trackSolo[(size_t) track].load(); }
    void setVolume (int track, float gain) { trackVolume[(size_t) track].store (gain); }
    float getVolume (int track) const { return trackVolume[(size_t) track].load(); }

    void clearTrack (int track);
    void newSession();
    juce::Result exportMix (juce::File& writtenFile);
    juce::File getSessionFolder() const { return sessionFolder; }

    void setMetronome (bool enabled) { metronomeOn.store (enabled); }
    bool isMetronomeOn() const { return metronomeOn.load(); }
    void setBpm (double bpm) { tempo.store (juce::jlimit (40.0, 240.0, bpm)); }
    double getBpm() const { return tempo.load(); }

    // Round-trip delay between hearing playback and the recorded rig output (device in + out + plugin).
    void setLatencyCompensation (int samples) { latencyCompensation.store (juce::jmax (0, samples)); }
    int getLatencyCompensation() const { return latencyCompensation.load(); }

    State getState() const { return state.load(); }
    juce::int64 getPlayhead() const { return playhead.load(); }
    double getSampleRate() const { return sampleRate.load(); }
    int getRecordingTrack() const { return recordingTrack.load(); }
    juce::int64 getRecordingStart() const { return recordingStart; } // message thread

    struct TrackInfo
    {
        bool hasAudio = false;
        juce::int64 start = 0, length = 0;
        const std::vector<float>* peaks = nullptr; // max |sample| per samplesPerPeak block
        juce::String fileName;
    };
    TrackInfo getTrackInfo (int track) const;
    juce::int64 getSessionLength() const;

private:
    struct Take
    {
        juce::AudioBuffer<float> audio;
        juce::int64 start = 0;
        std::vector<float> peaks;
        juce::File file;
    };

    void finishRecording();
    std::unique_ptr<Take> loadTake (const juce::File& file, juce::int64 start, int trimSamples);
    void replaceTake (int track, std::unique_ptr<Take> take);
    void addClick (float* left, float* right, int numSamples, juce::int64 position) noexcept;

    juce::File recordingsRoot, sessionFolder;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread writerThread { "POMAN recorder" };

    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> activeWriter;
    juce::File recordingFile;
    juce::int64 recordingStart = 0;
    std::atomic<int> recordingTrack { -1 }; // written on the message thread, read by process()

    juce::SpinLock takeLock;
    std::array<std::unique_ptr<Take>, numTracks> takes;

    std::atomic<State> state { State::stopped };
    std::atomic<juce::int64> playhead { 0 };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> armedTrack { 0 };
    std::atomic<int> latencyCompensation { 0 };
    std::array<std::atomic<bool>, numTracks> trackMuted {}, trackSolo {};
    std::array<std::atomic<float>, numTracks> trackVolume {};

    std::atomic<bool> metronomeOn { false };
    std::atomic<double> tempo { 120.0 };
    float clickPhase = 0.0f, clickEnvelope = 0.0f, clickFrequency = 1000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultitrackRecorder)
};
} // namespace poman
