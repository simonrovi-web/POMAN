// Offline checks of the multitrack recorder: record, latency trim, playback, mute/solo, overdub, export, click.
#include "recorder/MultitrackRecorder.h"
#include <cstdio>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")
#endif

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
int failures = 0;

void expect (bool ok, const char* label, const juce::String& detail = {})
{
    failures += ok ? 0 : 1;
    std::printf ("%s  %-44s %s\n", ok ? "PASS" : "FAIL", label, detail.toRawUTF8());
}

// Runs the recorder like the audio thread would; `source` fills the rig output for each block.
template <typename Source>
juce::AudioBuffer<float> run (poman::MultitrackRecorder& r, int numSamples, Source&& source)
{
    juce::AudioBuffer<float> out (2, numSamples);
    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int n = juce::jmin (blockSize, numSamples - start);
        auto* l = out.getWritePointer (0, start);
        auto* rr = out.getWritePointer (1, start);
        for (int i = 0; i < n; ++i)
            l[i] = rr[i] = source (start + i);
        r.process (l, rr, n);
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& b, int from, int n) { return b.getRMSLevel (0, from, n); }
} // namespace

int main()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("POMANRecorderTest");
    root.deleteRecursively();

    {
        poman::MultitrackRecorder recorder (root);
        recorder.prepare (sampleRate, blockSize);
        recorder.setReadTimeoutMs (2000); // offline: let playback wait for the disk
        recorder.setLatencyCompensation (1000);
        recorder.setCountIn (false); // sections 1-7 record immediately; the count-in has its own checks below

        // 1) Record one second on track 1: silence, then an impulse 1000 samples in, then a sine.
        recorder.armTrack (0);
        expect (recorder.record().wasOk(), "record() starts on armed track");
        run (recorder, (int) sampleRate, [] (int i)
        {
            if (i == 1000) return 1.0f;
            return i > 4000 ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate) : 0.0f;
        });
        recorder.stop();

        const auto info = recorder.getTrackInfo (0);
        expect (info.hasAudio, "take loaded after stop");
        expect (info.length == (juce::int64) sampleRate - 1000, "take trimmed by latency compensation",
                juce::String (info.length) + " samples");
        expect (root.findChildFiles (juce::File::findFiles, true, "*.wav").size() == 1, "WAV file written to session folder");
        // Waveform peaks come from the recording itself and are cached beside the WAV.
        const auto expectedPeaks = (size_t) ((info.length + poman::MultitrackRecorder::samplesPerPeak - 1) / poman::MultitrackRecorder::samplesPerPeak);
        expect (info.peaks != nullptr && info.peaks->size() == expectedPeaks && root.findChildFiles (juce::File::findFiles, true, "*.peaks").size() == 1,
                "live peaks kept and cached (.peaks)", juce::String (info.peaks != nullptr ? (int) info.peaks->size() : -1) + " peaks");

        // 2) Playback: the impulse recorded 1000 samples late must now sit at sample 0.
        recorder.rewind();
        recorder.play();
        auto played = run (recorder, (int) sampleRate, [] (int) { return 0.0f; });
        recorder.stop();
        const float vol = recorder.getVolume (0);
        expect (std::abs (played.getSample (0, 0) - vol) < 1.0e-3f, "latency-aligned impulse plays at sample 0",
                juce::String (played.getSample (0, 0), 3));
        expect (rms (played, 10000, 20000) > 0.2f, "recorded sine plays back", juce::String (rms (played, 10000, 20000), 3));

        // 3) Mute and solo.
        recorder.setMuted (0, true);
        recorder.rewind();
        recorder.play();
        auto muted = run (recorder, 20000, [] (int) { return 0.0f; });
        recorder.stop();
        expect (muted.getMagnitude (0, 0, 20000) == 0.0f, "muted track is silent");
        recorder.setMuted (0, false);

        recorder.setSolo (1, true);
        recorder.rewind();
        recorder.play();
        auto soloOther = run (recorder, 20000, [] (int) { return 0.0f; });
        recorder.stop();
        expect (soloOther.getMagnitude (0, 0, 20000) == 0.0f, "solo on another track silences track 1");
        recorder.setSolo (1, false);

        // 4) Overdub on track 2 while track 1 plays; recording only captures the rig, not playback.
        recorder.rewind();
        recorder.armTrack (1);
        recorder.record();
        auto monitored = run (recorder, (int) sampleRate, [] (int i) { return 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate); });
        recorder.stop();
        expect (rms (monitored, 10000, 20000) > 0.3f, "track 1 audible while overdubbing", juce::String (rms (monitored, 10000, 20000), 3));
        const auto take2 = recorder.getTrackInfo (1);
        expect (take2.hasAudio && take2.start == 0, "overdub take placed at record position");

        // Re-recording track 2 must not play its old take underneath.
        recorder.rewind();
        recorder.record();
        auto rerecord = run (recorder, 20000, [] (int) { return 0.0f; });
        recorder.stop();
        recorder.setMuted (0, true);
        recorder.rewind();
        recorder.play();
        auto afterRerecord = run (recorder, 20000, [] (int) { return 0.0f; });
        recorder.stop();
        recorder.setMuted (0, false);
        expect (afterRerecord.getMagnitude (0, 0, 20000) < 1.0e-4f, "new take replaces the old one on the same track");
        juce::ignoreUnused (rerecord);

        // 5) Export mix.
        juce::File mixFile;
        const auto exported = recorder.exportMix (mixFile);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (mixFile));
        expect (exported.wasOk() && reader != nullptr && reader->numChannels == 2
                    && reader->lengthInSamples == recorder.getSessionLength(),
                "exported Mix.wav has session length", mixFile.getFileName());

        // 5b) Extra layers (the drum machine) are mixed into the export.
        {
            juce::File layered;
            bool called = false;
            const auto res = recorder.exportMix (layered, [&] (juce::AudioBuffer<float>& m, juce::int64 position)
            {
                called = true;
                m.clear();
                if (position == 0)
                {
                    m.setSample (0, 10, 0.5f);
                    m.setSample (1, 10, 0.5f);
                }
            });
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (layered));
            juce::AudioBuffer<float> back (2, 200);
            if (r != nullptr)
                r->read (&back, 0, 200, 0, true, true);
            expect (res.wasOk() && called && std::abs (back.getSample (0, 10) - 0.5f) < 1.0e-3f && back.getSample (0, 100) == 0.0f,
                    "export mix includes extra layers");
        }

        // 6) Metronome adds a click on the downbeat when playing.
        recorder.clearTrack (0);
        recorder.clearTrack (1);
        recorder.setMetronome (true);
        recorder.rewind();
        recorder.play();
        auto click = run (recorder, 4000, [] (int) { return 0.0f; });
        recorder.stop();
        expect (click.getMagnitude (0, 0, 2000) > 0.1f, "metronome click on beat 1");

        // 7) Nothing happens while stopped.
        const auto playheadBefore = recorder.getPlayhead();
        auto idle = run (recorder, 4000, [] (int) { return 0.0f; });
        expect (idle.getMagnitude (0, 0, 4000) == 0.0f && recorder.getPlayhead() == playheadBefore,
                "stopped transport leaves output and playhead untouched");
    }

    // 8) Count-in: one bar (2 s at 120 BPM) of click with nothing recorded, then the take starts at 0:00.
    {
        poman::MultitrackRecorder recorder (root);
        recorder.prepare (sampleRate, blockSize);
        recorder.setReadTimeoutMs (2000); // offline: let playback wait for the disk
        recorder.setLatencyCompensation (1000);
        recorder.setBpm (120.0);
        recorder.setCountIn (true);
        recorder.setMetronome (false);
        recorder.setVolume (0, 0.5f);
        recorder.armTrack (0);
        recorder.record();

        const int bar = (int) sampleRate * 2;
        bool countingMidway = false;
        auto output = run (recorder, bar + (int) sampleRate, [&] (int i)
        {
            if (i == bar / 2)
                countingMidway = recorder.isCountingIn();
            if (i < bar) return 0.3f; // anything played during the count-in must not be recorded
            return i == bar + 1000 ? 1.0f : 0.0f;
        });
        expect (countingMidway, "isCountingIn() during the pre-roll bar");
        expect (output.getMagnitude (0, 0, 2000) > 0.4f, "count-in clicks even with CLICK off");
        recorder.stop();

        const auto info = recorder.getTrackInfo (0);
        expect (info.hasAudio && info.start == 0 && info.length == (juce::int64) sampleRate - 1000,
                "take starts at the record position after count-in", juce::String (info.length) + " samples");

        recorder.rewind();
        recorder.play();
        auto played = run (recorder, 4000, [] (int) { return 0.0f; });
        recorder.stop();
        expect (std::abs (played.getSample (0, 0) - 0.5f) < 1.0e-3f && played.getMagnitude (0, 10, 3000) < 1.0e-4f,
                "only post-count-in audio recorded, impulse at 0");

        // Stop inside the count-in: no take, no file, playhead back at 0.
        const auto filesBefore = root.findChildFiles (juce::File::findFiles, true, "*.wav").size();
        recorder.armTrack (1);
        recorder.record();
        run (recorder, bar / 2, [] (int) { return 0.3f; });
        recorder.stop();
        expect (! recorder.getTrackInfo (1).hasAudio && recorder.getPlayhead() == 0
                    && root.findChildFiles (juce::File::findFiles, true, "*.wav").size() == filesBefore,
                "stop during count-in discards the take");

        // 9) Session file: reopen in a fresh recorder (as after restarting the app).
        recorder.setMuted (0, true);
        recorder.saveSession();
        const auto folder = recorder.getSessionFolder();

        poman::MultitrackRecorder reopened (root);
        reopened.prepare (sampleRate, blockSize);
        reopened.setReadTimeoutMs (2000);
        const auto opened = reopened.openSession (folder);
        const auto again = reopened.getTrackInfo (0);
        expect (opened.wasOk() && again.hasAudio && again.start == info.start && again.length == info.length,
                "openSession restores takes with position and trim");
        expect (std::abs (reopened.getVolume (0) - 0.5f) < 1.0e-6f && reopened.isMuted (0) && reopened.isCountInEnabled(),
                "openSession restores mixer and count-in");
    }

    // 10) Folders recorded by 0.5.0 have no session.xml: load each track's WAV at 0:00.
    {
        const auto legacy = root.getChildFile ("Legacy");
        legacy.createDirectory();
        juce::AudioBuffer<float> tone (2, 4800);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.2f), tone.setSample (1, i, 0.2f);
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (legacy.getChildFile ("Track 2.wav"));
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        }

        poman::MultitrackRecorder recorder (root);
        recorder.prepare (sampleRate, blockSize);
        recorder.setReadTimeoutMs (2000); // offline: let playback wait for the disk
        const auto opened = recorder.openSession (legacy);
        const auto info = recorder.getTrackInfo (1);
        expect (opened.wasOk() && info.hasAudio && info.start == 0 && info.length == 4800 && ! recorder.getTrackInfo (0).hasAudio,
                "0.5.0 folder without session.xml opens");
        expect (recorder.openSession (root.getChildFile ("Nope")).failed(), "missing folder is reported");
    }

    // 11) A 10-minute take must not be loaded into RAM (it used to cost ~230 MB as floats).
    {
        const auto workingSetMb = []
        {
           #if JUCE_WINDOWS
            PROCESS_MEMORY_COUNTERS counters {};
            GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters));
            return (double) counters.WorkingSetSize / (1024.0 * 1024.0);
           #else
            return 0.0;
           #endif
        };

        poman::MultitrackRecorder recorder (root);
        recorder.prepare (sampleRate, blockSize);
        recorder.setReadTimeoutMs (2000);
        recorder.setCountIn (false);
        recorder.armTrack (0);
        recorder.record();

        const int total = (int) sampleRate * 600;
        juce::AudioBuffer<float> block (2, blockSize);
        juce::Random rng (11);
        for (int pos = 0, n = 0; pos < total; pos += blockSize, ++n)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    block.setSample (ch, i, 0.1f * (rng.nextFloat() - 0.5f));
            recorder.process (block.getWritePointer (0), block.getWritePointer (1), blockSize);
            if (n % 8 == 7)
                juce::Thread::sleep (1); // offline is far faster than real time: let the disk writer keep up
        }

        const double before = workingSetMb();
        recorder.stop(); // finalises the WAV and opens the take for playback
        recorder.rewind();
        recorder.play();
        for (int i = 0; i < 400; ++i) // ~2 s of playback
            recorder.process (block.getWritePointer (0), block.getWritePointer (1), blockSize);
        recorder.stop();
        const double grown = workingSetMb() - before;

        const auto info = recorder.getTrackInfo (0);
        const bool ok = info.hasAudio && recorder.getDroppedSamples() == 0 && info.length >= (juce::int64) total && grown < 40.0;
        expect (ok, "10-min take streams from disk",
                juce::String (info.length / sampleRate, 1) + " s, dropped " + juce::String (recorder.getDroppedSamples())
                    + ", memory +" + juce::String (grown, 1) + " MB");
    }

    root.deleteRecursively();
    return failures;
}
