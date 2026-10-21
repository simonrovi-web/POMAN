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
// Memory stays bounded whatever the session length: takes are never held in RAM. The armed track
// streams to a WAV file through a ThreadedWriter, and existing takes play back from disk through
// BufferingAudioReaders that read ahead on a background thread. Waveform peaks are collected while
// recording (and cached beside each WAV as .peaks), and the mix export renders in chunks.
//
// Threading: process() runs on the audio thread and never blocks on disk. Everything else runs on
// the message thread; takes are only swapped under a lock the audio thread try-locks, and old
// takes are destroyed outside it.
class MultitrackRecorder
{
public:
    static constexpr int numTracks = 4;
    static constexpr int samplesPerPeak = 512;
    enum class State { stopped, playing, recording };

    explicit MultitrackRecorder (juce::File recordingsRoot);
    ~MultitrackRecorder();

    // Audio thread ---------------------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
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
    // session.xml in the session folder: takes (file, position, latency trim), mixer, tempo and click.
    // Written automatically after every take and on exit; call after mixer edits to keep it current.
    void saveSession() const;
    // Opens a session folder. Folders from 0.5.0 (no session.xml) load each track's newest WAV at 0:00.
    juce::Result openSession (const juce::File& folder);
    juce::File getRecordingsRoot() const { return recordingsRoot; }
    // Extra sources for the export (e.g. the drum machine): called for each chunk, in order, with the
    // chunk's timeline position, after the tracks are mixed and before limiting.
    using ExportLayer = std::function<void (juce::AudioBuffer<float>& chunk, juce::int64 position)>;
    juce::Result exportMix (juce::File& writtenFile, const ExportLayer& addLayers = {});

    // How long a playback read may wait for the disk. 0 (the default) never blocks the audio thread;
    // offline renders and tests can wait so nothing is skipped.
    void setReadTimeoutMs (int ms) { readTimeoutMs = ms; }

    // Samples the disk could not keep up with during the last take (0 when the take is complete).
    juce::int64 getDroppedSamples() const { return droppedSamples.load(); }

    // Peaks of the take being recorded so far (message thread; call regularly while recording).
    const std::vector<float>& getLivePeaks();
    juce::File getSessionFolder() const { return sessionFolder; }

    void setCountIn (bool enabled) { countInEnabled.store (enabled); }
    bool isCountInEnabled() const { return countInEnabled.load(); }
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
    juce::int64 getRecordingStart() const { return recordingStart.load(); }
    // True while recording but the playhead is still in the one-bar count-in.
    bool isCountingIn() const { return state.load() == State::recording && playhead.load() < recordingStart.load(); }

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
        std::unique_ptr<juce::BufferingAudioReader> reader; // read by the audio thread
        juce::int64 start = 0, length = 0;
        int trim = 0;          // samples skipped at the head of the file (latency compensation)
        int fileChannels = 2;
        std::vector<float> peaks;
        juce::File file;
    };

    void finishRecording();
    std::unique_ptr<Take> loadTake (const juce::File& file, juce::int64 start, int trimSamples, std::vector<float>* knownPeaks = nullptr);
    std::vector<float> readPeaks (juce::AudioFormatReader& reader, int trimSamples) const;
    void drainLivePeaks();
    void replaceTake (int track, std::unique_ptr<Take> take);
    void movePlayhead (juce::int64 sample);
    void addClick (float* left, float* right, int from, int to, juce::int64 position, juce::int64 origin) noexcept;

    juce::File recordingsRoot, sessionFolder;
    // True once this recorder created (first take) or opened sessionFolder. Only an owned folder is
    // ever written, so a same-named folder from another session can never be overwritten.
    bool sessionOwned = false;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread writerThread { "POMAN recorder" }, readerThread { "POMAN playback" };
    int readTimeoutMs = 0;
    std::atomic<juce::int64> droppedSamples { 0 };
    juce::AudioBuffer<float> playScratch;

    // Peaks of the take in progress: the audio thread pushes one per samplesPerPeak through a FIFO.
    juce::AbstractFifo livePeakFifo { 1 << 16 };
    std::vector<float> livePeakStorage = std::vector<float> (1 << 16);
    std::vector<float> livePeaks;
    float livePeakMax = 0.0f;
    int livePeakCount = 0;

    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> activeWriter;
    juce::File recordingFile;
    std::atomic<juce::int64> recordingStart { 0 }; // take start on the timeline, read by process()
    int recordingTrim = 0;
    std::atomic<int> recordingTrack { -1 }; // written on the message thread, read by process()

    juce::SpinLock takeLock;
    std::array<std::unique_ptr<Take>, numTracks> takes;

    std::atomic<State> state { State::stopped };
    std::atomic<juce::int64> playhead { 0 };
    static constexpr juce::int64 noSeek = std::numeric_limits<juce::int64>::min();
    std::atomic<juce::int64> seekRequest { noSeek };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> armedTrack { 0 };
    std::atomic<int> latencyCompensation { 0 };
    std::array<std::atomic<bool>, numTracks> trackMuted {}, trackSolo {};
    std::array<std::atomic<float>, numTracks> trackVolume {};

    std::atomic<bool> metronomeOn { false }, countInEnabled { true };
    std::atomic<double> tempo { 120.0 };
    float clickPhase = 0.0f, clickEnvelope = 0.0f, clickFrequency = 1000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultitrackRecorder)
};
} // namespace poman
