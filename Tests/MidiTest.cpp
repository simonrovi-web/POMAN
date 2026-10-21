// MidiRouter checks: learn, footswitch edges, continuous CCs, program change, uniqueness, bursts, state.
#include "MidiRouter.h"
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void expect (bool ok, const char* label, const juce::String& detail = {})
{
    failures += ok ? 0 : 1;
    std::printf ("%s  %-38s %s\n", ok ? "PASS" : "FAIL", label, detail.toRawUTF8());
}

struct Hit { juce::String target; float value; bool pressed; };

struct Capture
{
    std::vector<Hit> hits;
    std::vector<int> programs;
    void run (poman::MidiRouter& r)
    {
        r.dispatch ([this] (const juce::String& t, float v, bool p) { hits.push_back ({ t, v, p }); },
                    [this] (int program) { programs.push_back (program); });
    }
};

juce::MidiBuffer cc (int number, int value)
{
    juce::MidiBuffer b;
    b.addEvent (juce::MidiMessage::controllerEvent (1, number, value), 0);
    return b;
}
} // namespace

int main()
{
    {
        poman::MidiRouter r;
        Capture c;
        r.learn ("ampGain");
        r.push (cc (7, 30));
        c.run (r);
        expect (r.getCcFor ("ampGain") == 7 && ! r.isLearning() && c.hits.empty(), "learn assigns CC, gesture not applied");

        r.push (cc (7, 127));
        r.push (cc (7, 64));
        c.run (r);
        expect (c.hits.size() == 2 && c.hits[0].target == "ampGain" && c.hits[0].value == 1.0f
                    && std::abs (c.hits[1].value - 64.0f / 127.0f) < 1.0e-6f,
                "continuous CC drives the parameter");
    }

    {
        poman::MidiRouter r;
        Capture c;
        r.assign ("action:tuner", 64);
        for (int v : { 127, 0, 127, 100, 0 })
            r.push (cc (64, v));
        c.run (r);
        int presses = 0;
        for (const auto& h : c.hits)
            presses += h.pressed ? 1 : 0;
        expect (presses == 2, "footswitch fires once per press", juce::String (presses) + " presses for 127,0,127,100,0");
    }

    {
        poman::MidiRouter r;
        Capture c;
        juce::MidiBuffer pc;
        pc.addEvent (juce::MidiMessage::programChange (1, 3), 0);
        r.push (pc);
        c.run (r);
        expect (c.programs.size() == 1 && c.programs[0] == 3, "program change reported");
    }

    {
        poman::MidiRouter r;
        r.assign ("a", 10);
        r.assign ("b", 10); // the CC moves to b
        r.assign ("b", 11); // b moves to CC 11
        expect (r.getCcFor ("a") == -1 && r.getCcFor ("b") == 11, "one CC per target, one target per CC");

        poman::MidiRouter copy;
        r.assign ("action:presetNext", 80);
        copy.fromValueTree (r.toValueTree());
        expect (copy.getCcFor ("b") == 11 && copy.getCcFor ("action:presetNext") == 80, "mappings survive save/restore");
    }

    {
        // A fast expression-pedal sweep: 200 messages between two message-thread ticks.
        poman::MidiRouter r;
        Capture c;
        r.assign ("output", 11);
        for (int i = 0; i < 200; ++i)
            r.push (cc (11, i % 128));
        c.run (r);
        expect (c.hits.size() == 200 && c.hits.back().value == (float) (199 % 128) / 127.0f, "bursts are not truncated",
                juce::String ((int) c.hits.size()) + " of 200 delivered");
    }

    return failures;
}
