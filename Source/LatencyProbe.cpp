#include "LatencyProbe.h"

#include <algorithm>
#include <cmath>

namespace
{
    // How far above the measured noise floor the onset threshold must sit.
    constexpr float noiseHeadroom = 4.0f; // ~ +12 dB

    // Beyond this the return is too noisy to find an onset in.
    constexpr float maxUsableThreshold = 0.5f;

    float peakAt (const float* const* input, int numChannels, int index) noexcept
    {
        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, std::abs (input[ch][index]));

        return peak;
    }
}

void LatencyProbe::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    const auto ms = [this] (double t) { return (int64_t) std::llround (t * sampleRate / 1000.0); };
    noiseSamples        = ms (300.0);
    onsetTimeoutSamples = ms (1000.0);
    quietSamples        = ms (100.0);
    maxReleaseSamples   = ms (2000.0);
    gapSamples          = ms (150.0);

    phase.store (Phase::idle);
    noteHeld = false;
    noteOnPending = false;
}

void LatencyProbe::start (const Settings& newSettings)
{
    settings = newSettings;
    settings.numPings = std::clamp (settings.numPings, 1, maxPings);

    result = {};
    pingsSent.store (0);
    noisePeak = 0.0f;
    phaseCounter = 0;
    noteOnPending = false;
    noteHeld = false;
    phase.store (Phase::measuringNoise);
}

void LatencyProbe::cancel (juce::MidiBuffer& midiOut)
{
    if (! isRunning())
        return;

    sendNoteOff (midiOut, 0);
    result.cancelled = true;
    finish();
}

bool LatencyProbe::isRunning() const noexcept
{
    const auto p = getPhase();
    return p != Phase::idle && p != Phase::finished;
}

void LatencyProbe::sendNoteOff (juce::MidiBuffer& midiOut, int sampleOffset)
{
    if (! noteHeld)
        return;

    midiOut.addEvent (juce::MidiMessage::noteOff (settings.midiChannel, settings.note), sampleOffset);
    noteHeld = false;
}

void LatencyProbe::finish()
{
    noteOnPending = false;
    phase.store (Phase::finished, std::memory_order_release);
}

void LatencyProbe::process (const float* const* input, int numInputChannels, int numSamples, juce::MidiBuffer& midiOut)
{
    if (! isRunning())
    {
        blockStart += numSamples;
        return;
    }

    if (getPhase() == Phase::measuringNoise && phaseCounter == 0)
        midiOut.addEvent (juce::MidiMessage::allNotesOff (settings.midiChannel), 0);

    // Note-ons are only ever sent at the very start of a block. Some hosts
    // quantise plugin MIDI output to block boundaries; sending at offset 0
    // means that can't skew the measurement.
    if (noteOnPending)
    {
        midiOut.addEvent (juce::MidiMessage::noteOn (settings.midiChannel, settings.note,
                                                     (juce::uint8) std::clamp (settings.velocity, 1, 127)),
                          0);
        noteOnPending = false;
        noteHeld = true;
        noteOnTime = blockStart;
        pingsSent.fetch_add (1);
        phase.store (Phase::waitingForOnset);
    }

    for (int i = 0; i < numSamples && isRunning(); ++i)
    {
        const float peak = numInputChannels > 0 ? peakAt (input, numInputChannels, i) : 0.0f;

        switch (getPhase())
        {
            case Phase::measuringNoise:
                noisePeak = std::max (noisePeak, peak);

                if (++phaseCounter >= noiseSamples)
                {
                    threshold = std::max (juce::Decibels::decibelsToGain (settings.thresholdDb),
                                          noisePeak * noiseHeadroom);

                    result.noiseFloorDb = juce::Decibels::gainToDecibels (noisePeak);
                    result.thresholdDb = juce::Decibels::gainToDecibels (threshold);

                    if (threshold > maxUsableThreshold)
                    {
                        result.tooNoisy = true;
                        finish();
                    }
                    else
                    {
                        noteOnPending = true;
                        phase.store (Phase::gap);
                        phaseCounter = gapSamples; // fire at the next block start
                    }
                }
                break;

            case Phase::waitingForOnset:
            {
                const auto elapsed = blockStart + i - noteOnTime;

                if (peak >= threshold || elapsed > onsetTimeoutSamples)
                {
                    if (peak >= threshold)
                        result.latencies[(size_t) result.numMeasured++] = elapsed;
                    else
                        ++result.numTimedOut;

                    sendNoteOff (midiOut, i);
                    phaseCounter = 0;
                    quietCounter = 0;
                    phase.store (Phase::waitingForSilence);
                }
                break;
            }

            case Phase::waitingForSilence:
                ++phaseCounter;
                quietCounter = peak < threshold * 0.5f ? quietCounter + 1 : 0;

                if (quietCounter >= quietSamples || phaseCounter >= maxReleaseSamples)
                {
                    phaseCounter = 0;
                    phase.store (Phase::gap);
                }
                break;

            case Phase::gap:
                if (! noteOnPending && ++phaseCounter >= gapSamples)
                {
                    if (pingsSent.load() >= settings.numPings)
                        finish();
                    else
                        noteOnPending = true;
                }
                break;

            case Phase::idle:
            case Phase::finished:
                break;
        }
    }

    blockStart += numSamples;
}
