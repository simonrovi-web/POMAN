#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace poman
{
// Backing-track player for practice: plays a song file at 50-150 % speed without changing its pitch,
// and transposes it by up to +/-12 semitones without changing its speed, with an A-B loop.
//
// Speed: WSOLA time stretching on the source. Each 2048-sample Hann frame is taken from where the
// tempo says, nudged by up to +/-384 samples to the point that best continues the previous frame
// (cross-correlation), then overlap-added at a fixed 50 % hop, so the waveform stays continuous.
// Pitch: the stretched stream is resampled by the pitch ratio (and the file-to-engine rate ratio)
// with a 4-point Lagrange interpolator; stretching by tempo / pitch first keeps the speed.
//
// The song is decoded on the message thread into 16-bit memory (up to 15 min) and swapped in under
// a spin lock the audio thread only try-locks. Transport commands arrive through atomics.
class BackingTrack
{
public:
    static constexpr double maxMinutes = 15.0;

    // Message thread ----------------------------------------------------------------------------
    juce::Result load (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return juce::Result::fail ("Cannot read " + file.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)");
        if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60.0 * maxMinutes))
            return juce::Result::fail ("The song is longer than 15 minutes.");

        auto song = std::make_unique<Song>();
        song->rate = reader->sampleRate;
        song->length = (int) reader->lengthInSamples;
        song->left.resize ((size_t) song->length + frame);
        song->right.resize ((size_t) song->length + frame);

        constexpr int chunk = 1 << 16;
        juce::AudioBuffer<float> buffer (2, chunk);
        for (int pos = 0; pos < song->length; pos += chunk)
        {
            const int n = juce::jmin (chunk, song->length - pos);
            reader->read (&buffer, 0, n, pos, true, reader->numChannels > 1);
            if (reader->numChannels == 1)
                buffer.copyFrom (1, 0, buffer, 0, 0, n);
            for (int i = 0; i < n; ++i)
            {
                song->left[(size_t) (pos + i)] = toInt16 (buffer.getSample (0, i));
                song->right[(size_t) (pos + i)] = toInt16 (buffer.getSample (1, i));
            }
        }

        // Overview for the waveform display.
        song->overview.assign (overviewPoints, 0.0f);
        const int perPoint = juce::jmax (1, song->length / overviewPoints);
        for (int p = 0; p < overviewPoints; ++p)
        {
            int peak = 0;
            for (int i = p * perPoint; i < juce::jmin (song->length, (p + 1) * perPoint); ++i)
                peak = juce::jmax (peak, std::abs ((int) song->left[(size_t) i]), std::abs ((int) song->right[(size_t) i]));
            song->overview[(size_t) p] = (float) peak / 32767.0f;
        }

        song->file = file;
        lengthSeconds.store (song->length / song->rate);
        {
            const juce::SpinLock::ScopedLockType lock (songLock);
            current = std::move (song); // the previous song is freed while the audio thread cannot use it
            playing.store (false);
            position.store (0.0);
            seekRequest.store (0.0);
            loopStart.store (0.0);
            loopEnd.store (lengthSeconds.load());
        }
        fileName = file.getFileName();
        filePath = file;
        return juce::Result::ok();
    }

    void unload()
    {
        const juce::SpinLock::ScopedLockType lock (songLock);
        current = nullptr;
        playing.store (false);
        lengthSeconds.store (0.0);
        fileName.clear();
        filePath = juce::File();
    }

    bool isLoaded() const { return lengthSeconds.load() > 0.0; }
    juce::String getFileName() const { return fileName; }
    juce::File getFile() const { return filePath; }
    double getLengthSeconds() const { return lengthSeconds.load(); }
    // Current song overview (peaks), message thread only.
    std::vector<float> getOverview() const
    {
        const juce::SpinLock::ScopedLockType lock (songLock);
        return current != nullptr ? current->overview : std::vector<float>();
    }

    // Any thread.
    void setPlaying (bool shouldPlay) { playing.store (shouldPlay && isLoaded()); }
    bool isPlaying() const { return playing.load(); }
    void seek (double seconds) { seekRequest.store (juce::jlimit (0.0, lengthSeconds.load(), seconds)); }
    double getPositionSeconds() const { return position.load(); }
    void setSpeed (double s) { speed.store (juce::jlimit (0.5, 1.5, s)); }
    double getSpeed() const { return speed.load(); }
    void setSemitones (int s) { semitones.store (juce::jlimit (-12, 12, s)); }
    int getSemitones() const { return semitones.load(); }
    void setVolume (float v) { volume.store (juce::jlimit (0.0f, 2.0f, v)); }
    float getVolume() const { return volume.load(); }
    void setLoop (double a, double b, bool on)
    {
        loopStart.store (juce::jmin (a, b));
        loopEnd.store (juce::jmax (a, b));
        looping.store (on && std::abs (b - a) > 0.05);
    }
    bool isLooping() const { return looping.load(); }
    double getLoopStart() const { return loopStart.load(); }
    double getLoopEnd() const { return loopEnd.load(); }

    // Audio thread --------------------------------------------------------------------------------
    void prepare (double newSampleRate)
    {
        engineRate = newSampleRate;
        olaL.assign (frame, 0.0f);
        olaR.assign (frame, 0.0f);
        ringL.assign (ringSize, 0.0f);
        ringR.assign (ringSize, 0.0f);
        window.resize (frame);
        for (int k = 0; k < frame; ++k)
            window[(size_t) k] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) k / (float) frame); // periodic: sums to 1 at 50 %
        seekRequest.store (position.load()); // resume where it was, once a block runs with the song locked
    }

    // Adds the track (stereo) to left/right.
    void process (float* left, float* right, int numSamples) noexcept
    {
        const juce::SpinLock::ScopedTryLockType lock (songLock);
        if (! lock.isLocked() || current == nullptr)
            return;
        const auto& song = *current;

        if (const double s = seekRequest.exchange (-1.0); s >= 0.0)
            restart (s * song.rate);
        if (! playing.load())
            return;

        const double pitch = std::exp2 (semitones.load() / 12.0);
        const double tempo = speed.load() / pitch;              // source samples per stretched sample
        const double step = pitch * song.rate / engineRate;      // stretched samples per output sample
        const float gain = volume.load();

        for (int i = 0; i < numSamples; ++i)
        {
            // Keep four stretched samples ahead of the interpolator.
            while (written - (juce::int64) readPos < 4)
                if (! stretchHop (song, tempo))
                {
                    playing.store (false); // end of the song
                    break;
                }
            if (! playing.load())
                break;

            const auto base = (juce::int64) readPos;
            const float t = (float) (readPos - (double) base);
            left[i] += gain * lagrange (ringL, base, t);
            right[i] += gain * lagrange (ringR, base, t);
            readPos += step;
        }
        position.store (inPos / song.rate);
    }

    // For tests: the frame and search sizes in source samples.
    static constexpr int frame = 2048, hop = frame / 2, searchRange = 384;

private:
    struct Song
    {
        std::vector<std::int16_t> left, right; // padded by one frame of silence
        std::vector<float> overview;
        int length = 0;
        double rate = 44100.0;
        juce::File file;
    };

    static std::int16_t toInt16 (float x) { return (std::int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (x * 32767.0f)); }
    static float sample (const std::vector<std::int16_t>& v, int i) noexcept { return (float) v[(size_t) i] * (1.0f / 32767.0f); }

    void restart (double sourcePosition) noexcept
    {
        inPos = sourcePosition;
        prevStart = -1;
        std::fill (olaL.begin(), olaL.end(), 0.0f);
        std::fill (olaR.begin(), olaR.end(), 0.0f);
        written = 0;
        readPos = 0.0;
    }

    // One WSOLA hop: places the next frame and pushes `hop` finished samples into the ring.
    bool stretchHop (const Song& song, double tempo) noexcept
    {
        const double loopA = loopStart.load() * song.rate, loopB = loopEnd.load() * song.rate;
        if (looping.load() && inPos >= loopB)
        {
            inPos = loopA + std::fmod (inPos - loopB, juce::jmax (1.0, loopB - loopA));
            prevStart = -1; // a jump: no continuity to search for
        }
        if (inPos >= song.length)
            return false;

        const int last = song.length - 1;
        const int nominal = juce::jlimit (0, last, (int) inPos);
        int start = nominal;
        if (prevStart >= 0)
            start = bestStart (song, prevStart + hop, nominal);

        for (int k = 0; k < frame; ++k)
        {
            const int s = start + k;
            const float w = window[(size_t) k];
            olaL[(size_t) k] += w * sample (song.left, s); // the song is padded by a frame of silence
            olaR[(size_t) k] += w * sample (song.right, s);
        }
        for (int k = 0; k < hop; ++k)
        {
            ringL[(size_t) (written & ringMask)] = olaL[(size_t) k];
            ringR[(size_t) (written & ringMask)] = olaR[(size_t) k];
            ++written;
        }
        std::copy (olaL.begin() + hop, olaL.end(), olaL.begin());
        std::copy (olaR.begin() + hop, olaR.end(), olaR.begin());
        std::fill (olaL.begin() + hop, olaL.end(), 0.0f);
        std::fill (olaR.begin() + hop, olaR.end(), 0.0f);

        prevStart = start;
        inPos += hop * tempo;
        return true;
    }

    // The frame start within +/-searchRange of `nominal` whose first half best matches the natural
    // continuation at `natural` (coarse search on every 4th sample and 2nd lag, then refined).
    int bestStart (const Song& song, int natural, int nominal) const noexcept
    {
        const int lo = juce::jmax (0, nominal - searchRange), hi = juce::jmin (song.length - 1, nominal + searchRange);
        if (natural + hop >= (int) song.left.size() || hi + hop >= (int) song.left.size())
            return nominal;
        const auto correlate = [&] (int candidate, int stride)
        {
            double sum = 0.0;
            for (int k = 0; k < hop; k += stride)
                sum += ((double) song.left[(size_t) (natural + k)] + song.right[(size_t) (natural + k)])
                     * ((double) song.left[(size_t) (candidate + k)] + song.right[(size_t) (candidate + k)]);
            return sum;
        };
        int best = nominal;
        double bestScore = -1.0e300;
        for (int c = lo; c <= hi; c += 2)
            if (const double score = correlate (c, 4); score > bestScore)
                bestScore = score, best = c;
        const int coarse = best;
        bestScore = -1.0e300;
        for (int c = juce::jmax (lo, coarse - 2); c <= juce::jmin (hi, coarse + 2); ++c)
            if (const double score = correlate (c, 1); score > bestScore)
                bestScore = score, best = c;
        return best;
    }

    float lagrange (const std::vector<float>& ring, juce::int64 base, float t) const noexcept
    {
        const auto at = [&] (juce::int64 i) { return i < 0 ? 0.0f : ring[(size_t) (i & ringMask)]; };
        const float ym1 = at (base - 1), y0 = at (base), y1 = at (base + 1), y2 = at (base + 2);
        // 4-point, 3rd-order Lagrange through x = -1, 0, 1, 2, evaluated at t in [0, 1).
        return ym1 * (-t * (t - 1.0f) * (t - 2.0f) / 6.0f) + y0 * ((t + 1.0f) * (t - 1.0f) * (t - 2.0f) / 2.0f)
             + y1 * (-(t + 1.0f) * t * (t - 2.0f) / 2.0f) + y2 * ((t + 1.0f) * t * (t - 1.0f) / 6.0f);
    }

    static constexpr int overviewPoints = 2000;
    static constexpr int ringSize = 1 << 14, ringMask = ringSize - 1;

    mutable juce::SpinLock songLock;
    std::unique_ptr<Song> current;
    juce::String fileName;
    juce::File filePath;

    std::atomic<bool> playing { false }, looping { false };
    std::atomic<double> position { 0.0 }, seekRequest { -1.0 }, lengthSeconds { 0.0 }, speed { 1.0 };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<int> semitones { 0 };
    std::atomic<float> volume { 0.8f };

    double engineRate = 48000.0;
    std::vector<float> window, olaL, olaR, ringL, ringR;
    double inPos = 0.0, readPos = 0.0;
    int prevStart = -1;
    juce::int64 written = 0;
};
} // namespace poman
