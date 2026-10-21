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
    readerThread.startThread();
    newSession();
}

MultitrackRecorder::~MultitrackRecorder()
{
    finishRecording();
    saveSession();
    for (int t = 0; t < numTracks; ++t)
        replaceTake (t, nullptr); // readers must go before their thread
    readerThread.stopThread (4000);
    writerThread.stopThread (4000);
}

void MultitrackRecorder::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate.store (newSampleRate);
    playScratch.setSize (2, maxBlockSize);
    clickEnvelope = 0.0f;
}

//==============================================================================
void MultitrackRecorder::captureDi (const float* mono, int numSamples) noexcept
{
    if (state.load() != State::recording || isReamping())
        return;
    auto position = playhead.load();
    if (const auto seek = seekRequest.load(); seek != noSeek)
        position = seek; // process() applies it this block: capture on the same timeline
    const auto takeStart = recordingStart.load();
    if (position + numSamples <= takeStart)
        return;

    const juce::SpinLock::ScopedTryLockType lock (writerLock);
    if (lock.isLocked() && diWriter != nullptr)
    {
        const int skip = (int) juce::jmax ((juce::int64) 0, takeStart - position);
        const float* channels[] { mono + skip };
        diWriter->write (channels, numSamples - skip);
    }
}

bool MultitrackRecorder::readDi (float* mono, int numSamples) noexcept
{
    if (! isReamping() || state.load() != State::recording)
        return false;
    const juce::SpinLock::ScopedTryLockType lock (reampLock);
    if (! lock.isLocked() || reampReader == nullptr)
        return false;

    auto position = playhead.load();
    if (const auto seek = seekRequest.load(); seek != noSeek)
        position = seek;
    juce::FloatVectorOperations::clear (mono, numSamples);
    const auto offset = position - reampDiStart;
    const auto first = juce::jmax ((juce::int64) 0, -offset);
    const auto last = juce::jmin ((juce::int64) numSamples, reampReader->lengthInSamples - reampDiTrim - offset);
    if (first < last)
    {
        float* dest[] { mono + first };
        reampReader->read (dest, 1, reampDiTrim + offset + first, (int) (last - first));
    }
    if (position + numSamples >= reampEnd.load())
        reampDone.store (true); // the message thread stops it (pollReamp)
    return true;
}

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
            if (! activeWriter->write (channels, numSamples - skip))
                droppedSamples.fetch_add (numSamples - skip); // FIFO full: the disk is too slow

            for (int i = skip; i < numSamples; ++i)
            {
                livePeakMax = juce::jmax (livePeakMax, std::abs (left[i]), std::abs (right[i]));
                if (++livePeakCount == samplesPerPeak)
                {
                    if (livePeakFifo.getFreeSpace() > 0)
                        livePeakFifo.write (1).forEach ([this] (int index) { livePeakStorage[(size_t) index] = livePeakMax; });
                    livePeakMax = 0.0f;
                    livePeakCount = 0;
                }
            }
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
                const auto first = juce::jmax ((juce::int64) 0, -offset);
                const auto last = juce::jmin ((juce::int64) juce::jmin (numSamples, playScratch.getNumSamples()), take->length - offset);
                if (first >= last)
                    continue;

                // Reads from the read-ahead buffer; if the disk has not caught up (just after a seek)
                // this returns silence instead of waiting.
                const float gain = trackVolume[(size_t) t].load();
                const int n = (int) (last - first);
                float* dest[] { playScratch.getWritePointer (0), playScratch.getWritePointer (1) };
                take->reader->read (dest, take->fileChannels > 1 ? 2 : 1, take->trim + offset + first, n);
                if (take->fileChannels == 1)
                    juce::FloatVectorOperations::copy (dest[1], dest[0], n);
                juce::FloatVectorOperations::addWithMultiply (left + first, dest[0], gain, n);
                juce::FloatVectorOperations::addWithMultiply (right + first, dest[1], gain, n);
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

    const bool reamping = isReamping();
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

    // The clean guitar, beside the take (not when re-amping: the new take keeps using its DI).
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedDi;
    recordingDiFile = juce::File();
    if (! reamping)
    {
        const auto diFile = file.getSiblingFile (file.getFileNameWithoutExtension() + " DI.wav");
        std::unique_ptr<juce::OutputStream> diStream = std::make_unique<juce::FileOutputStream> (diFile);
        if (static_cast<juce::FileOutputStream*> (diStream.get())->openedOk())
            if (auto w = wav.createWriterFor (diStream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate (sampleRate.load())
                                                           .withNumChannels (1)
                                                           .withBitsPerSample (24)))
            {
                threadedDi = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 65536);
                recordingDiFile = diFile;
            }
    }

    recordingFile = file;
    recordingTrack = track;
    livePeaks.clear();
    livePeakFifo.reset();
    droppedSamples.store (0);
    livePeakMax = 0.0f;
    livePeakCount = 0;
    recordingStart = playhead.load();
    // The recorded rig output is late by the device round trip plus the rig; the DI, captured as it
    // enters the rig, only by the device part. Re-amping involves no device: only the rig's latency.
    recordingTrim = reamping ? pluginLatency.load() : latencyCompensation.load();
    recordingDiTrim = juce::jmax (0, recordingTrim - pluginLatency.load());

    // Pre-roll one bar: the other tracks and the click play in before the take starts.
    if (countInEnabled.load() && ! reamping)
        movePlayhead (recordingStart.load() - (juce::int64) std::round (4.0 * 60.0 / tempo.load() * sampleRate.load()));
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        activeWriter = std::move (threaded);
        diWriter = std::move (threadedDi);
    }
    state.store (State::recording);
    return juce::Result::ok();
}

void MultitrackRecorder::finishRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished, finishedDi;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        finished = std::move (activeWriter);
        finishedDi = std::move (diWriter);
    }

    if (finished == nullptr)
        return;

    state.store (State::playing); // keep the transport consistent while the file is finalised
    finished.reset();              // flushes the FIFO and closes the WAV
    finishedDi.reset();

    // Re-amping: the new take keeps the source take's DI.
    juce::File diFile = recordingDiFile;
    int diTrim = recordingDiTrim;
    if (const int source = reampTrack.exchange (-1); source >= 0)
    {
        const juce::SpinLock::ScopedLockType lock (reampLock);
        if (reampReader != nullptr)
            diFile = reampFile, diTrim = reampDiTrim;
        reampReader = nullptr;
        reampDone.store (false);
    }

    // Peaks were collected while recording; skip the ones covering the trimmed head.
    drainLivePeaks();
    if (livePeakCount > 0)
        livePeaks.push_back (livePeakMax);
    auto peaks = livePeaks;
    const auto skipPeaks = (size_t) juce::jmin ((int) peaks.size(), recordingTrim / samplesPerPeak);
    peaks.erase (peaks.begin(), peaks.begin() + (std::ptrdiff_t) skipPeaks);

    // The recorded rig output arrives late by the round trip, so drop that much from the head.
    if (auto take = loadTake (recordingFile, recordingStart.load(), recordingTrim, &peaks, diFile, diTrim))
    {
        replaceTake (recordingTrack.load(), std::move (take));
        saveSession();
    }
    else
    {
        recordingFile.deleteFile(); // stopped during the count-in: nothing was captured
        if (diFile == recordingDiFile)
            recordingDiFile.deleteFile();
    }

    recordingTrack = -1;
}

namespace
{
// <take>.wav.peaks: magic, samples per peak, trim, count, then the float peaks.
constexpr int peaksMagic = 0x504f4d50; // "POMP"

juce::File peaksFileFor (const juce::File& wav) { return wav.getSiblingFile (wav.getFileName() + ".peaks"); }
} // namespace

std::vector<float> MultitrackRecorder::readPeaks (juce::AudioFormatReader& reader, int trimSamples) const
{
    // Streams the file in chunks: memory stays bounded even for very long takes.
    std::vector<float> peaks;
    constexpr int chunk = samplesPerPeak * 128;
    juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader.numChannels), chunk);
    const auto length = reader.lengthInSamples - trimSamples;
    peaks.reserve ((size_t) (length / samplesPerPeak + 1));
    for (juce::int64 pos = 0; pos < length; pos += chunk)
    {
        const int n = (int) juce::jmin ((juce::int64) chunk, length - pos);
        reader.read (&buffer, 0, n, trimSamples + pos, true, reader.numChannels > 1);
        for (int from = 0; from < n; from += samplesPerPeak)
        {
            const int count = juce::jmin (samplesPerPeak, n - from);
            float p = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                p = juce::jmax (p, buffer.getMagnitude (ch, from, count));
            peaks.push_back (p);
        }
    }
    return peaks;
}

std::unique_ptr<MultitrackRecorder::Take> MultitrackRecorder::loadTake (const juce::File& file, juce::int64 start, int trimSamples,
                                                                        std::vector<float>* knownPeaks, const juce::File& diFile, int diTrim)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= trimSamples)
        return nullptr;

    auto take = std::make_unique<Take>();
    take->file = file;
    take->start = start;
    take->trim = trimSamples;
    take->length = reader->lengthInSamples - trimSamples;
    take->fileChannels = (int) reader->numChannels;
    if (diFile.existsAsFile())
        take->diFile = diFile, take->diTrim = diTrim;
    const auto expectedPeaks = (size_t) ((take->length + samplesPerPeak - 1) / samplesPerPeak);

    // Peaks: collected while recording, else from the .peaks cache, else scanned (and cached).
    const auto cache = peaksFileFor (file);
    if (knownPeaks != nullptr && knownPeaks->size() + 2 >= expectedPeaks && knownPeaks->size() <= expectedPeaks + 2)
    {
        take->peaks = std::move (*knownPeaks);
    }
    else if (juce::FileInputStream in (cache); in.openedOk() && in.readInt() == peaksMagic && in.readInt() == samplesPerPeak
                                                && in.readInt() == trimSamples)
    {
        const int count = in.readInt();
        if (count >= 0 && (size_t) count == expectedPeaks)
        {
            take->peaks.resize ((size_t) count);
            in.read (take->peaks.data(), count * (int) sizeof (float));
        }
    }
    if (take->peaks.empty())
        take->peaks = readPeaks (*reader, trimSamples);
    take->peaks.resize (expectedPeaks, 0.0f);

    if (! cache.existsAsFile() || knownPeaks != nullptr)
    {
        cache.deleteFile();
        if (juce::FileOutputStream out (cache); out.openedOk())
        {
            out.writeInt (peaksMagic);
            out.writeInt (samplesPerPeak);
            out.writeInt (trimSamples);
            out.writeInt ((int) take->peaks.size());
            out.write (take->peaks.data(), take->peaks.size() * sizeof (float));
        }
    }

    // About 4 s of read-ahead; reads on the audio thread never wait unless a timeout is set.
    take->reader = std::make_unique<juce::BufferingAudioReader> (reader.release(), readerThread, (int) (sampleRate.load() * 4.0));
    take->reader->setReadTimeout (readTimeoutMs);
    return take;
}

void MultitrackRecorder::drainLivePeaks()
{
    livePeakFifo.read (livePeakFifo.getNumReady()).forEach ([this] (int index) { livePeaks.push_back (livePeakStorage[(size_t) index]); });
}

const std::vector<float>& MultitrackRecorder::getLivePeaks()
{
    drainLivePeaks();
    return livePeaks;
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
            if (take->diFile != juce::File())
            {
                track->setAttribute ("di", take->diFile.getRelativePathFrom (sessionFolder));
                track->setAttribute ("diTrim", take->diTrim);
            }
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
                                                    track->getIntAttribute ("trim", 0), nullptr,
                                                    track->hasAttribute ("di") ? folder.getChildFile (track->getStringAttribute ("di")) : juce::File(),
                                                    track->getIntAttribute ("diTrim", 0))) != nullptr)
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

bool MultitrackRecorder::hasDi (int track) const
{
    const auto* take = takes[(size_t) track].get();
    return take != nullptr && take->diFile.existsAsFile();
}

juce::Result MultitrackRecorder::startReamp (int track)
{
    if (state.load() == State::recording)
        return juce::Result::fail ("Stop the recording first.");
    const auto* take = takes[(size_t) track].get();
    if (take == nullptr || ! take->diFile.existsAsFile())
        return juce::Result::fail ("This track has no DI to re-amp (record a new take first).");

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (take->diFile));
    if (reader == nullptr)
        return juce::Result::fail ("Cannot read " + take->diFile.getFileName());

    stop();
    {
        const juce::SpinLock::ScopedLockType lock (reampLock);
        reampReader = std::make_unique<juce::BufferingAudioReader> (reader.release(), readerThread, (int) (sampleRate.load() * 4.0));
        reampReader->setReadTimeout (readTimeoutMs);
        reampFile = take->diFile;
        reampDiStart = take->start;
        reampDiTrim = take->diTrim;
    }
    // Play the DI from the take's start; one extra second lets delay and reverb tails ring into the take.
    reampEnd.store (take->start + (reampReader->lengthInSamples - take->diTrim) + pluginLatency.load() + (juce::int64) sampleRate.load());
    reampDone.store (false);
    reampTrack.store (track);
    armTrack (track);
    movePlayhead (take->start);
    const auto result = record();
    if (result.failed())
    {
        reampTrack.store (-1);
        const juce::SpinLock::ScopedLockType lock (reampLock);
        reampReader = nullptr;
    }
    return result;
}

void MultitrackRecorder::pollReamp()
{
    if (reampDone.load() && state.load() == State::recording)
        stop();
}

MultitrackRecorder::TrackInfo MultitrackRecorder::getTrackInfo (int track) const
{
    TrackInfo info;
    if (const auto* take = takes[(size_t) track].get())
    {
        info.hasAudio = true;
        info.start = take->start;
        info.length = take->length;
        info.peaks = &take->peaks;
        info.fileName = take->file.getFileName();
        info.file = take->file;
        info.diFile = take->diFile;
        info.trim = take->trim;
        info.diTrim = take->diTrim;
    }
    return info;
}

juce::int64 MultitrackRecorder::getSessionLength() const
{
    juce::int64 end = 0;
    for (const auto& take : takes)
        if (take != nullptr)
            end = juce::jmax (end, take->start + take->length);
    return end;
}

juce::Result MultitrackRecorder::exportMix (juce::File& writtenFile, const ExportLayer& addLayers)
{
    const auto length = getSessionLength();
    if (length <= 0)
        return juce::Result::fail ("Nothing recorded yet.");
    if (! sessionFolder.createDirectory())
        return juce::Result::fail ("Cannot create " + sessionFolder.getFullPathName());

    bool anySolo = false;
    for (const auto& s : trackSolo)
        anySolo = anySolo || s.load();

    // Plain (non-buffering) readers: the export reads each file sequentially exactly once.
    struct Source { std::unique_ptr<juce::AudioFormatReader> reader; juce::int64 start, length; int trim, channels; float gain; };
    std::vector<Source> sources;
    for (int t = 0; t < numTracks; ++t)
    {
        const auto* take = takes[(size_t) t].get();
        if (take == nullptr || trackMuted[(size_t) t].load() || (anySolo && ! trackSolo[(size_t) t].load()))
            continue;
        if (std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (take->file)); r != nullptr)
            sources.push_back ({ std::move (r), take->start, take->length, take->trim, take->fileChannels, trackVolume[(size_t) t].load() });
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
    if (writer == nullptr)
        return juce::Result::fail ("Cannot create a WAV writer.");

    constexpr int chunk = 1 << 16;
    juce::AudioBuffer<float> mix (2, chunk), read (2, chunk);
    for (juce::int64 pos = 0; pos < length; pos += chunk)
    {
        const int n = (int) juce::jmin ((juce::int64) chunk, length - pos);
        juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), 2, n);
        view.clear();

        for (auto& s : sources)
        {
            const auto first = juce::jmax ((juce::int64) 0, s.start - pos);
            const auto last = juce::jmin ((juce::int64) n, s.start + s.length - pos);
            if (first >= last)
                continue;
            const int count = (int) (last - first);
            float* dest[] { read.getWritePointer (0), read.getWritePointer (1) };
            s.reader->read (dest, s.channels > 1 ? 2 : 1, s.trim + pos + first - s.start, count);
            if (s.channels == 1)
                juce::FloatVectorOperations::copy (dest[1], dest[0], count);
            for (int ch = 0; ch < 2; ++ch)
                view.addFrom (ch, (int) first, read, ch, 0, count, s.gain);
        }

        if (addLayers)
            addLayers (view, pos);

        // Soft safety limit so a hot mix never wraps when converted to 24-bit.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = view.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = std::abs (d[i]) <= 0.9f ? d[i] : std::copysign (0.9f + 0.1f * std::tanh ((std::abs (d[i]) - 0.9f) / 0.1f), d[i]);
        }

        if (! writer->writeFromAudioSampleBuffer (view, 0, n))
            return juce::Result::fail ("Writing the mix failed.");
    }

    return juce::Result::ok();
}
} // namespace poman
