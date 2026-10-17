#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ParamIds
{
    constexpr auto midiChannel = "midiChannel";
    constexpr auto pingNote = "pingNote";
    constexpr auto pingVelocity = "pingVelocity";
    constexpr auto numPings = "numPings";
    constexpr auto thresholdDb = "thresholdDb";
    constexpr auto offsetMs = "offsetMs";
}

namespace StateIds
{
    const juce::Identifier measuredLatency { "measuredLatency" };
    const juce::Identifier measuredSampleRate { "measuredSampleRate" };
    const juce::Identifier middleCOctave { "middleCOctave" };
    const juce::Identifier useFlats { "useFlats" };
}

namespace
{
    // The octave each host shows middle C (note 60) in, as a starting point.
    int hostMiddleCOctave()
    {
        const juce::PluginHostType host;

        if (host.isFruityLoops())
            return 5;

        if (host.isReaper() || host.isRenoise())
            return 4;

        if (host.isAbletonLive() || host.isBitwigStudio() || host.isSteinberg() || host.isLogic()
            || host.isProTools() || host.isStudioOne())
            return 3;

        return 4; // scientific pitch notation
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const TareProcessor& processor)
    {
        using namespace juce;

        const auto pingNoteAttributes = AudioParameterIntAttributes()
            .withStringFromValueFunction ([&processor] (int note, int) { return processor.getNoteName (note); })
            .withValueFromStringFunction ([&processor] (const String& text)
            {
                return processor.parseNoteName (text).value_or (text.getIntValue());
            });

        return {
            std::make_unique<AudioParameterInt> (ParameterID { ParamIds::midiChannel, 1 }, "Ping MIDI channel", 1, 16, 1),
            std::make_unique<AudioParameterInt> (ParameterID { ParamIds::pingNote, 1 }, "Ping note", 0, 127, 60,
                                                 pingNoteAttributes),
            std::make_unique<AudioParameterInt> (ParameterID { ParamIds::pingVelocity, 1 }, "Ping velocity", 1, 127, 127),
            std::make_unique<AudioParameterInt> (ParameterID { ParamIds::numPings, 1 }, "Number of pings", 1, LatencyProbe::maxPings, 8),
            std::make_unique<AudioParameterFloat> (ParameterID { ParamIds::thresholdDb, 1 }, "Onset threshold",
                                                   NormalisableRange<float> (-80.0f, -6.0f, 0.5f), -40.0f,
                                                   AudioParameterFloatAttributes().withLabel ("dB")),
            std::make_unique<AudioParameterFloat> (ParameterID { ParamIds::offsetMs, 1 }, "Latency trim",
                                                   NormalisableRange<float> (-50.0f, 50.0f, 0.01f), 0.0f,
                                                   AudioParameterFloatAttributes().withLabel ("ms")),
        };
    }

    int intParam (const juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return (int) std::lround (state.getRawParameterValue (id)->load());
    }

    juce::String formatSamples (double samples, double sampleRate)
    {
        const auto ms = sampleRate > 0.0 ? samples * 1000.0 / sampleRate : 0.0;
        return juce::String (juce::roundToInt (samples)) + " samples (" + juce::String (ms, 2) + " ms)";
    }
}

TareProcessor::TareProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Synth Return", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // State tag predates the rename to Tare. Keep it, or saved projects
      // lose their calibration.
      middleCOctave (hostMiddleCOctave()),
      parameters (*this, nullptr, "AudioPatcher", createParameterLayout (*this))
{
    startTimerHz (15);
}

TareProcessor::~TareProcessor()
{
    stopTimer();
}

bool TareProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getChannelSet (true, 0);

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void TareProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    probe.prepare (sampleRate);
    startRequested.store (false);
    cancelRequested.store (false);

    setLatencySamples (computeReportedLatency());
}

void TareProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (cancelRequested.exchange (false) && probe.isRunning())
    {
        midi.clear();
        probe.cancel (midi);
        resultReady.store (true, std::memory_order_release);
    }

    if (startRequested.exchange (false))
    {
        LatencyProbe::Settings settings;
        settings.midiChannel = intParam (parameters, ParamIds::midiChannel);
        settings.note = intParam (parameters, ParamIds::pingNote);
        settings.velocity = intParam (parameters, ParamIds::pingVelocity);
        settings.numPings = intParam (parameters, ParamIds::numPings);
        settings.thresholdDb = parameters.getRawParameterValue (ParamIds::thresholdDb)->load();
        probe.start (settings);
    }

    if (probe.isRunning())
    {
        // While measuring, incoming MIDI is swallowed so only the pings reach
        // the synth, and the output is muted.
        midi.clear();
        probe.process (buffer.getArrayOfReadPointers(), numIn, numSamples, midi);

        if (! probe.isRunning())
            resultReady.store (true, std::memory_order_release);

        buffer.clear();
        return;
    }

    // Normal operation: MIDI passes through untouched, and the synth return is
    // passed to the output. The buffer is processed in place, so input channels
    // are already sitting in the output channels.
    if (numIn == 0)
    {
        buffer.clear();
    }
    else
    {
        for (int ch = numIn; ch < numOut; ++ch)
        {
            if (numIn == 1)
                buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
            else
                buffer.clear (ch, 0, numSamples);
        }
    }
}

void TareProcessor::startCalibration()
{
    if (resultReady.exchange (false, std::memory_order_acquire))
        consumeProbeResult();

    cancelRequested.store (false);
    startRequested.store (true);
}

void TareProcessor::cancelCalibration()
{
    if (startRequested.exchange (false))
        return;

    cancelRequested.store (true);
}

bool TareProcessor::isCalibrating() const
{
    return startRequested.load() || probe.isRunning();
}

juce::String TareProcessor::getStatusText() const
{
    if (startRequested.load())
        return "Starting... (the host must be processing audio)";

    switch (probe.getPhase())
    {
        case LatencyProbe::Phase::measuringNoise:
            return "Measuring noise floor - keep the synth silent";

        case LatencyProbe::Phase::waitingForOnset:
        case LatencyProbe::Phase::waitingForSilence:
        case LatencyProbe::Phase::gap:
            return "Ping " + juce::String (probe.getPingsSent()) + " of " + juce::String (probe.getPingsRequested());

        case LatencyProbe::Phase::idle:
        case LatencyProbe::Phase::finished:
            break;
    }

    const auto sr = currentSampleRate.load();
    auto text = "Reporting " + formatSamples (getLatencySamples(), sr) + " to the host";

    const auto measuredSr = measuredSampleRate.load();

    if (measuredLatency.load() < 0)
        text << "\nNot calibrated yet";
    else if (measuredSr > 0.0 && sr > 0.0 && measuredSr != sr)
        text << "\nCalibrated at " << juce::String (measuredSr, 0) << " Hz (now scaled) - recalibrate";

    return text;
}

void TareProcessor::timerCallback()
{
    if (resultReady.exchange (false, std::memory_order_acquire))
        consumeProbeResult();

    const auto desired = computeReportedLatency();

    if (desired != getLatencySamples())
        setLatencySamples (desired);
}

void TareProcessor::consumeProbeResult()
{
    const auto& r = probe.getResult();
    const auto sr = currentSampleRate.load();

    if (r.cancelled)
    {
        resultText = "Calibration cancelled.";
        return;
    }

    if (r.tooNoisy)
    {
        resultText = "Return input too noisy (noise floor " + juce::String (r.noiseFloorDb, 1)
                   + " dBFS). Check the routing and gain staging.";
        return;
    }

    if (r.numMeasured == 0)
    {
        resultText = "No onsets detected (" + juce::String (r.numTimedOut) + " pings timed out).\n"
                     "Check that the plugin's MIDI output reaches the synth, the synth's audio "
                     "reaches the plugin's return input, and the threshold is low enough.";
        return;
    }

    std::vector<int64_t> sorted (r.latencies.begin(), r.latencies.begin() + r.numMeasured);
    std::sort (sorted.begin(), sorted.end());

    const auto median = sorted[sorted.size() / 2];
    const auto spread = sorted.back() - sorted.front();
    const auto previous = measuredLatency.load();

    measuredLatency.store ((int) median);
    measuredSampleRate.store (sr);

    resultText = "Round trip: " + formatSamples ((double) median, sr) + "\n"
               + "Jitter (max - min): " + formatSamples ((double) spread, sr) + "\n"
               + "Detected " + juce::String (r.numMeasured) + " of " + juce::String (r.numMeasured + r.numTimedOut)
               + " pings, noise floor " + juce::String (r.noiseFloorDb, 1) + " dBFS, threshold "
               + juce::String (r.thresholdDb, 1) + " dBFS";

    if (previous >= 0)
    {
        const auto delta = median - previous;
        resultText << "\nChange since last run: " << (delta >= 0 ? "+" : "") << juce::String (delta) << " samples";

        if (std::abs (delta) <= 2)
            resultText << " (stable)";
        else
            resultText << " - calibrate again to confirm the host has settled";
    }
}

int TareProcessor::computeReportedLatency() const
{
    const auto sr = currentSampleRate.load();
    const auto measured = measuredLatency.load();

    if (sr <= 0.0 || measured < 0)
        return 0;

    // If the sample rate changed since calibrating, scale as a best guess.
    // Driver buffers are sized in samples so this isn't exact.
    const auto measuredSr = measuredSampleRate.load();
    const auto scale = measuredSr > 0.0 ? sr / measuredSr : 1.0;
    const auto trimMs = (double) parameters.getRawParameterValue (ParamIds::offsetMs)->load();

    return std::max (0, (int) std::lround (measured * scale + trimMs * sr / 1000.0));
}

void TareProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (StateIds::measuredLatency, measuredLatency.load(), nullptr);
    state.setProperty (StateIds::measuredSampleRate, measuredSampleRate.load(), nullptr);
    state.setProperty (StateIds::middleCOctave, middleCOctave.load(), nullptr);
    state.setProperty (StateIds::useFlats, useFlats.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TareProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (parameters.state.getType()))
            return;

        auto state = juce::ValueTree::fromXml (*xml);
        measuredLatency.store ((int) state.getProperty (StateIds::measuredLatency, -1));
        measuredSampleRate.store ((double) state.getProperty (StateIds::measuredSampleRate, 0.0));
        middleCOctave.store (std::clamp ((int) state.getProperty (StateIds::middleCOctave, middleCOctave.load()),
                                         minMiddleCOctave, maxMiddleCOctave));
        useFlats.store ((bool) state.getProperty (StateIds::useFlats, useFlats.load()));
        parameters.replaceState (state);
    }
}

void TareProcessor::setNoteNaming (int newMiddleCOctave, bool newUseFlats)
{
    middleCOctave.store (std::clamp (newMiddleCOctave, minMiddleCOctave, maxMiddleCOctave));
    useFlats.store (newUseFlats);

    // The ping note's text changed, so ask the host to redraw it.
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

juce::String TareProcessor::getNoteName (int note) const
{
    return juce::MidiMessage::getMidiNoteName (note, ! useFlats.load(), true, middleCOctave.load());
}

std::optional<int> TareProcessor::parseNoteName (const juce::String& text) const
{
    // Accepts e.g. "C3", "F#4", "Bb2", "C-1" in the current octave naming.
    const auto t = text.trim();

    if (t.isEmpty())
        return std::nullopt;

    static constexpr int semitones[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);

    if (letter < 'A' || letter > 'G')
        return std::nullopt;

    int note = semitones[letter - 'A'];
    int pos = 1;

    if (t[pos] == '#')
        ++note, ++pos;
    else if (t[pos] == 'b')
        --note, ++pos;

    const auto octaveText = t.substring (pos).trim();

    if (! octaveText.containsOnly ("-0123456789") || octaveText.isEmpty())
        return std::nullopt;

    note += 60 + (octaveText.getIntValue() - middleCOctave.load()) * 12;

    if (note < 0 || note > 127)
        return std::nullopt;

    return note;
}

juce::AudioProcessorEditor* TareProcessor::createEditor()
{
    return new TareEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TareProcessor();
}
