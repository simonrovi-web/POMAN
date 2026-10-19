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
    saveSession();
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

    // Seeks from the message thread are applied here, so they cannot race with the playhead advance.
    auto position = playhead.load();
    if (const auto seek = seekRequest.exchange (noSeek); seek != noSeek)
        position = seek;
    const auto takeStart = recordingStart.load();

    // During the count-in the playhead runs up to takeStart; only samples from there on are recorded.
    if (currentState == State::recording && position + numSamples > takeStart)
    {
        const juce::SpinLock::ScopedTryLockType lock (writerLock);
        if (lock.isLocked() && activeWriter != nullptr)
        {
            const int skip = (int) juce::jmax ((juce::int64) 0, takeStart - position);
            const float* channels[] { left + skip, right + skip };
            activeWriter->write (channels, numSamples - skip);
        }
    }

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

    // Count-in clicks are counted back from the take start; the metronome follows the session grid.
    const int countInEnd = currentState == State::recording
                               ? (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, takeStart - position)
                               : 0;
    if (countInEnd > 0)
        addClick (left, right, 0, countInEnd, position, takeStart);
    if (metronomeOn.load())
        addClick (left, right, countInEnd, numSamples, position, 0);

    playhead.store (position + numSamples);
}

void MultitrackRecorder::addClick (float* left, float* right, int from, int to, juce::int64 position, juce::int64 origin) noexcept
{
    const double sr = sampleRate.load();
    const double samplesPerBeat = sr * 60.0 / tempo.load();
    const float decay = (float) std::exp (-1.0 / (0.012 * sr));

    for (int i = from; i < to; ++i)
    {
        const auto sample = position + i - origin;
        const auto beat = (juce::int64) std::floor ((double) sample / samplesPerBeat);
        if ((juce::int64) std::floor ((double) beat * samplesPerBeat) == sample)
        {
            clickEnvelope = 1.0f;
            clickPhase = 0.0f;
            clickFrequency = ((beat % 4) + 4) % 4 == 0 ? 1600.0f : 1000.0f; // accent on the downbeat (4/4)
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

    // Stopping inside a count-in from 0:00 leaves the playhead in the pre-roll.
    if (playhead.load() < 0)
        movePlayhead (0);
}

void MultitrackRecorder::setPlayhead (juce::int64 sample)
{
    if (state.load() != State::recording)
        movePlayhead (juce::jmax ((juce::int64) 0, sample));
}

void MultitrackRecorder::movePlayhead (juce::int64 sample)
{
    if (state.load() == State::stopped)
    {
        seekRequest.store (noSeek);
        playhead.store (sample);
    }
    else
    {
        seekRequest.store (sample);
    }
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

    if (! sessionOwned)
    {
        if (sessionFolder.exists())
            sessionFolder = sessionFolder.getParentDirectory().getNonexistentChildFile (sessionFolder.getFileName(), {}, false);
        if (! sessionFolder.createDirectory())
            return juce::Result::fail ("Cannot create " + sessionFolder.getFullPathName());
        sessionOwned = true;
    }

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
    recordingTrim = latencyCompensation.load();

    // Pre-roll one bar: the other tracks and the click play in before the take starts.
    if (countInEnabled.load())
        movePlayhead (recordingStart.load() - (juce::int64) std::round (4.0 * 60.0 / tempo.load() * sampleRate.load()));
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
    if (auto take = loadTake (recordingFile, recordingStart.load(), recordingTrim))
    {
        replaceTake (recordingTrack.load(), std::move (take));
        saveSession();
    }
    else
    {
        recordingFile.deleteFile(); // stopped during the count-in: nothing was captured
    }

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
    take->trim = trimSamples;
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
    saveSession();
    for (int t = 0; t < numTracks; ++t)
        replaceTake (t, nullptr);
    movePlayhead (0);
    sessionOwned = false;
    sessionFolder = recordingsRoot.getChildFile ("Session " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
}

void MultitrackRecorder::saveSession() const
{
    if (! sessionOwned || ! sessionFolder.isDirectory())
        return; // nothing recorded or opened yet

    juce::XmlElement root ("POMANSession");
    root.setAttribute ("version", 1);
    root.setAttribute ("bpm", tempo.load());
    root.setAttribute ("metronome", metronomeOn.load());
    root.setAttribute ("countIn", countInEnabled.load());

    for (int t = 0; t < numTracks; ++t)
    {
        auto* track = root.createNewChildElement ("Track");
        track->setAttribute ("index", t);
        track->setAttribute ("volume", trackVolume[(size_t) t].load());
        track->setAttribute ("muted", trackMuted[(size_t) t].load());
        track->setAttribute ("solo", trackSolo[(size_t) t].load());
        if (const auto* take = takes[(size_t) t].get())
        {
            track->setAttribute ("file", take->file.getRelativePathFrom (sessionFolder));
            track->setAttribute ("start", juce::String (take->start));
            track->setAttribute ("trim", take->trim);
        }
    }

    root.writeTo (sessionFolder.getChildFile ("session.xml"));
}

juce::Result MultitrackRecorder::openSession (const juce::File& folder)
{
    if (! folder.isDirectory())
        return juce::Result::fail ("Not a folder: " + folder.getFullPathName());

    stop();
    saveSession();
    std::array<std::unique_ptr<Take>, numTracks> loaded;
    int found = 0;

    if (const auto xml = juce::parseXML (folder.getChildFile ("session.xml")); xml != nullptr && xml->hasTagName ("POMANSession"))
    {
        setBpm (xml->getDoubleAttribute ("bpm", tempo.load()));
        metronomeOn.store (xml->getBoolAttribute ("metronome", false));
        countInEnabled.store (xml->getBoolAttribute ("countIn", true));

        for (auto* track : xml->getChildWithTagNameIterator ("Track"))
        {
            const int t = track->getIntAttribute ("index", -1);
            if (t < 0 || t >= numTracks)
                continue;

            trackVolume[(size_t) t].store ((float) track->getDoubleAttribute ("volume", 0.8));
            trackMuted[(size_t) t].store (track->getBoolAttribute ("muted", false));
            trackSolo[(size_t) t].store (track->getBoolAttribute ("solo", false));

            const auto fileName = track->getStringAttribute ("file");
            if (fileName.isNotEmpty())
                if ((loaded[(size_t) t] = loadTake (folder.getChildFile (fileName), track->getStringAttribute ("start").getLargeIntValue(),
                                                    track->getIntAttribute ("trim", 0))) != nullptr)
                    ++found;
        }
    }
    else
    {
        for (int t = 0; t < numTracks; ++t)
        {
            auto candidates = folder.findChildFiles (juce::File::findFiles, false, "Track " + juce::String (t + 1) + "*.wav");
            std::sort (candidates.begin(), candidates.end(),
                       [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
            if (! candidates.isEmpty() && (loaded[(size_t) t] = loadTake (candidates.getFirst(), 0, 0)) != nullptr)
                ++found;
        }

        if (found == 0)
            return juce::Result::fail ("No POMAN session or Track WAV files in " + folder.getFileName());
    }

    for (int t = 0; t < numTracks; ++t)
        replaceTake (t, std::move (loaded[(size_t) t]));

    sessionFolder = folder;
    sessionOwned = true;
    movePlayhead (0);
    return juce::Result::ok();
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

juce::Result MultitrackRecorder::exportMix (juce::File& writtenFile, const std::function<void (juce::AudioBuffer<float>&)>& addLayers)
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

    if (addLayers)
        addLayers (mix);

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
