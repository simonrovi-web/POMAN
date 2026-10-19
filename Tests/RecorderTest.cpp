// Offline checks of the multitrack recorder: record, latency trim, playback, mute/solo, overdub, export, click.
#include "recorder/MultitrackRecorder.h"
#include <cstdio>

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
        recorder.prepare (sampleRate);
        recorder.setLatencyCompensation (1000);

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

    root.deleteRecursively();
    return failures;
}
