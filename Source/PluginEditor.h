#pragma once

#include "PluginProcessor.h"

#include <memory>
#include <vector>

class TareEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TareEditor (TareProcessor&);
    ~TareEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncNoteNaming();

    struct ParamRow
    {
        juce::String paramId;
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    TareProcessor& processor;

    juce::TextButton calibrateButton { "Calibrate" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;
    juce::Label resultLabel;
    std::vector<std::unique_ptr<ParamRow>> rows;

    juce::Label noteNamingLabel { {}, "Note names" };
    juce::ComboBox middleCBox;
    juce::ComboBox accidentalsBox;
    juce::Slider* pingNoteSlider = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TareEditor)
};
