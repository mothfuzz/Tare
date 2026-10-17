#include "PluginEditor.h"

namespace
{
    constexpr int margin = 12;
    constexpr int rowHeight = 28;
}

TareEditor::TareEditor (TareProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    calibrateButton.onClick = [this] { processor.startCalibration(); };
    cancelButton.onClick = [this] { processor.cancelCalibration(); };
    addAndMakeVisible (calibrateButton);
    addAndMakeVisible (cancelButton);

    for (auto* label : { &statusLabel, &resultLabel })
    {
        label->setJustificationType (juce::Justification::topLeft);
        label->setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (*label);
    }

    for (auto* param : processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);

        if (withId == nullptr)
            continue;

        auto row = std::make_unique<ParamRow>();
        row->paramId = withId->getParameterID();
        row->label.setText (param->getName (64), juce::dontSendNotification);
        row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, rowHeight - 6);
        row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, withId->getParameterID(), row->slider);

        addAndMakeVisible (row->label);
        addAndMakeVisible (row->slider);
        if (row->paramId == "pingNote")
            pingNoteSlider = &row->slider;

        rows.push_back (std::move (row));
    }

    for (int octave = TareProcessor::minMiddleCOctave; octave <= TareProcessor::maxMiddleCOctave; ++octave)
        middleCBox.addItem ("Middle C is C" + juce::String (octave), octave);

    accidentalsBox.addItem ("Sharps", 1);
    accidentalsBox.addItem ("Flats", 2);

    const auto applyNoteNaming = [this]
    {
        processor.setNoteNaming (middleCBox.getSelectedId(), accidentalsBox.getSelectedId() == 2);

        if (pingNoteSlider != nullptr)
            pingNoteSlider->updateText();
    };

    middleCBox.onChange = applyNoteNaming;
    accidentalsBox.onChange = applyNoteNaming;
    addAndMakeVisible (noteNamingLabel);
    addAndMakeVisible (middleCBox);
    addAndMakeVisible (accidentalsBox);

    const auto numRows = (int) rows.size() + 1;
    setSize (480, 2 * margin + rowHeight + 8 + 40 + 8 + 96 + 8 + numRows * rowHeight);
    timerCallback();
    startTimerHz (15);
}

TareEditor::~TareEditor()
{
    stopTimer();
}

void TareEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void TareEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto buttons = area.removeFromTop (rowHeight);
    calibrateButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    cancelButton.setBounds (buttons.removeFromLeft (80));

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);
    resultLabel.setBounds (area.removeFromTop (96));
    area.removeFromTop (8);

    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowHeight);
        row->label.setBounds (r.removeFromLeft (150));
        row->slider.setBounds (r);

        if (&row->slider == pingNoteSlider)
        {
            auto naming = area.removeFromTop (rowHeight);
            noteNamingLabel.setBounds (naming.removeFromLeft (150));
            middleCBox.setBounds (naming.removeFromLeft (150).reduced (0, 2));
            naming.removeFromLeft (8);
            accidentalsBox.setBounds (naming.removeFromLeft (100).reduced (0, 2));
        }
    }
}

void TareEditor::timerCallback()
{
    const auto calibrating = processor.isCalibrating();
    calibrateButton.setEnabled (! calibrating);
    cancelButton.setEnabled (calibrating);

    statusLabel.setText (processor.getStatusText(), juce::dontSendNotification);
    resultLabel.setText (processor.getResultText(), juce::dontSendNotification);
    syncNoteNaming();
}

// Keeps the drop-downs in step with the processor, which can change underneath
// the editor when the host loads a project.
void TareEditor::syncNoteNaming()
{
    const auto octave = processor.getMiddleCOctave();
    const auto accidentals = processor.getUseFlats() ? 2 : 1;

    if (middleCBox.getSelectedId() == octave && accidentalsBox.getSelectedId() == accidentals)
        return;

    middleCBox.setSelectedId (octave, juce::dontSendNotification);
    accidentalsBox.setSelectedId (accidentals, juce::dontSendNotification);

    if (pingNoteSlider != nullptr)
        pingNoteSlider->updateText();
}
