#include "MultitrackRecorder.h"

namespace poman
{
MultitrackRecorder::MultitrackRecorder (juce::File root)
    : recordingsRoot (std::move (root))
{
    formatManager.registerBasicFormats();
    for (auto& v : trackVolume)
        v.store (0.8f);
    writerThread.startThread();
    newSession();
}

MultitrackRecorder::~MultitrackRecorder()
{
    finishRecording();
    writerThread.stopThread (4000);
}

void MultitrackRecorder::prepare (double newSampleRate)
{
    sampleRate.store (newSampleRate);
    clickEnvelope = 0.0f;
}

//==============================================================================
void MultitrackRecorder::process (float* left, float* right, int numSamples) noexcept
{
    const auto currentState = state.load();
    if (currentState == State::stopped)
        return;

    if (currentState == State::recording)
    {
        const juce::SpinLock::ScopedTryLockType lock (writerLock);
        if (lock.isLocked() && activeWriter != nullptr)
        {
            const float* channels[] { left, right };
            activeWriter->write (channels, numSamples);
        }
    }

    const auto position = playhead.load();
    {
        const juce::SpinLock::ScopedTryLockType lock (takeLock);
        if (lock.isLocked())
        {
            bool anySolo = false;
            for (const auto& s : trackSolo)
                anySolo = anySolo || s.load();

            for (int t = 0; t < numTracks; ++t)
            {
                const auto* take = takes[(size_t) t].get();
                // The track being recorded is replaced by the new take, so its old audio stays silent.
                if (take == nullptr || trackMuted[(size_t) t].load() || (anySolo && ! trackSolo[(size_t) t].load())
                    || (currentState == State::recording && t == recordingTrack.load()))
                    continue;

                const auto offset = position - take->start;
                const auto length = (juce::int64) take->audio.getNumSamples();
                const auto first = juce::jmax ((juce::int64) 0, -offset);
                const auto last = juce::jmin ((juce::int64) numSamples, length - offset);
                if (first >= last)
                    continue;

                const float gain = trackVolume[(size_t) t].load();
                const int n = (int) (last - first);
                const int source = (int) (offset + first);
                juce::FloatVectorOperations::addWithMultiply (left + first, take->audio.getReadPointer (0, source), gain, n);
                juce::FloatVectorOperations::addWithMultiply (right + first, take->audio.getReadPointer (1, source), gain, n);
            }
        }
    }

    if (metronomeOn.load())
        addClick (left, right, numSamples, position);

    playhead.store (position + numSamples);
}

void MultitrackRecorder::addClick (float* left, float* right, int numSamples, juce::int64 position) noexcept
{
    const double sr = sampleRate.load();
    const double samplesPerBeat = sr * 60.0 / tempo.load();
    const float decay = (float) std::exp (-1.0 / (0.012 * sr));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto sample = position + i;
        const auto beat = (juce::int64) std::floor ((double) sample / samplesPerBeat);
        if ((juce::int64) std::floor ((double) beat * samplesPerBeat) == sample)
        {
            clickEnvelope = 1.0f;
            clickPhase = 0.0f;
            clickFrequency = beat % 4 == 0 ? 1600.0f : 1000.0f; // accent on the downbeat (4/4)
        }

        if (clickEnvelope > 1.0e-4f)
        {
            const float click = 0.35f * clickEnvelope * std::sin (clickPhase);
            clickPhase += juce::MathConstants<float>::twoPi * clickFrequency / (float) sr;
            clickEnvelope *= decay;
            left[i] += click;
            right[i] += click;
        }
    }
}

//==============================================================================
void MultitrackRecorder::play()
{
    if (state.load() == State::stopped)
        state.store (State::playing);
}

void MultitrackRecorder::stop()
{
    if (state.load() == State::recording)
        finishRecording();
    state.store (State::stopped);
}

void MultitrackRecorder::setPlayhead (juce::int64 sample)
{
    if (state.load() != State::recording)
        playhead.store (juce::jmax ((juce::int64) 0, sample));
}

void MultitrackRecorder::armTrack (int track)
{
    if (state.load() != State::recording)
        armedTrack.store (juce::jlimit (-1, numTracks - 1, track));
}

juce::Result MultitrackRecorder::record()
{
    if (state.load() == State::recording)
        return juce::Result::ok();

    const int track = armedTrack.load();
    if (track < 0)
        return juce::Result::fail ("Arm a track first (R button).");

    if (! sessionFolder.createDirectory())
        return juce::Result::fail ("Cannot create " + sessionFolder.getFullPathName());

    auto file = sessionFolder.getNonexistentChildFile ("Track " + juce::String (track + 1), ".wav", false);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return juce::Result::fail ("Cannot write " + file.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate.load())
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return juce::Result::fail ("Cannot create a WAV writer.");

    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 65536);

    recordingFile = file;
    recordingTrack = track;
    recordingStart = playhead.load();
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        activeWriter = std::move (threaded);
    }
    state.store (State::recording);
    return juce::Result::ok();
}

void MultitrackRecorder::finishRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        finished = std::move (activeWriter);
    }

    if (finished == nullptr)
        return;

    state.store (State::playing); // keep the transport consistent while the file is finalised
    finished.reset();              // flushes the FIFO and closes the WAV

    // The recorded rig output arrives late by the round trip, so drop that much from the head.
    if (auto take = loadTake (recordingFile, recordingStart, latencyCompensation.load()))
        replaceTake (recordingTrack.load(), std::move (take));

    recordingTrack = -1;
}

std::unique_ptr<MultitrackRecorder::Take> MultitrackRecorder::loadTake (const juce::File& file, juce::int64 start, int trimSamples)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= trimSamples)
        return nullptr;

    auto take = std::make_unique<Take>();
    take->file = file;
    take->start = start;
    const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples - trimSamples, std::numeric_limits<int>::max());
    take->audio.setSize (2, length);
    reader->read (&take->audio, 0, length, trimSamples, true, reader->numChannels > 1);

    take->peaks.resize ((size_t) (length + samplesPerPeak - 1) / samplesPerPeak);
    for (size_t p = 0; p < take->peaks.size(); ++p)
    {
        const int from = (int) p * samplesPerPeak;
        const int n = juce::jmin (samplesPerPeak, length - from);
        take->peaks[p] = juce::jmax (take->audio.getMagnitude (0, from, n), take->audio.getMagnitude (1, from, n));
    }
    return take;
}

void MultitrackRecorder::replaceTake (int track, std::unique_ptr<Take> take)
{
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        std::swap (takes[(size_t) track], take);
    }
    // `take` now holds the previous take and is freed here, outside the lock.
}

void MultitrackRecorder::clearTrack (int track)
{
    if (state.load() == State::recording && track == recordingTrack.load())
        return;
    replaceTake (track, nullptr);
}

void MultitrackRecorder::newSession()
{
    stop();
    for (int t = 0; t < numTracks; ++t)
        replaceTake (t, nullptr);
    playhead.store (0);
    sessionFolder = recordingsRoot.getChildFile ("Session " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
}

MultitrackRecorder::TrackInfo MultitrackRecorder::getTrackInfo (int track) const
{
    TrackInfo info;
    if (const auto* take = takes[(size_t) track].get())
    {
        info.hasAudio = true;
        info.start = take->start;
        info.length = take->audio.getNumSamples();
        info.peaks = &take->peaks;
        info.fileName = take->file.getFileName();
    }
    return info;
}

juce::int64 MultitrackRecorder::getSessionLength() const
{
    juce::int64 end = 0;
    for (const auto& take : takes)
        if (take != nullptr)
            end = juce::jmax (end, take->start + take->audio.getNumSamples());
    return end;
}

juce::Result MultitrackRecorder::exportMix (juce::File& writtenFile)
{
    const auto length = getSessionLength();
    if (length <= 0)
        return juce::Result::fail ("Nothing recorded yet.");
    if (length > std::numeric_limits<int>::max())
        return juce::Result::fail ("Session too long to export.");
    if (! sessionFolder.createDirectory())
        return juce::Result::fail ("Cannot create " + sessionFolder.getFullPathName());

    bool anySolo = false;
    for (const auto& s : trackSolo)
        anySolo = anySolo || s.load();

    juce::AudioBuffer<float> mix (2, (int) length);
    mix.clear();
    for (int t = 0; t < numTracks; ++t)
    {
        const auto* take = takes[(size_t) t].get();
        if (take == nullptr || trackMuted[(size_t) t].load() || (anySolo && ! trackSolo[(size_t) t].load()))
            continue;
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, (int) take->start, take->audio, ch, 0, take->audio.getNumSamples(), trackVolume[(size_t) t].load());
    }

    // Soft safety limit so a hot mix never wraps when converted to 24-bit.
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = mix.getWritePointer (ch);
        for (int i = 0; i < mix.getNumSamples(); ++i)
            d[i] = std::abs (d[i]) <= 0.9f ? d[i] : std::copysign (0.9f + 0.1f * std::tanh ((std::abs (d[i]) - 0.9f) / 0.1f), d[i]);
    }

    writtenFile = sessionFolder.getNonexistentChildFile ("Mix", ".wav", false);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (writtenFile);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return juce::Result::fail ("Cannot write " + writtenFile.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate.load())
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples()))
        return juce::Result::fail ("Writing the mix failed.");

    return juce::Result::ok();
}
} // namespace poman
