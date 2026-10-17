// Offline check of LatencyProbe: simulates a hardware synth with a known round
// trip and verifies the probe measures it exactly, across varying block sizes.

#include "../Source/LatencyProbe.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
    struct SimulatedSynth
    {
        int64_t delay = 0;              // round trip in samples
        float level = 0.3f;
        float noiseLevel = 0.0003f;     // ~ -70 dBFS
        std::vector<std::pair<int64_t, bool>> events; // (time sound changes, note on?)
        std::mt19937 rng { 1234 };

        void receive (const juce::MidiBuffer& midi, int64_t blockStart)
        {
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();

                if (msg.isNoteOn())
                    events.emplace_back (blockStart + meta.samplePosition + delay, true);
                else if (msg.isNoteOff())
                    events.emplace_back (blockStart + meta.samplePosition + delay, false);
            }
        }

        void render (float* out, int numSamples, int64_t blockStart)
        {
            std::uniform_real_distribution<float> noise (-noiseLevel, noiseLevel);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto t = blockStart + i;
                bool on = false;

                for (const auto& [time, isOn] : events)
                    if (time <= t)
                        on = isOn;

                // Square wave so the onset sample itself is at full level.
                out[i] = noise (rng) + (on ? (((t / 50) % 2) ? level : -level) : 0.0f);
            }
        }
    };

    bool runCase (double sampleRate, int64_t delay, const std::vector<int>& blockSizes)
    {
        LatencyProbe probe;
        probe.prepare (sampleRate);

        LatencyProbe::Settings settings;
        settings.numPings = 5;
        probe.start (settings);

        SimulatedSynth synth;
        synth.delay = delay;

        std::vector<float> input (4096);
        juce::MidiBuffer midi;
        int64_t blockStart = 0;
        size_t blockIndex = 0;

        while (probe.isRunning() && blockStart < (int64_t) (sampleRate * 60))
        {
            const int n = blockSizes[blockIndex++ % blockSizes.size()];
            synth.render (input.data(), n, blockStart);

            const float* channels[] = { input.data() };
            midi.clear();
            probe.process (channels, 1, n, midi);
            synth.receive (midi, blockStart);

            blockStart += n;
        }

        const auto& r = probe.getResult();
        bool ok = ! probe.isRunning() && r.numMeasured == settings.numPings && r.numTimedOut == 0;

        for (int i = 0; i < r.numMeasured; ++i)
            ok = ok && r.latencies[(size_t) i] == delay;

        std::printf ("%s  sr=%.0f delay=%lld measured=%d/%d first=%lld\n",
                     ok ? "PASS" : "FAIL", sampleRate, (long long) delay, r.numMeasured, settings.numPings,
                     r.numMeasured > 0 ? (long long) r.latencies[0] : -1LL);
        return ok;
    }
}

int main()
{
    bool ok = true;
    ok &= runCase (48000.0, 1234, { 256 });
    ok &= runCase (44100.0, 3001, { 64, 512, 100, 33 });
    ok &= runCase (96000.0, 20000, { 1024 });
    // A real round trip can never be shorter than one block, since a block's
    // input is captured before it is processed. Exactly one block is the edge.
    ok &= runCase (48000.0, 128, { 128 });
    ok &= runCase (48000.0, 129, { 128 });
    return ok ? 0 : 1;
}
