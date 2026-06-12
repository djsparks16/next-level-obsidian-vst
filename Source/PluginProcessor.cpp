#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ObsidianAudioProcessor::ObsidianAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    bank.init();

    synth.addSound (new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (apvts, bank, hostBpm));

    fx.attach (apvts);

    clearVisualNoteState();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ObsidianAudioProcessor::createLayout()
{
    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;
    using BoolParam   = juce::AudioParameterBool;
    using Range       = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto add = [&] (const juce::String& id, const juce::String& name, Range range, float def)
    {
        params.push_back (std::make_unique<FloatParam> (juce::ParameterID { id, 1 }, name, range, def));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name,
                          const juce::StringArray& items, int def)
    {
        params.push_back (std::make_unique<ChoiceParam> (juce::ParameterID { id, 1 }, name, items, def));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        params.push_back (std::make_unique<BoolParam> (juce::ParameterID { id, 1 }, name, def));
    };

    const juce::StringArray tableNames { "Basic", "Pulse", "Growl", "Formant", "Metal", "Reese", "Acid", "Organ",
                                       "Chaos", "Vowel", "Razor", "Glass", "Monster", "Dirty", "Phase", "Alien", "User" };
    const juce::StringArray warpANames { "Off", "Sync", "Bend", "Mirror", "Fold", "Asym", "Quant", "FM (Osc B)",
                                        "AM (Osc B)", "RM (Osc B)", "Phase Bend", "Squeeze", "Window", "Fractal" };
    const juce::StringArray warpBNames { "Off", "Sync", "Bend", "Mirror", "Fold", "Asym", "Quant",
                                        "Phase Bend", "Squeeze", "Window", "Fractal", "Flip", "Wrap", "Crush" };
    const juce::StringArray oscTypeNames { "Wavetable", "Multisample", "Sample", "Granular", "Spectral" };
    const juce::StringArray interpNames { "Classic", "Smooth" };
    const juce::StringArray lfoShapes  { "Sine", "Triangle", "Saw", "Square", "S&H", "Draw 1", "Draw 2", "Chaos" };
    const juce::StringArray qualityNames { "1x Eco", "2x Clean", "4x High", "8x Ultra" };

    // ---------------- Oscillators A / B / C ----------------
    auto addOsc = [&] (const juce::String& prefix, const juce::String& name, int tableDef, float morphDef, float levelDef)
    {
        addChoice (prefix + "Type", name + " Type", oscTypeNames, 0);
        addChoice (prefix + "Table", name + " Table", tableNames, tableDef);
        addChoice (prefix + "Interp", name + " Interp", interpNames, 1);
        add (prefix + "Morph", name + " Morph", { 0.0f, 1.0f }, morphDef);
        add (prefix + "Level", name + " Level", { 0.0f, 1.0f }, levelDef);
        addChoice (prefix + "WarpMode", name + " Warp", prefix == "oscA" ? warpANames : warpBNames, 0);
        add (prefix + "WarpAmt", name + " Warp Amt", { 0.0f, 1.0f }, 0.0f);
        add (prefix + "Semi", name + " Semi", { -24.0f, 24.0f, 1.0f }, 0.0f);
        add (prefix + "Fine", name + " Fine", { -100.0f, 100.0f }, 0.0f);
        add (prefix + "Pan", name + " Pan", { -1.0f, 1.0f }, 0.0f);
        add (prefix + "Scan", name + " Scan/Rate", { 0.0f, 1.0f }, 0.5f);
        add (prefix + "Density", name + " Density", { 0.0f, 1.0f }, 0.5f);
    };

    addOsc ("oscA", "Osc A", 0, 0.3f, 0.8f);
    addOsc ("oscB", "Osc B", 1, 0.5f, 0.0f);
    addOsc ("oscC", "Osc C", 2, 0.2f, 0.0f);

    // ---------------- Sub / Noise ----------------
    add ("subLevel", "Sub Level", { 0.0f, 1.0f }, 0.0f);
    addChoice ("subOct", "Sub Octave", { "-1 Oct", "-2 Oct" }, 0);
    addChoice ("subWave", "Sub Wave", { "Sine", "Triangle", "Square", "Saw" }, 0);
    add ("subPan", "Sub Pan", { -1.0f, 1.0f }, 0.0f);
    add ("noiseLevel", "Noise Level", { 0.0f, 1.0f }, 0.0f);
    addChoice ("noiseColour", "Noise Colour", { "White", "Pinkish", "Dark", "Air" }, 0);
    add ("noisePan", "Noise Pan", { -1.0f, 1.0f }, 0.0f);

    // ---------------- Unison / Voice ----------------
    add ("uniCount",  "Unison Voices", { 1.0f, 16.0f, 1.0f }, 1.0f);
    add ("uniDetune", "Unison Detune", { 0.0f, 100.0f }, 18.0f);
    add ("uniWidth",  "Unison Width",  { 0.0f, 1.0f }, 0.8f);
    add ("glideTime", "Glide", { 0.0f, 2.0f, 0.0f, 0.5f }, 0.0f);
    add ("bendRange", "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f);
    addChoice ("oscQuality", "Warp Quality", qualityNames, 1);

    // ---------------- Envelopes ----------------
    add ("ampA", "Env1 Attack",  { 0.001f, 5.0f, 0.0f, 0.35f }, 0.005f);
    add ("ampD", "Env1 Decay",   { 0.001f, 5.0f, 0.0f, 0.35f }, 0.2f);
    add ("ampS", "Env1 Sustain", { 0.0f, 1.0f }, 0.8f);
    add ("ampR", "Env1 Release", { 0.001f, 8.0f, 0.0f, 0.35f }, 0.15f);

    add ("env2A", "Env2 Attack",  { 0.001f, 5.0f, 0.0f, 0.35f }, 0.005f);
    add ("env2D", "Env2 Decay",   { 0.001f, 5.0f, 0.0f, 0.35f }, 0.3f);
    add ("env2S", "Env2 Sustain", { 0.0f, 1.0f }, 0.2f);
    add ("env2R", "Env2 Release", { 0.001f, 8.0f, 0.0f, 0.35f }, 0.2f);

    // ---------------- Filter ----------------
    addChoice ("fltRouting", "Filter Routing", { "Serial", "Parallel" }, 0);
    addChoice ("fltModel", "Filter 1 Model", { "SVF LP", "SVF BP", "SVF HP", "Ladder 24", "MG Ladder", "Acid Ladder", "MG Dirty", "Comb 2", "PZSVF Draw" }, 0);
    add ("cutoff",    "Filter 1 Cutoff",     { 20.0f, 20000.0f, 0.0f, 0.25f }, 18000.0f);
    add ("reso",      "Filter 1 Resonance",  { 0.1f, 8.0f, 0.0f, 0.5f }, 0.707f);
    add ("fltEnvAmt", "Env2 > Cut", { -1.0f, 1.0f }, 0.0f);
    add ("fltDrive",  "Filter 1 Drive",      { 0.0f, 1.0f }, 0.0f);
    addChoice ("flt2Model", "Filter 2 Model", { "SVF LP", "SVF BP", "SVF HP", "Ladder 24", "MG Ladder", "Acid Ladder", "MG Dirty", "Comb 2", "PZSVF Draw" }, 0);
    add ("cutoff2",    "Filter 2 Cutoff",     { 20.0f, 20000.0f, 0.0f, 0.25f }, 18000.0f);
    add ("reso2",      "Filter 2 Resonance",  { 0.1f, 8.0f, 0.0f, 0.5f }, 0.707f);
    add ("flt2Drive",  "Filter 2 Drive",      { 0.0f, 1.0f }, 0.0f);
    add ("fltBlend",   "Filter Blend",        { 0.0f, 1.0f }, 0.5f);

    // ---------------- LFOs ----------------
    addChoice ("lfo1Shape", "LFO1 Shape", lfoShapes, 0);
    add ("lfo1Rate", "LFO1 Rate", { 0.01f, 20.0f, 0.0f, 0.4f }, 2.0f);
    addBool ("lfo1Sync", "LFO1 Sync", false);
    addChoice ("lfo1Div", "LFO1 Div", Mod::divNames, 2);
    add ("lfo1Cut", "LFO1 > Cut", { 0.0f, 1.0f }, 0.0f);
    add ("lfo1DrawA", "LFO1 Draw A", { 0.0f, 1.0f }, 0.25f);
    add ("lfo1DrawB", "LFO1 Draw B", { 0.0f, 1.0f }, 0.75f);

    addChoice ("lfo2Shape", "LFO2 Shape", lfoShapes, 0);
    add ("lfo2Rate", "LFO2 Rate", { 0.01f, 20.0f, 0.0f, 0.4f }, 1.0f);
    addBool ("lfo2Sync", "LFO2 Sync", false);
    addChoice ("lfo2Div", "LFO2 Div", Mod::divNames, 2);
    add ("lfo2DrawA", "LFO2 Draw A", { 0.0f, 1.0f }, 0.65f);
    add ("lfo2DrawB", "LFO2 Draw B", { 0.0f, 1.0f }, 0.35f);

    // Hidden-but-automatable parity modulators for future pages: 10 LFOs, 4 envelopes.
    for (int i = 3; i <= 10; ++i)
    {
        const auto n = juce::String (i);
        addChoice ("lfo" + n + "Shape", "LFO" + n + " Shape", lfoShapes, 0);
        add ("lfo" + n + "Rate", "LFO" + n + " Rate", { 0.01f, 20.0f, 0.0f, 0.4f }, 1.0f);
        addBool ("lfo" + n + "Sync", "LFO" + n + " Sync", false);
        addChoice ("lfo" + n + "Div", "LFO" + n + " Div", Mod::divNames, 2);
        add ("lfo" + n + "DrawA", "LFO" + n + " Draw A", { 0.0f, 1.0f }, 0.5f);
        add ("lfo" + n + "DrawB", "LFO" + n + " Draw B", { 0.0f, 1.0f }, 0.5f);
    }
    for (int e = 3; e <= 4; ++e)
    {
        const auto n = juce::String (e);
        add ("env" + n + "A", "Env" + n + " Attack",  { 0.001f, 5.0f, 0.0f, 0.35f }, 0.005f);
        add ("env" + n + "D", "Env" + n + " Decay",   { 0.001f, 5.0f, 0.0f, 0.35f }, 0.3f);
        add ("env" + n + "S", "Env" + n + " Sustain", { 0.0f, 1.0f }, 0.2f);
        add ("env" + n + "R", "Env" + n + " Release", { 0.001f, 8.0f, 0.0f, 0.35f }, 0.2f);
    }

    // ---------------- Mod matrix ----------------
    for (int s = 1; s <= Mod::numSlots; ++s)
    {
        const auto n = juce::String (s);
        addChoice ("mod" + n + "Src", "Mod " + n + " Source", Mod::sourceNames, 0);
        addChoice ("mod" + n + "Dst", "Mod " + n + " Dest",   Mod::destNames, 0);
        add ("mod" + n + "Amt", "Mod " + n + " Amount", { -1.0f, 1.0f }, 0.0f);
    }

    // ---------------- FX ----------------
    addBool ("fxDistOn", "Dist On", false);
    addChoice ("fxDistMode", "Dist Mode", { "Soft", "Overdrive", "Fold", "Clip" }, 0);
    add ("fxDistDrive", "Dist Drive", { 0.0f, 1.0f }, 0.3f);
    add ("fxDistMix",   "Dist Mix",   { 0.0f, 1.0f }, 1.0f);

    addBool ("fxConvOn", "Convolution On", false);
    add ("fxConvSize", "Conv Size", { 0.0f, 1.0f }, 0.35f);
    add ("fxConvMix",  "Conv Mix",  { 0.0f, 1.0f }, 0.25f);
    addBool ("fxBodeOn", "Bode On", false);
    add ("fxBodeShift", "Bode Shift", { -5000.0f, 5000.0f }, 0.0f);
    add ("fxBodeMix", "Bode Mix", { 0.0f, 1.0f }, 0.0f);
    addBool ("fxDelayHQ", "Delay HQ", true);

    add ("phat", "PHAT Macro", { 0.0f, 1.0f }, 0.0f);

    addBool ("fxWompOn", "Womp On", false);
    add ("fxWompRate",  "Womp Rate",  { 0.05f, 12.0f, 0.0f, 0.45f }, 2.0f);
    add ("fxWompDepth", "Womp Depth", { 0.0f, 1.0f }, 0.65f);
    add ("fxWompMix",   "Womp Mix",   { 0.0f, 1.0f }, 0.85f);

    addBool ("fxCrushOn", "Crush On", false);
    add ("fxCrushBits", "Crush Bits", { 4.0f, 16.0f, 1.0f }, 10.0f);
    add ("fxCrushRate", "Crush Rate", { 0.02f, 1.0f }, 1.0f);
    add ("fxCrushMix",  "Crush Mix",  { 0.0f, 1.0f }, 0.35f);

    addBool ("fxChorusOn", "Chorus On", false);
    add ("fxChorusRate",  "Chorus Rate",  { 0.05f, 8.0f, 0.0f, 0.5f }, 0.8f);
    add ("fxChorusDepth", "Chorus Depth", { 0.0f, 1.0f }, 0.3f);
    add ("fxChorusMix",   "Chorus Mix",   { 0.0f, 1.0f }, 0.5f);

    addBool ("fxPhaserOn", "Phaser On", false);
    add ("fxPhaserRate",  "Phaser Rate",  { 0.05f, 8.0f, 0.0f, 0.5f }, 0.5f);
    add ("fxPhaserDepth", "Phaser Depth", { 0.0f, 1.0f }, 0.6f);
    add ("fxPhaserMix",   "Phaser Mix",   { 0.0f, 1.0f }, 0.5f);

    addBool ("fxDelayOn", "Delay On", false);
    add ("fxDelayTime", "Delay Time", { 1.0f, 2000.0f, 0.0f, 0.4f }, 350.0f);
    add ("fxDelayFb",   "Delay FB",   { 0.0f, 0.95f }, 0.4f);
    add ("fxDelayMix",  "Delay Mix",  { 0.0f, 1.0f }, 0.3f);

    addBool ("fxRevOn", "Reverb On", false);
    add ("fxRevSize",  "Rev Size",  { 0.0f, 1.0f }, 0.6f);
    add ("fxRevDamp",  "Rev Damp",  { 0.0f, 1.0f }, 0.4f);
    add ("fxRevWidth", "Rev Width", { 0.0f, 1.0f }, 1.0f);
    add ("fxRevMix",   "Rev Mix",   { 0.0f, 1.0f }, 0.3f);

    addBool ("fxCompOn", "Comp On", false);
    add ("fxCompThresh", "Comp Thresh", { -48.0f, 0.0f }, -12.0f);
    add ("fxCompRatio",  "Comp Ratio",  { 1.0f, 20.0f, 0.0f, 0.5f }, 4.0f);

    addBool ("fxWidthOn", "Width On", false);
    add ("fxWidthAmt", "Stereo Width", { 0.0f, 2.0f }, 1.25f);

    addBool ("fxEqOn", "EQ On", false);
    add ("fxEqLow",  "EQ Low",  { -12.0f, 12.0f }, 0.0f);
    add ("fxEqHigh", "EQ High", { -12.0f, 12.0f }, 0.0f);

    addBool ("fxHyperOn", "Hyper On", false);
    add ("fxHyperAmt", "Hyper Amt", { 0.0f, 1.0f }, 0.35f);
    add ("fxHyperMix", "Hyper Mix", { 0.0f, 1.0f }, 0.55f);

    addBool ("fxLimitOn", "Limiter On", true);
    add ("fxLimitDrive", "Limiter Drive", { 0.0f, 1.0f }, 0.18f);
    add ("fxLimitCeil",  "Limiter Ceiling", { -12.0f, 0.0f }, -0.8f);

    // ---------------- Master ----------------
    add ("master", "Master", { -48.0f, 6.0f }, -6.0f);

    return { params.begin(), params.end() };
}

//==============================================================================
void ObsidianAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    outputPeakL.store (0.0f);
    outputPeakR.store (0.0f);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    fx.prepare (spec);

    masterGain.prepare (spec);
    masterGain.setRampDurationSeconds (0.02);
}

void ObsidianAudioProcessor::releaseResources()
{
    panicAllNotes();
    outputPeakL.store (0.0f);
    outputPeakR.store (0.0f);
}

void ObsidianAudioProcessor::clearVisualNoteState() noexcept
{
    for (auto& v : activeNoteVelocities)
        v.store (0.0f);

    activeNoteCount.store (0);
}

void ObsidianAudioProcessor::refreshActiveNoteCount() noexcept
{
    int count = 0;
    for (const auto& v : activeNoteVelocities)
        if (v.load() > 0.0f)
            ++count;

    activeNoteCount.store (count);
}

float ObsidianAudioProcessor::getActiveNoteVelocity (int midiNote) const noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return 0.0f;

    return activeNoteVelocities[(size_t) midiNote].load();
}

bool ObsidianAudioProcessor::isNoteActive (int midiNote) const noexcept
{
    return getActiveNoteVelocity (midiNote) > 0.0f;
}

void ObsidianAudioProcessor::panicAllNotes()
{
    for (int ch = 1; ch <= 16; ++ch)
        synth.allNotesOff (ch, false);

    clearVisualNoteState();
}

bool ObsidianAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ObsidianAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    bool midiVisualStateChanged = false;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            const float velocity = msg.getVelocity();

            if (juce::isPositiveAndBelow (note, 128))
            {
                activeNoteVelocities[(size_t) note].store (velocity);
                lastMidiNote.store (note);
                lastMidiVelocity.store (velocity);
                midiVisualStateChanged = true;
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();

            if (juce::isPositiveAndBelow (note, 128))
            {
                activeNoteVelocities[(size_t) note].store (0.0f);
                midiVisualStateChanged = true;
            }
        }

        if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            clearVisualNoteState();
            midiVisualStateChanged = true;
        }
    }

    if (midiVisualStateChanged)
        refreshActiveNoteCount();

    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    fx.process (buffer);

    masterGain.setGainDecibels (apvts.getRawParameterValue ("master")->load());
    auto block = juce::dsp::AudioBlock<float> (buffer);
    auto ctx   = juce::dsp::ProcessContextReplacing<float> (block);
    masterGain.process (ctx);

    const auto updatePeak = [] (std::atomic<float>& atom, float measured)
    {
        const float held = atom.load();
        atom.store (juce::jmax (measured, held * 0.86f));
    };

    updatePeak (outputPeakL, buffer.getRMSLevel (0, 0, buffer.getNumSamples()));
    updatePeak (outputPeakR, buffer.getNumChannels() > 1 ? buffer.getRMSLevel (1, 0, buffer.getNumSamples())
                                                         : outputPeakL.load());
}

//==============================================================================
bool ObsidianAudioProcessor::loadUserWavetable (const juce::File& file)
{
    auto wt = WavetableBank::loadFromWav (file);
    if (! wt.has_value())
        return false;

    suspendProcessing (true);
    bank.setUserTable (std::move (*wt));
    suspendProcessing (false);
    return true;
}


//==============================================================================
bool ObsidianAudioProcessor::applyPresetXml (const juce::XmlElement& xml, juce::String* error)
{
    const auto expectedTag = apvts.state.getType().toString();
    if (! xml.hasTagName (expectedTag))
    {
        if (error != nullptr)
            *error = "Preset has the wrong root tag. Expected <" + expectedTag + ">.";
        return false;
    }

    juce::StringArray validParameterIDs;
    juce::ValueTree fullState (apvts.state.getType());

    for (auto* param : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            validParameterIDs.add (ranged->paramID);
            fullState.setProperty (juce::Identifier (ranged->paramID),
                                   ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
        }
    }

    for (int i = 0; i < xml.getNumAttributes(); ++i)
    {
        const auto attrName = xml.getAttributeName (i);
        if (! validParameterIDs.contains (attrName))
        {
            if (error != nullptr)
                *error = "Preset contains an unknown parameter: " + attrName;
            return false;
        }

        fullState.setProperty (juce::Identifier (attrName), xml.getAttributeValue (i), nullptr);
    }

    apvts.replaceState (fullState);
    return true;
}

bool ObsidianAudioProcessor::loadPresetXmlText (const juce::String& xmlText, juce::String* error)
{
    auto xml = juce::parseXML (xmlText);
    if (xml == nullptr)
    {
        if (error != nullptr)
            *error = "Preset XML could not be parsed.";
        return false;
    }

    return applyPresetXml (*xml, error);
}

bool ObsidianAudioProcessor::loadPresetFile (const juce::File& file, juce::String* error)
{
    if (! file.existsAsFile())
    {
        if (error != nullptr)
            *error = "Preset file does not exist.";
        return false;
    }

    auto xml = juce::parseXML (file);
    if (xml == nullptr)
    {
        if (error != nullptr)
            *error = "Preset XML could not be parsed.";
        return false;
    }

    return applyPresetXml (*xml, error);
}

//==============================================================================
juce::AudioProcessorEditor* ObsidianAudioProcessor::createEditor()
{
    return new ObsidianAudioProcessorEditor (*this);
}

void ObsidianAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ObsidianAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        juce::String ignored;
        applyPresetXml (*xml, &ignored);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ObsidianAudioProcessor();
}
