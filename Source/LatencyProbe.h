#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

// Measures MIDI-out -> hardware synth -> audio-in round trip latency.
//
// Runs entirely on the audio thread. It emits a series of note-ons and, for
// each one, finds the first sample on the return input that crosses a
// threshold. Both timestamps live in the plugin's own sample timeline, which is
// the timeline the host's delay compensation works in, so the measured value
// can be reported as plugin latency without any further correction.
class LatencyProbe
{
public:
    static constexpr int maxPings = 32;

    struct Settings
    {
        int midiChannel = 1;    // 1-16
        int note = 60;
        int velocity = 127;     // 1-127
        int numPings = 8;
        float thresholdDb = -40.0f;
    };

    enum class Phase
    {
        idle,
        measuringNoise,
        waitingForOnset,
        waitingForSilence,
        gap,
        finished
    };

    struct Result
    {
        std::array<int64_t, maxPings> latencies {}; // samples, one per detected onset
        int numMeasured = 0;
        int numTimedOut = 0;
        float noiseFloorDb = -100.0f;
        float thresholdDb = -100.0f;
        bool tooNoisy = false;
        bool cancelled = false;
    };

    void prepare (double newSampleRate);

    // Audio thread only.
    void start (const Settings&);
    void cancel (juce::MidiBuffer& midiOut);
    void process (const float* const* input, int numInputChannels, int numSamples, juce::MidiBuffer& midiOut);

    bool isRunning() const noexcept;
    const Result& getResult() const noexcept { return result; }

    // Safe to read from any thread, for progress display.
    Phase getPhase() const noexcept { return phase.load (std::memory_order_relaxed); }
    int getPingsSent() const noexcept { return pingsSent.load (std::memory_order_relaxed); }
    int getPingsRequested() const noexcept { return settings.numPings; }

private:
    void sendNoteOff (juce::MidiBuffer& midiOut, int sampleOffset);
    void finish();

    double sampleRate = 44100.0;
    Settings settings;
    Result result;

    std::atomic<Phase> phase { Phase::idle };
    std::atomic<int> pingsSent { 0 };

    bool noteOnPending = false;
    bool noteHeld = false;
    int64_t blockStart = 0;     // absolute sample position of the current block
    int64_t noteOnTime = 0;
    int64_t phaseCounter = 0;
    int64_t quietCounter = 0;
    float noisePeak = 0.0f;
    float threshold = 0.0f;

    int64_t noiseSamples = 0;
    int64_t onsetTimeoutSamples = 0;
    int64_t quietSamples = 0;
    int64_t maxReleaseSamples = 0;
    int64_t gapSamples = 0;
};
