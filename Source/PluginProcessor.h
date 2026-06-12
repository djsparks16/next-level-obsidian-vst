#pragma once
#include <JuceHeader.h>
#include <array>
#include "SynthVoice.h"
#include "FXChain.h"

class ObsidianAudioProcessor : public juce::AudioProcessor
{
public:
    ObsidianAudioProcessor();
    ~ObsidianAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 3.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the editor (message thread)
    bool loadUserWavetable (const juce::File& file);
    bool loadPresetFile (const juce::File& file, juce::String* error = nullptr);
    bool loadPresetXmlText (const juce::String& xmlText, juce::String* error = nullptr);

    // Emergency note kill, used by the UI PANIC control and MIDI CC 120/123.
    void panicAllNotes();

    float getActiveNoteVelocity (int midiNote) const noexcept;
    bool isNoteActive (int midiNote) const noexcept;
    int getLastMidiNote() const noexcept { return lastMidiNote.load(); }
    float getLastMidiVelocity() const noexcept { return lastMidiVelocity.load(); }
    int getActiveNoteCount() const noexcept { return activeNoteCount.load(); }

    float getOutputPeakL() const noexcept { return outputPeakL.load(); }
    float getOutputPeakR() const noexcept { return outputPeakR.load(); }

    // Read-only access for the editor's wavetable displays (message thread)
    const WavetableBank& getBank() const noexcept { return bank; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    bool applyPresetXml (const juce::XmlElement& xml, juce::String* error);

    WavetableBank bank;
    juce::Synthesiser synth;
    FXChain fx;
    juce::dsp::Gain<float> masterGain;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<float> outputPeakL { 0.0f }, outputPeakR { 0.0f };

    std::array<std::atomic<float>, 128> activeNoteVelocities {};
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<float> lastMidiVelocity { 0.0f };
    std::atomic<int> activeNoteCount { 0 };

    void clearVisualNoteState() noexcept;
    void refreshActiveNoteCount() noexcept;

    static constexpr int numVoices = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ObsidianAudioProcessor)
};
