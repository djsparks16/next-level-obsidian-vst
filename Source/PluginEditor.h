#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// NeonObsidianLookAndFeel - centralised glass / neon styling for the plugin UI.
//==============================================================================
class NeonObsidianLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonObsidianLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

//==============================================================================
// Editor — owns a single fixed-grid main view (built in PluginEditor.cpp).
// Layout philosophy: Serum-style signal-flow bands instead of tabs:
//   header / oscillators+filter / envelopes+LFOs+voice / matrix+FX.
//==============================================================================
class ObsidianAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ObsidianAudioProcessorEditor (ObsidianAudioProcessor&);
    ~ObsidianAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ObsidianAudioProcessor& processor;
    NeonObsidianLookAndFeel lnf;

    std::unique_ptr<juce::Component> mainView; // concrete type lives in the .cpp

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObsidianAudioProcessorEditor)
};
