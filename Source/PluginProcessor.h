#pragma once

#include "LatencyProbe.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <optional>

#if TARE_HAS_CLAP
 #include <clap-juce-extensions/clap-juce-extensions.h>
#endif

// Stands in for an external hardware synth:
//  - incoming MIDI is passed to the MIDI output (route that to the synth)
//  - the synth's audio comes back on the input and is passed to the output
//  - the measured round trip is reported to the host as plugin latency, so the
//    host's delay compensation lines the synth up with everything else.
class TareProcessor final : public juce::AudioProcessor,
                            public juce::VST3ClientExtensions,
                           #if TARE_HAS_CLAP
                            public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                           #endif
                            private juce::Timer
{
public:
    TareProcessor();
    ~TareProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Built as both an instrument and an effect (see CMakeLists.txt). On the
    // instrument the return input is an aux (sidechain) bus, which is what
    // hosts expect there; on the effect it is the main input.
    static constexpr bool returnIsMainInput = ! JucePlugin_IsSynth;

    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override { return returnIsMainInput; }

   #if TARE_HAS_CLAP
    bool isInputMain (int index) override { return index == 0 && returnIsMainInput; }
   #endif

    // Message thread API, used by the editor.
    void startCalibration();
    void cancelCalibration();
    bool isCalibrating() const;
    juce::String getStatusText() const;
    juce::String getResultText() const { return resultText; }

    // How note numbers are displayed. DAWs disagree on which octave middle C
    // (note 60) is in. Plugins aren't told the project's key signature, so
    // sharps vs flats is a manual choice too.
    static constexpr int minMiddleCOctave = 3;
    static constexpr int maxMiddleCOctave = 5;
    void setNoteNaming (int middleCOctave, bool useFlats);
    int getMiddleCOctave() const { return middleCOctave.load(); }
    bool getUseFlats() const { return useFlats.load(); }
    juce::String getNoteName (int note) const;
    std::optional<int> parseNoteName (const juce::String&) const;

private:
    // Declared before `parameters`, whose ping note text depends on these.
    std::atomic<int> middleCOctave;
    std::atomic<bool> useFlats { false };

public:
    juce::AudioProcessorValueTreeState parameters;

private:
    void timerCallback() override;
    void consumeProbeResult();
    int computeReportedLatency() const;

    LatencyProbe probe;

    std::atomic<bool> startRequested { false };
    std::atomic<bool> cancelRequested { false };
    std::atomic<bool> resultReady { false };

    // Last measured round trip, and the sample rate it was measured at.
    std::atomic<int> measuredLatency { -1 };
    std::atomic<double> measuredSampleRate { 0.0 };
    std::atomic<double> currentSampleRate { 0.0 };

    juce::String resultText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TareProcessor)
};
