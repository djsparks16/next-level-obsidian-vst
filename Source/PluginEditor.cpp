#include "PluginEditor.h"
#include "Modulation.h"
#include <cmath>

//==============================================================================
// Palette + shared glass painting
//==============================================================================
namespace
{
    constexpr auto bg0     = 0xff070810u;
    constexpr auto bg1     = 0xff101220u;
    constexpr auto glass   = 0xaa171a2cu;
    constexpr bool heavyGlow = false; // keep editor animation cheap so the audio thread never gets starved by paint storms
    constexpr auto cyan    = 0xff20f7ffu;
    constexpr auto magenta = 0xffff2bd6u;
    constexpr auto violet  = 0xff8a5cffu;
    constexpr auto lime    = 0xff7cff4fu;
    constexpr auto amber   = 0xffffb020u;

    const juce::Colour textHi  (0xfff4fdff);
    const juce::Colour textLo  (0xffaccbdd);

    using APVTS = juce::AudioProcessorValueTreeState;

    void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent,
                         float corner = 12.0f, int shadow = 12)
    {
        if (heavyGlow && shadow > 0)
            juce::DropShadow (accent.withAlpha (0.18f), juce::jmin (shadow, 8), { 0, 0 }).drawForRectangle (g, r.toNearestInt());

        juce::ColourGradient fill (juce::Colour (0x55264ffa).withAlpha (0.33f), r.getTopLeft(),
                                   juce::Colour (glass), r.getBottomRight(), false);
        fill.addColour (0.45, juce::Colour (0x9920253a));
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, corner);

        g.setColour (juce::Colour (0x18ffffff));
        g.drawRoundedRectangle (r.reduced (1.0f), corner, 1.0f);

        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);

        // Neon edge light along the top
        auto top = r.withHeight (3.0f).reduced (12.0f, 0.0f);
        juce::ColourGradient glow (accent.withAlpha (0.0f), top.getTopLeft(),
                                   accent.withAlpha (0.85f), top.getCentre(), false);
        glow.addColour (1.0, accent.withAlpha (0.0f));
        g.setGradientFill (glow);
        g.fillRoundedRectangle (top, 2.0f);

        // Stained-glass leading lines on larger panels only
        if (r.getHeight() > 90.0f)
        {
            juce::Path shards;
            shards.startNewSubPath (r.getX() + r.getWidth() * 0.08f, r.getBottom() - 1.0f);
            shards.lineTo (r.getX() + r.getWidth() * 0.34f, r.getY() + 1.0f);
            shards.startNewSubPath (r.getX() + r.getWidth() * 0.67f, r.getY() + 1.0f);
            shards.lineTo (r.getRight() - r.getWidth() * 0.12f, r.getBottom() - 1.0f);
            g.setColour (accent.withAlpha (0.09f));
            g.strokePath (shards, juce::PathStrokeType (1.0f));
        }
    }

    // Dark inset "screen" used behind waveform / envelope / LFO / filter displays
    void drawDisplayScreen (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xcc05060c));
        g.fillRoundedRectangle (r, 7.0f);

        juce::ColourGradient sheen (juce::Colour (0x10ffffff), r.getTopLeft(),
                                    juce::Colour (0x00000000), r.getBottomLeft(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f), 7.0f);

        g.setColour (accent.withAlpha (0.30f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

        // Faint centre grid line
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4.0f, r.getRight() - 4.0f);
    }

    void strokeNeon (juce::Graphics& g, const juce::Path& p, juce::Colour accent, float coreWidth = 1.6f)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (coreWidth + 3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (coreWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    bool isWhiteSemitone (int semitone) noexcept
    {
        switch (semitone % 12)
        {
            case 0: case 2: case 4: case 5: case 7: case 9: case 11:
                return true;
            default:
                return false;
        }
    }

    int midiNoteForWhiteIndex (int whiteIndex, int startOctave = 2) noexcept
    {
        static constexpr int whiteSemis[] { 0, 2, 4, 5, 7, 9, 11 };
        return 12 * (startOctave + 1) + 12 * (whiteIndex / 7) + whiteSemis[whiteIndex % 7];
    }

    juce::String midiNoteName (int midiNote)
    {
        if (! juce::isPositiveAndBelow (midiNote, 128))
            return "--";

        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12 - 1);
    }

    float midiNoteFrequency (int midiNote) noexcept
    {
        return 440.0f * std::pow (2.0f, (midiNote - 69) / 12.0f);
    }
}

//==============================================================================
// NeonObsidianLookAndFeel
//==============================================================================
NeonObsidianLookAndFeel::NeonObsidianLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (cyan));
    setColour (juce::Slider::thumbColourId,             juce::Colour (magenta));
    setColour (juce::Slider::trackColourId,             juce::Colour (cyan));
    setColour (juce::Slider::textBoxTextColourId,       textHi);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x55111322));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0x6634d2eb));
    setColour (juce::ComboBox::backgroundColourId,      juce::Colour (0x88101624));
    setColour (juce::ComboBox::textColourId,            textHi);
    setColour (juce::ComboBox::outlineColourId,         juce::Colour (cyan).withAlpha (0.65f));
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff0c0f1a));
    setColour (juce::PopupMenu::textColourId,           textHi);
    setColour (juce::TextButton::buttonColourId,        juce::Colour (0x88101624));
    setColour (juce::TextButton::textColourOffId,       textHi);
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf00c0f1a));
    setColour (juce::BubbleComponent::outlineColourId,    juce::Colour (cyan).withAlpha (0.7f));
}

void NeonObsidianLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPosProportional, float rotaryStartAngle,
                                                float rotaryEndAngle, juce::Slider& slider)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const auto size = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);

    const auto radius = size * 0.5f;
    const auto centre = b.getCentre();
    const auto angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // Glow scaled to knob size (keeps small FX knobs crisp)
    juce::Path shadowPath;
    shadowPath.addEllipse (b);
    if (heavyGlow && width > 42 && height > 42)
        juce::DropShadow (accent.withAlpha (0.28f), juce::jlimit (4, 9, (int) (radius * 0.35f)), { 0, 0 })
            .drawForPath (g, shadowPath);

    juce::ColourGradient body (juce::Colour (0xff232842), b.getTopLeft(),
                               juce::Colour (0xff080a12), b.getBottomRight(), false);
    body.addColour (0.25, juce::Colour (0xff303754));
    g.setGradientFill (body);
    g.fillEllipse (b);

    g.setColour (juce::Colour (0x30ffffff));
    g.drawEllipse (b.reduced (1.0f), 1.0f);

    juce::Path bgArc, valueArc;
    const auto arcRadius = radius - 3.0f;
    const auto arcWidth  = juce::jlimit (2.5f, 4.0f, radius * 0.14f);
    bgArc.addCentredArc    (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (juce::Colour (0x55252b45));
    g.strokePath (bgArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withAlpha (0.92f));
    g.strokePath (valueArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.4f, -radius + 7.0f, 2.8f, radius * 0.42f, 1.4f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (textHi);
    g.fillPath (pointer);

    auto hi = b.reduced (b.getWidth() * 0.22f, b.getHeight() * 0.18f)
               .translated (-b.getWidth() * 0.08f, -b.getHeight() * 0.12f);
    g.setColour (juce::Colour (0x14ffffff));
    g.fillEllipse (hi);
}

void NeonObsidianLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                float sliderPos, float minSliderPos, float maxSliderPos,
                                                const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    auto track  = juce::Rectangle<float> ((float) x, (float) y + height * 0.5f - 2.5f, (float) width, 5.0f).reduced (2.0f, 0.0f);
    auto accent = slider.findColour (juce::Slider::trackColourId);

    g.setColour (juce::Colour (0x7722283e));
    g.fillRoundedRectangle (track, 3.0f);

    // Bipolar parameters (e.g. mod amounts) fill out from the centre
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
    {
        const float zeroX = track.getX() + track.getWidth()
                              * (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()));
        auto fill = sliderPos >= zeroX ? juce::Rectangle<float> (zeroX, track.getY(), sliderPos - zeroX, track.getHeight())
                                       : juce::Rectangle<float> (sliderPos, track.getY(), zeroX - sliderPos, track.getHeight());
        g.setColour (accent.withAlpha (0.88f));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colour (0x66ffffff));
        g.fillRect (juce::Rectangle<float> (zeroX - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f));
    }
    else
    {
        g.setColour (accent.withAlpha (0.88f));
        g.fillRoundedRectangle (track.withRight (sliderPos), 3.0f);
    }

    g.setColour (textHi);
    g.fillEllipse (sliderPos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void NeonObsidianLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                    const juce::Colour&, bool over, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (1.0f);
    auto accent = down ? juce::Colour (magenta) : (over ? juce::Colour (lime) : juce::Colour (cyan));
    drawGlassPanel (g, b, accent, 8.0f, 6);
}

void NeonObsidianLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                            int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    auto accent = box.findColour (juce::ComboBox::outlineColourId);
    drawGlassPanel (g, b, isButtonDown ? juce::Colour (magenta) : accent, 7.0f, 0);

    juce::Path arrow;
    arrow.addTriangle ((float) width - 16.0f, height * 0.42f,
                       (float) width - 8.0f,  height * 0.42f,
                       (float) width - 12.0f, height * 0.62f);
    g.setColour (textHi);
    g.fillPath (arrow);
}

void NeonObsidianLookAndFeel::positionComboBoxText (juce::ComboBox&, juce::Label& label)
{
    label.setBounds (6, 1, label.getParentWidth() - 22, label.getParentHeight() - 2);
    label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    label.setJustificationType (juce::Justification::centredLeft);
}

void NeonObsidianLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                                bool over, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (2.0f);
    auto accent = button.getToggleState() ? juce::Colour (lime) : juce::Colour (0xff626b86);
    if (over || down) accent = accent.brighter (0.25f);

    const auto pillW = juce::jmin (b.getWidth(), 30.0f);
    const auto pillH = juce::jmin (b.getHeight(), 14.0f);

    juce::Rectangle<float> pill;
    if (button.getButtonText().isNotEmpty())
    {
        pill = juce::Rectangle<float> (b.getRight() - pillW, b.getCentreY() - pillH * 0.5f, pillW, pillH);
        g.setColour (button.getToggleState() ? textHi : textLo);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (button.getButtonText().toUpperCase(),
                    b.withTrimmedRight (pillW + 3.0f).toNearestInt(), juce::Justification::centredLeft);
    }
    else
    {
        pill = b.withSizeKeepingCentre (pillW, pillH);
    }

    g.setColour (juce::Colour (0xcc0a0c16));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);

    const auto d = pill.getHeight() - 4.0f;
    const auto knobX = button.getToggleState() ? pill.getRight() - d - 2.0f : pill.getX() + 2.0f;
    g.setColour (button.getToggleState() ? textHi : juce::Colour (0xff626b86));
    g.fillEllipse (knobX, pill.getY() + 2.0f, d, d);

    if (button.getToggleState())
    {
        juce::Path p;
        p.addEllipse (knobX, pill.getY() + 2.0f, d, d);
        juce::DropShadow (juce::Colour (lime).withAlpha (0.6f), 7, { 0, 0 }).drawForPath (g, p);
    }
}

//==============================================================================
// Building blocks
//==============================================================================
namespace
{
    //--------------------------------------------------------------------------
    // Knob: rotary slider + uppercase caption, value shown in a popup on drag.
    //--------------------------------------------------------------------------
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> att;

        void init (juce::Component& parent, APVTS& apvts, const juce::String& paramID,
                   const juce::String& caption, juce::Colour accent)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.18f,
                                        juce::MathConstants<float>::pi * 2.82f, true);
            slider.setMouseDragSensitivity (260);          // finer travel for exact DAW-style setting
            slider.setVelocityBasedMode (true);            // slow drags become surgical, fast drags stay useful
            slider.setVelocityModeParameters (0.72, 1, 0.06, true, juce::ModifierKeys::shiftModifier);
            slider.setScrollWheelEnabled (true);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setPopupDisplayEnabled (true, false, nullptr);
            slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            slider.setColour (juce::Slider::thumbColourId, accent);
            slider.setWantsKeyboardFocus (true);
            slider.setTooltip (caption + ": drag, wheel, or Shift-drag for micro control");
            parent.addAndMakeVisible (slider);
            att = std::make_unique<APVTS::SliderAttachment> (apvts, paramID, slider);

            label.setText (caption.toUpperCase(), juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, textLo);
            label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            label.setInterceptsMouseClicks (false, false);
            parent.addAndMakeVisible (label);
        }

        void setBounds (juce::Rectangle<int> cell)
        {
            label.setBounds (cell.removeFromBottom (15));
            auto knobArea = cell.reduced (2);
            const int side = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
            slider.setBounds (knobArea.withSizeKeepingCentre (side, side));
        }

        void setEnabledVisual (bool shouldBeEnabled)
        {
            slider.setAlpha (shouldBeEnabled ? 1.0f : 0.38f);
            label.setAlpha  (shouldBeEnabled ? 1.0f : 0.45f);
            slider.setEnabled (true);
        }
    };

    //--------------------------------------------------------------------------
    // Choice: combo box bound to an AudioParameterChoice.
    //--------------------------------------------------------------------------
    struct Choice
    {
        juce::ComboBox box;
        std::unique_ptr<APVTS::ComboBoxAttachment> att;

        void init (juce::Component& parent, APVTS& apvts, const juce::String& paramID, juce::Colour accent)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
                box.addItemList (choice->choices, 1);
            box.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.55f));
            parent.addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (apvts, paramID, box);
        }
    };

    //--------------------------------------------------------------------------
    // GlassCard: panel with a neon header strip + title; children fill content()
    //--------------------------------------------------------------------------
    class GlassCard : public juce::Component
    {
    public:
        GlassCard (juce::String titleIn, juce::Colour accentIn)
            : title (std::move (titleIn)), accent (accentIn) {}

        juce::Colour getAccent() const noexcept { return accent; }

        juce::Rectangle<int> content() const
        {
            return getLocalBounds().reduced (9).withTrimmedTop (headerHeight + 2);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, accent, 12.0f, 9);

            auto strip = getLocalBounds().reduced (7).removeFromTop (headerHeight).toFloat();
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (strip, 6.0f);

            g.setColour (textHi);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (title, strip.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }

        static constexpr int headerHeight = 19;

    private:
        juce::String title;
        juce::Colour accent;
    };

    //--------------------------------------------------------------------------
    // WavetableDisplay: live view of the oscillator's current morph frame.
    //--------------------------------------------------------------------------
    class WavetableDisplay : public juce::Component, private juce::Timer
    {
    public:
        WavetableDisplay (ObsidianAudioProcessor& p, juce::String tableID, juce::String morphID,
                          juce::Colour accentIn)
            : processor (p), accent (accentIn)
        {
            tableParam = p.apvts.getRawParameterValue (tableID);
            morphParam = p.apvts.getRawParameterValue (morphID);
            startTimerHz (12);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto inner = r.reduced (5.0f, 6.0f);
            const int  n     = juce::jmax (16, (int) inner.getWidth());

            const int   tableIdx = (int) tableParam->load();
            const float morph    = morphParam->load();

            const auto& wt        = processor.getBank().get (tableIdx);
            const int   numFrames = wt.getNumFrames();
            const float framePos  = morph * (float) (numFrames - 1);
            const int   f0        = juce::jlimit (0, numFrames - 1, (int) framePos);
            const int   f1        = juce::jmin (f0 + 1, numFrames - 1);
            const float ft        = framePos - (float) f0;

            const auto& tA = wt.table (f0, 0);
            const auto& tB = wt.table (f1, 0);

            juce::Path wave;
            for (int i = 0; i < n; ++i)
            {
                const float pos = (float) i / (float) (n - 1);
                const int   idx = juce::jlimit (0, Wavetable::tableSize - 1,
                                                (int) (pos * (float) (Wavetable::tableSize - 1)));
                const float s   = tA[(size_t) idx] + ft * (tB[(size_t) idx] - tA[(size_t) idx]);
                const float xx  = inner.getX() + pos * inner.getWidth();
                const float yy  = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, s) * inner.getHeight() * 0.46f;

                if (i == 0) wave.startNewSubPath (xx, yy);
                else        wave.lineTo (xx, yy);
            }

            // Soft fill under the curve
            juce::Path fill (wave);
            fill.lineTo (inner.getRight(), inner.getCentreY());
            fill.lineTo (inner.getX(), inner.getCentreY());
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.08f));
            g.fillPath (fill);

            strokeNeon (g, wave, accent);

            // Frame position readout, Serum-style
            g.setColour (textLo.withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (f0 + 1) + " / " + juce::String (numFrames),
                        getLocalBounds().reduced (8, 4), juce::Justification::topRight);
        }

    private:
        void timerCallback() override
        {
            // Cheap change signature: table choice, morph, frame count and a few spot samples
            const int   tableIdx = (int) tableParam->load();
            const auto& wt       = processor.getBank().get (tableIdx);

            float sig = (float) tableIdx * 1000.0f + morphParam->load() * 97.0f
                        + (float) wt.getNumFrames();
            const auto& t0 = wt.table (0, 0);
            for (int i = 0; i < Wavetable::tableSize; i += Wavetable::tableSize / 7)
                sig += t0[(size_t) i];

            if (std::abs (sig - lastSig) > 1.0e-6f)
            {
                lastSig = sig;
                repaint();
            }
        }

        ObsidianAudioProcessor& processor;
        juce::Colour accent;
        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* morphParam = nullptr;
        float lastSig = -1.0e9f;
    };

    //--------------------------------------------------------------------------
    // ADSRDisplay
    //--------------------------------------------------------------------------
    class ADSRDisplay : public juce::Component, private juce::Timer
    {
    public:
        ADSRDisplay (APVTS& apvts, const juce::String& prefix, juce::Colour accentIn,
                     float maxAD = 5.0f, float maxR = 8.0f)
            : accent (accentIn), maxAttackDecay (maxAD), maxRelease (maxR)
        {
            a = apvts.getRawParameterValue (prefix + "A");
            d = apvts.getRawParameterValue (prefix + "D");
            s = apvts.getRawParameterValue (prefix + "S");
            r = apvts.getRawParameterValue (prefix + "R");
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto rect = getLocalBounds().toFloat();
            drawDisplayScreen (g, rect, accent);

            const auto in   = rect.reduced (7.0f, 8.0f);
            const float top = in.getY(), bot = in.getBottom();

            auto seg = [] (float v, float maxV) { return 0.02f + 0.28f * std::pow (v / maxV, 0.45f); };

            const float wa = seg (a->load(), maxAttackDecay);
            const float wd = seg (d->load(), maxAttackDecay);
            const float wr = seg (r->load(), maxRelease);
            const float ws = juce::jmax (0.08f, 1.0f - wa - wd - wr);
            const float total = wa + wd + ws + wr;

            const float xA = in.getX() + in.getWidth() * (wa / total);
            const float xD = xA        + in.getWidth() * (wd / total);
            const float xS = xD        + in.getWidth() * (ws / total);
            const float yS = bot - s->load() * (bot - top);

            juce::Path p;
            p.startNewSubPath (in.getX(), bot);
            p.quadraticTo (in.getX() + (xA - in.getX()) * 0.4f, top, xA, top);          // attack
            p.quadraticTo (xA + (xD - xA) * 0.3f, yS, xD, yS);                          // decay
            p.lineTo (xS, yS);                                                          // sustain
            p.quadraticTo (xS + (in.getRight() - xS) * 0.3f, bot, in.getRight(), bot);  // release

            juce::Path fill (p);
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.10f));
            g.fillPath (fill);

            strokeNeon (g, p, accent, 1.5f);

            g.setColour (textHi);
            for (auto pt : { juce::Point<float> (xA, top),
                             juce::Point<float> (xD, yS),
                             juce::Point<float> (xS, yS) })
                g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
        }

    private:
        void timerCallback() override
        {
            const float sig = a->load() * 7.1f + d->load() * 3.3f + s->load() * 11.7f + r->load();
            if (std::abs (sig - lastSig) > 1.0e-6f) { lastSig = sig; repaint(); }
        }

        juce::Colour accent;
        float maxAttackDecay, maxRelease;
        std::atomic<float>* a = nullptr; std::atomic<float>* d = nullptr;
        std::atomic<float>* s = nullptr; std::atomic<float>* r = nullptr;
        float lastSig = -1.0e9f;
    };

    //--------------------------------------------------------------------------
    // LFODisplay: one cycle of the selected shape.
    //--------------------------------------------------------------------------
    class LFODisplay : public juce::Component, private juce::Timer
    {
    public:
        LFODisplay (APVTS& apvts, const juce::String& shapeID, const juce::String& drawAID,
                    const juce::String& drawBID, juce::Colour accentIn)
            : accent (accentIn)
        {
            shape = apvts.getRawParameterValue (shapeID);
            drawA = apvts.getRawParameterValue (drawAID);
            drawB = apvts.getRawParameterValue (drawBID);
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto in = r.reduced (7.0f, 8.0f);
            auto yOf = [&] (float v) { return in.getCentreY() - v * in.getHeight() * 0.45f; };

            juce::Path p;
            const int sh = (int) shape->load();

            if (sh == 4) // S&H: deterministic preview steps
            {
                static const float steps[] = { 0.35f, -0.6f, 0.85f, -0.2f, 0.55f, -0.9f, 0.1f, -0.45f };
                const int n = (int) (sizeof (steps) / sizeof (steps[0]));
                for (int i = 0; i < n; ++i)
                {
                    const float x0 = in.getX() + in.getWidth() * (float) i / (float) n;
                    const float x1 = in.getX() + in.getWidth() * (float) (i + 1) / (float) n;
                    if (i == 0) p.startNewSubPath (x0, yOf (steps[i]));
                    else        p.lineTo (x0, yOf (steps[i]));
                    p.lineTo (x1, yOf (steps[i]));
                }
            }
            else if (sh == 5 || sh == 6) // drawable curves
            {
                const float a = drawA != nullptr ? drawA->load() * 2.0f - 1.0f : 0.0f;
                const float b = drawB != nullptr ? drawB->load() * 2.0f - 1.0f : 0.0f;
                const float y0 = -1.0f, y1 = sh == 5 ? a : b, y2 = sh == 5 ? b : a, y3 = -1.0f;
                const int n = juce::jmax (16, (int) in.getWidth());
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / (float) (n - 1);
                    const float seg = juce::jlimit (0.0f, 2.999f, t * 3.0f);
                    const int idx = (int) seg;
                    const float u = seg - (float) idx;
                    const float sm = u * u * (3.0f - 2.0f * u);
                    const float v = idx == 0 ? y0 + sm * (y1 - y0)
                                  : idx == 1 ? y1 + sm * (y2 - y1)
                                             : y2 + sm * (y3 - y2);
                    const float x = in.getX() + t * in.getWidth();
                    if (i == 0) p.startNewSubPath (x, yOf (v));
                    else        p.lineTo (x, yOf (v));
                }

                g.setColour (accent.withAlpha (0.35f));
                g.fillEllipse (in.getX() + in.getWidth() / 3.0f - 3.0f, yOf (y1) - 3.0f, 6.0f, 6.0f);
                g.fillEllipse (in.getX() + in.getWidth() * 2.0f / 3.0f - 3.0f, yOf (y2) - 3.0f, 6.0f, 6.0f);
            }
            else if (sh == 3) // square
            {
                p.startNewSubPath (in.getX(), yOf (1.0f));
                p.lineTo (in.getCentreX(), yOf (1.0f));
                p.lineTo (in.getCentreX(), yOf (-1.0f));
                p.lineTo (in.getRight(),  yOf (-1.0f));
            }
            else if (sh == 2) // saw
            {
                p.startNewSubPath (in.getX(), yOf (-1.0f));
                p.lineTo (in.getRight(), yOf (1.0f));
            }
            else if (sh == 1) // triangle
            {
                p.startNewSubPath (in.getX(), yOf (-1.0f));
                p.lineTo (in.getCentreX(), yOf (1.0f));
                p.lineTo (in.getRight(), yOf (-1.0f));
            }
            else // sine
            {
                const int n = juce::jmax (16, (int) in.getWidth());
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) i / (float) (n - 1);
                    const float x = in.getX() + t * in.getWidth();
                    const float y = yOf (std::sin (juce::MathConstants<float>::twoPi * t));
                    if (i == 0) p.startNewSubPath (x, y);
                    else        p.lineTo (x, y);
                }
            }

            strokeNeon (g, p, accent, 1.5f);
        }

    private:
        void timerCallback() override
        {
            const float v = shape->load() + (drawA != nullptr ? drawA->load() : 0.0f) * 0.01f
                          + (drawB != nullptr ? drawB->load() : 0.0f) * 0.001f;
            if (v != last) { last = v; repaint(); }
        }

        juce::Colour accent;
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* drawA = nullptr;
        std::atomic<float>* drawB = nullptr;
        float last = -1.0f;
    };

    //--------------------------------------------------------------------------
    // FilterDisplay: stylised response curve from model / cutoff / resonance.
    //--------------------------------------------------------------------------
    class FilterDisplay : public juce::Component, private juce::Timer
    {
    public:
        FilterDisplay (APVTS& apvts, juce::Colour accentIn) : accent (accentIn)
        {
            model  = apvts.getRawParameterValue ("fltModel");
            cutoff = apvts.getRawParameterValue ("cutoff");
            reso   = apvts.getRawParameterValue ("reso");
            startTimerHz (15);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawDisplayScreen (g, r, accent);

            const auto in = r.reduced (6.0f, 7.0f);
            const int  m  = (int) model->load();
            const float fc = juce::jlimit (20.0f, 20000.0f, cutoff->load());
            const float resoDb = juce::jlimit (0.0f, 18.0f,
                                               20.0f * std::log10 (juce::jmax (0.1f, reso->load()) / 0.707f));
            const float slope = (m == 3 ? 24.0f : 12.0f);

            auto dbToY = [&] (float db)
            {
                const float norm = (24.0f - juce::jlimit (-36.0f, 24.0f, db)) / 60.0f;
                return in.getY() + norm * in.getHeight();
            };

            juce::Path p;
            const int n = juce::jmax (24, (int) in.getWidth() / 2);

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) (n - 1);
                const float f = 20.0f * std::pow (1000.0f, t);           // 20 Hz .. 20 kHz, log axis
                const float octs = std::log2 (f / fc);

                float db = 0.0f;
                if (m == 1)       db = -slope * 0.75f * std::abs (octs); // band pass
                else if (m == 2)  db = octs < 0.0f ? slope * octs : 0.0f; // high pass
                else              db = octs > 0.0f ? -slope * octs : 0.0f; // low pass (SVF or ladder)

                db += resoDb * std::exp (-octs * octs * 7.0f);           // resonance bump at cutoff

                const float x = in.getX() + t * in.getWidth();
                const float y = dbToY (db);
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }

            // 0 dB reference
            g.setColour (juce::Colour (0x12ffffff));
            g.drawHorizontalLine ((int) dbToY (0.0f), in.getX(), in.getRight());

            juce::Path fill (p);
            fill.lineTo (in.getRight(), in.getBottom());
            fill.lineTo (in.getX(), in.getBottom());
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.08f));
            g.fillPath (fill);

            strokeNeon (g, p, accent, 1.5f);
        }

    private:
        void timerCallback() override
        {
            const float sig = model->load() * 100.0f + cutoff->load() * 0.013f + reso->load() * 3.0f;
            if (std::abs (sig - lastSig) > 1.0e-5f) { lastSig = sig; repaint(); }
        }

        juce::Colour accent;
        std::atomic<float>* model = nullptr;
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* reso = nullptr;
        float lastSig = -1.0e9f;
    };

    //==========================================================================
    // Panels
    //==========================================================================
    class OscPanel : public GlassCard
    {
    public:
        OscPanel (ObsidianAudioProcessor& p, const juce::String& prefix, // "oscA" / "oscB"
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent),
              display (p, prefix + "Table", prefix + "Morph", accent)
        {
            auto& apvts = p.apvts;
            type.init  (*this, apvts, prefix + "Type", accent);
            table.init (*this, apvts, prefix + "Table", accent);
            warp.init  (*this, apvts, prefix + "WarpMode", accent);
            interp.init (*this, apvts, prefix + "Interp", accent);
            addAndMakeVisible (display);

            morph.init   (*this, apvts, prefix + "Morph",   "Morph",  accent);
            warpAmt.init (*this, apvts, prefix + "WarpAmt", "Warp",   accent);
            semi.init    (*this, apvts, prefix + "Semi",    "Semi",   accent);
            fine.init    (*this, apvts, prefix + "Fine",    "Fine",   accent);
            level.init   (*this, apvts, prefix + "Level",   "Level",  accent);
            pan.init     (*this, apvts, prefix + "Pan",     "Pan",    accent);
            scan.init    (*this, apvts, prefix + "Scan",    "Scan",   accent);
            density.init (*this, apvts, prefix + "Density", "Grain",  accent);
        }

        void resized() override
        {
            auto r = content();

            auto row1 = r.removeFromTop (22);
            type.box.setBounds  (row1.removeFromLeft (juce::roundToInt (row1.getWidth() * 0.38f)).reduced (1, 0));
            table.box.setBounds (row1.reduced (1, 0));
            r.removeFromTop (3);
            auto row2 = r.removeFromTop (22);
            warp.box.setBounds   (row2.removeFromLeft (juce::roundToInt (row2.getWidth() * 0.64f)).reduced (1, 0));
            interp.box.setBounds (row2.reduced (1, 0));
            r.removeFromTop (4);

            auto knobRow = r.removeFromBottom (juce::jmin (118, r.getHeight() / 2 + 24));
            auto rowA = knobRow.removeFromTop (knobRow.getHeight() / 2);
            const int cellA = rowA.getWidth() / 4;
            morph.setBounds   (rowA.removeFromLeft (cellA));
            warpAmt.setBounds (rowA.removeFromLeft (cellA));
            semi.setBounds    (rowA.removeFromLeft (cellA));
            fine.setBounds    (rowA);
            const int cellB = knobRow.getWidth() / 4;
            level.setBounds   (knobRow.removeFromLeft (cellB));
            pan.setBounds     (knobRow.removeFromLeft (cellB));
            scan.setBounds    (knobRow.removeFromLeft (cellB));
            density.setBounds (knobRow);

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice type, table, warp, interp;
        WavetableDisplay display;
        Knob morph, warpAmt, semi, fine, level, pan, scan, density;
    };

    class SubNoisePanel : public GlassCard
    {
    public:
        SubNoisePanel (APVTS& apvts, juce::Colour accent) : GlassCard ("SUB / NOISE", accent)
        {
            oct.init   (*this, apvts, "subOct", accent);
            wave.init  (*this, apvts, "subWave", accent);
            colour.init (*this, apvts, "noiseColour", accent);
            sub.init   (*this, apvts, "subLevel",   "Sub",   accent);
            subPan.init (*this, apvts, "subPan",    "Sub Pan", accent);
            noise.init (*this, apvts, "noiseLevel", "Noise", accent);
            noisePan.init (*this, apvts, "noisePan", "Noise Pan", accent);
        }

        void resized() override
        {
            auto r = content();
            oct.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (3);
            wave.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (3);
            colour.box.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);

            const int half = r.getHeight() / 2;
            auto subRow = r.removeFromTop (half);
            sub.setBounds    (subRow.removeFromLeft (subRow.getWidth() / 2).reduced (3, 1));
            subPan.setBounds (subRow.reduced (3, 1));
            noise.setBounds    (r.removeFromLeft (r.getWidth() / 2).reduced (3, 1));
            noisePan.setBounds (r.reduced (3, 1));
        }

    private:
        Choice oct, wave, colour;
        Knob sub, subPan, noise, noisePan;
    };

    class FilterPanel : public GlassCard
    {
    public:
        FilterPanel (APVTS& apvts, juce::Colour accent)
            : GlassCard ("FILTER", accent), display (apvts, accent)
        {
            routing.init (*this, apvts, "fltRouting", accent);
            model.init  (*this, apvts, "fltModel", accent);
            model2.init (*this, apvts, "flt2Model", accent);
            addAndMakeVisible (display);
            cutoff.init (*this, apvts, "cutoff",    "Cut 1", accent);
            reso.init   (*this, apvts, "reso",      "Res 1",   accent);
            drive.init  (*this, apvts, "fltDrive",  "Drv 1",  accent);
            envAmt.init (*this, apvts, "fltEnvAmt", "Env 2",  accent);
            cutoff2.init (*this, apvts, "cutoff2",  "Cut 2", accent);
            reso2.init   (*this, apvts, "reso2",    "Res 2", accent);
            drive2.init  (*this, apvts, "flt2Drive", "Drv 2", accent);
            blend.init   (*this, apvts, "fltBlend", "Blend", accent);
        }

        void resized() override
        {
            auto r = content();
            auto top = r.removeFromTop (22);
            routing.box.setBounds (top.removeFromLeft (juce::roundToInt (top.getWidth() * 0.30f)).reduced (1, 0));
            model.box.setBounds   (top.removeFromLeft (juce::roundToInt (top.getWidth() * 0.50f)).reduced (1, 0));
            model2.box.setBounds  (top.reduced (1, 0));
            r.removeFromTop (5);

            auto knobRow = r.removeFromBottom (juce::jmin (112, r.getHeight() / 2 + 18));
            auto row1 = knobRow.removeFromTop (knobRow.getHeight() / 2);
            const int cell1 = row1.getWidth() / 4;
            cutoff.setBounds (row1.removeFromLeft (cell1));
            reso.setBounds   (row1.removeFromLeft (cell1));
            drive.setBounds  (row1.removeFromLeft (cell1));
            envAmt.setBounds (row1);
            const int cell2 = knobRow.getWidth() / 4;
            cutoff2.setBounds (knobRow.removeFromLeft (cell2));
            reso2.setBounds   (knobRow.removeFromLeft (cell2));
            drive2.setBounds  (knobRow.removeFromLeft (cell2));
            blend.setBounds   (knobRow);

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice routing, model, model2;
        FilterDisplay display;
        Knob cutoff, reso, drive, envAmt, cutoff2, reso2, drive2, blend;
    };

    class EnvPanel : public GlassCard
    {
    public:
        EnvPanel (APVTS& apvts, const juce::String& prefix, // "amp" / "env2"
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent), display (apvts, prefix, accent)
        {
            addAndMakeVisible (display);
            a.init (*this, apvts, prefix + "A", "A", accent);
            d.init (*this, apvts, prefix + "D", "D", accent);
            s.init (*this, apvts, prefix + "S", "S", accent);
            r.init (*this, apvts, prefix + "R", "R", accent);
        }

        void resized() override
        {
            auto rect = content();
            auto knobRow = rect.removeFromBottom (juce::jmin (66, rect.getHeight() / 2));
            const int cellW = knobRow.getWidth() / 4;
            a.setBounds (knobRow.removeFromLeft (cellW));
            d.setBounds (knobRow.removeFromLeft (cellW));
            s.setBounds (knobRow.removeFromLeft (cellW));
            r.setBounds (knobRow);

            rect.removeFromBottom (4);
            display.setBounds (rect);
        }

    private:
        ADSRDisplay display;
        Knob a, d, s, r;
    };

    class LfoPanel : public GlassCard
    {
    public:
        LfoPanel (APVTS& apvts, int index, bool withCutoffSend,
                  const juce::String& title, juce::Colour accent)
            : GlassCard (title, accent),
              display (apvts, "lfo" + juce::String (index) + "Shape",
                       "lfo" + juce::String (index) + "DrawA",
                       "lfo" + juce::String (index) + "DrawB", accent)
        {
            const auto n = juce::String (index);
            shape.init (*this, apvts, "lfo" + n + "Shape", accent);
            div.init   (*this, apvts, "lfo" + n + "Div",   accent);

            sync.setButtonText ("Sync");
            addAndMakeVisible (sync);
            syncAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, "lfo" + n + "Sync", sync);

            addAndMakeVisible (display);
            rate.init (*this, apvts, "lfo" + n + "Rate", "Rate", accent);
            drawA.init (*this, apvts, "lfo" + n + "DrawA", "Draw A", accent);
            drawB.init (*this, apvts, "lfo" + n + "DrawB", "Draw B", accent);

            if (withCutoffSend)
            {
                cut = std::make_unique<Knob>();
                cut->init (*this, apvts, "lfo1Cut", "> Cutoff", accent);
            }
        }

        void resized() override
        {
            auto r = content();

            auto row = r.removeFromTop (22);
            shape.box.setBounds (row.removeFromLeft (juce::roundToInt (row.getWidth() * 0.40f)).reduced (1, 0));
            sync.setBounds (row.removeFromLeft (62).reduced (2, 0));
            div.box.setBounds (row.reduced (1, 0));
            r.removeFromTop (4);

            auto knobRow = r.removeFromBottom (juce::jmin (66, r.getHeight() / 2));
            if (cut != nullptr)
            {
                const int cellW = knobRow.getWidth() / 4;
                rate.setBounds  (knobRow.removeFromLeft (cellW));
                drawA.setBounds (knobRow.removeFromLeft (cellW));
                drawB.setBounds (knobRow.removeFromLeft (cellW));
                cut->setBounds  (knobRow);
            }
            else
            {
                const int cellW = knobRow.getWidth() / 3;
                rate.setBounds  (knobRow.removeFromLeft (cellW));
                drawA.setBounds (knobRow.removeFromLeft (cellW));
                drawB.setBounds (knobRow);
            }

            r.removeFromBottom (4);
            display.setBounds (r);
        }

    private:
        Choice shape, div;
        juce::ToggleButton sync;
        std::unique_ptr<APVTS::ButtonAttachment> syncAtt;
        LFODisplay display;
        Knob rate, drawA, drawB;
        std::unique_ptr<Knob> cut;
    };

    class VoicePanel : public GlassCard
    {
    public:
        VoicePanel (APVTS& apvts, juce::Colour accent) : GlassCard ("VOICE", accent)
        {
            voices.init (*this, apvts, "uniCount",  "Unison", accent);
            detune.init (*this, apvts, "uniDetune", "Detune", accent);
            width.init  (*this, apvts, "uniWidth",  "Width",  accent);
            glide.init  (*this, apvts, "glideTime", "Glide",  accent);
            bend.init   (*this, apvts, "bendRange", "Bend",   accent);
            quality.init (*this, apvts, "oscQuality", accent);
        }

        void resized() override
        {
            auto r = content();
            auto topRow = r.removeFromTop (r.getHeight() / 2);

            const int w3 = topRow.getWidth() / 3;
            voices.setBounds (topRow.removeFromLeft (w3));
            detune.setBounds (topRow.removeFromLeft (w3));
            width.setBounds  (topRow);

            quality.box.setBounds (r.removeFromTop (22).reduced (8, 0));
            r.removeFromTop (2);
            const int w2 = r.getWidth() / 2;
            glide.setBounds (r.removeFromLeft (w2).reduced (8, 0));
            bend.setBounds  (r.reduced (8, 0));
        }

    private:
        Choice quality;
        Knob voices, detune, width, glide, bend;
    };

    //--------------------------------------------------------------------------
    // MatrixPanel: 8 compact rows of source -> destination -> amount
    //--------------------------------------------------------------------------
    class MatrixPanel : public GlassCard
    {
    public:
        MatrixPanel (APVTS& apvts, juce::Colour accent) : GlassCard ("MOD MATRIX", accent)
        {
            for (int s = 1; s <= Mod::numSlots; ++s)
            {
                auto row = std::make_unique<Row>();
                const auto n = juce::String (s);

                row->index.setText (n, juce::dontSendNotification);
                row->index.setJustificationType (juce::Justification::centred);
                row->index.setColour (juce::Label::textColourId, accent.withAlpha (0.8f));
                row->index.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                row->index.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (row->index);

                row->src.addItemList (Mod::sourceNames, 1);
                row->dst.addItemList (Mod::destNames, 1);
                row->src.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
                row->dst.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.4f));
                addAndMakeVisible (row->src);
                addAndMakeVisible (row->dst);

                row->amt.setSliderStyle (juce::Slider::LinearHorizontal);
                row->amt.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                row->amt.setPopupDisplayEnabled (true, false, nullptr);
                row->amt.setColour (juce::Slider::trackColourId, accent);
                addAndMakeVisible (row->amt);

                row->srcAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, "mod" + n + "Src", row->src);
                row->dstAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, "mod" + n + "Dst", row->dst);
                row->amtAtt = std::make_unique<APVTS::SliderAttachment>   (apvts, "mod" + n + "Amt", row->amt);

                rows.push_back (std::move (row));
            }
        }

        void resized() override
        {
            auto r = content();
            const int rowH = r.getHeight() / Mod::numSlots;

            for (auto& row : rows)
            {
                auto line = r.removeFromTop (rowH).reduced (0, juce::jmax (1, (rowH - 22) / 2));
                row->index.setBounds (line.removeFromLeft (16));
                row->src.setBounds   (line.removeFromLeft (juce::roundToInt (line.getWidth() * 0.30f)).reduced (2, 0));
                row->dst.setBounds   (line.removeFromLeft (juce::roundToInt (line.getWidth() * 0.48f)).reduced (2, 0));
                row->amt.setBounds   (line.reduced (2, 0));
            }
        }

    private:
        struct Row
        {
            juce::Label index;
            juce::ComboBox src, dst;
            juce::Slider amt;
            std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt, dstAtt;
            std::unique_ptr<APVTS::SliderAttachment> amtAtt;
        };
        std::vector<std::unique_ptr<Row>> rows;
    };

    //--------------------------------------------------------------------------
    // FxPanel: six mini cards in a 3x2 grid, each with a power switch.
    //--------------------------------------------------------------------------
    class FxPanel : public GlassCard, private juce::Timer
    {
    public:
        FxPanel (APVTS& apvts, juce::Colour accent) : GlassCard ("FX RACK", accent)
        {
            struct Def { const char* name; const char* onID;
                         std::vector<std::pair<const char*, const char*>> params; };

            const std::vector<Def> defs =
            {
                { "WOMP",   "fxWompOn",   { { "fxWompRate",  "Rate" }, { "fxWompDepth", "Depth" }, { "fxWompMix", "Mix" } } },
                { "CRUSH",  "fxCrushOn",  { { "fxCrushBits", "Bits" }, { "fxCrushRate",  "Rate"  }, { "fxCrushMix", "Mix" } } },
                { "DIST",   "fxDistOn",   { { "fxDistDrive",   "Drive" }, { "fxDistMix",   "Mix"   } } },
                { "CONV",   "fxConvOn",   { { "fxConvSize", "Size" }, { "fxConvMix", "Mix" } } },
                { "BODE",   "fxBodeOn",   { { "fxBodeShift", "Shift" }, { "fxBodeMix", "Mix" } } },
                { "CHORUS", "fxChorusOn", { { "fxChorusRate",  "Rate"  }, { "fxChorusDepth","Depth"}, { "fxChorusMix", "Mix" } } },
                { "PHASER", "fxPhaserOn", { { "fxPhaserRate",  "Rate"  }, { "fxPhaserDepth","Depth"}, { "fxPhaserMix", "Mix" } } },
                { "DELAY",  "fxDelayOn",  { { "fxDelayTime",   "Time"  }, { "fxDelayFb",   "FB"    }, { "fxDelayMix",  "Mix" } } },
                { "REVERB", "fxRevOn",    { { "fxRevSize",     "Size"  }, { "fxRevDamp",   "Damp"  }, { "fxRevWidth",  "Width" }, { "fxRevMix", "Mix" } } },
                { "WIDTH",  "fxWidthOn",  { { "fxWidthAmt", "Wide" } } },
                { "HYPER",  "fxHyperOn",  { { "fxHyperAmt", "Amt" }, { "fxHyperMix", "Mix" } } },
                { "EQ",     "fxEqOn",     { { "fxEqLow", "Low" }, { "fxEqHigh", "High" } } },
                { "COMP",   "fxCompOn",   { { "fxCompThresh",  "Thresh"}, { "fxCompRatio", "Ratio" } } },
                { "LIMIT",  "fxLimitOn",  { { "fxLimitDrive", "Drive" }, { "fxLimitCeil", "Ceil" } } },
            };

            for (const auto& def : defs)
            {
                auto unit = std::make_unique<Unit>();
                unit->name = def.name;
                unit->accent = accent;

                unit->on.setButtonText ("");
                addAndMakeVisible (unit->on);
                unit->onAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, def.onID, unit->on);

                for (const auto& [id, caption] : def.params)
                {
                    auto k = std::make_unique<Knob>();
                    k->init (*this, apvts, id, caption, accent);
                    unit->knobs.push_back (std::move (k));
                }
                units.push_back (std::move (unit));
            }
            startTimerHz (12);
        }

        void resized() override
        {
            auto r = content();
            const int cols = 3;
            const int rowsN = juce::jmax (1, (int) std::ceil (units.size() / (float) cols));
            const int cw = r.getWidth() / cols, ch = r.getHeight() / rowsN;

            for (size_t i = 0; i < units.size(); ++i)
            {
                auto cell = juce::Rectangle<int> (r.getX() + (int) (i % cols) * cw,
                                                  r.getY() + (int) (i / cols) * ch, cw, ch).reduced (3);
                units[i]->bounds = cell;

                auto header = cell.removeFromTop (16);
                units[i]->on.setBounds (header.removeFromRight (32));

                const int n = (int) units[i]->knobs.size();
                const int kw = cell.getWidth() / juce::jmax (1, n);
                for (auto& k : units[i]->knobs)
                    k->setBounds (cell.removeFromLeft (kw).reduced (2, 0));
            }
            syncUnitVisuals();
        }

        void paint (juce::Graphics& g) override
        {
            GlassCard::paint (g);

            for (auto& u : units)
            {
                auto cell = u->bounds.toFloat();
                g.setColour (juce::Colour (0x4408090f));
                g.fillRoundedRectangle (cell, 7.0f);
                g.setColour (u->accent.withAlpha (u->on.getToggleState() ? 0.45f : 0.18f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 7.0f, 1.0f);

                g.setColour (u->on.getToggleState() ? textHi : textLo.withAlpha (0.7f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (u->name, u->bounds.reduced (8, 2).removeFromTop (14),
                            juce::Justification::centredLeft);

                if (! u->on.getToggleState())
                {
                    g.setColour (textLo.withAlpha (0.26f));
                    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
                    g.drawText ("BYPASS", u->bounds.reduced (8, 2).removeFromTop (14),
                                juce::Justification::centredRight);
                }
            }
        }

        void timerCallback() override
        {
            syncUnitVisuals();
        }

    private:
        void syncUnitVisuals()
        {
            for (auto& u : units)
            {
                const bool on = u->on.getToggleState();
                for (auto& k : u->knobs)
                    k->setEnabledVisual (on);
            }
        }
        struct Unit
        {
            juce::String name;
            juce::Colour accent;
            juce::Rectangle<int> bounds;
            juce::ToggleButton on;
            std::unique_ptr<APVTS::ButtonAttachment> onAtt;
            std::vector<std::unique_ptr<Knob>> knobs;
        };
        std::vector<std::unique_ptr<Unit>> units;
    };


    //--------------------------------------------------------------------------
    // Visual chrome: Serum-style top tabs, mod source rail, and keyboard footer.
    //--------------------------------------------------------------------------
    class TopTabStrip : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 8.0f, 4);

            static const char* names[] { "OSC", "FX", "MATRIX", "GLOBAL" };
            auto cell = getLocalBounds().reduced (5, 4).removeFromLeft (juce::jmin (420, getWidth() / 2));
            const int w = cell.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
            {
                auto tab = cell.removeFromLeft (w).reduced (2, 0).toFloat();
                const bool active = i == 0;
                g.setColour ((active ? juce::Colour (cyan) : juce::Colour (0xff20263a)).withAlpha (active ? 0.26f : 0.50f));
                g.fillRoundedRectangle (tab, 6.0f);
                g.setColour ((active ? juce::Colour (lime) : textLo).withAlpha (active ? 0.95f : 0.65f));
                g.drawRoundedRectangle (tab.reduced (0.5f), 6.0f, 1.0f);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (names[i], tab.toNearestInt(), juce::Justification::centred);
            }

            auto preset = getLocalBounds().reduced (5, 4).withTrimmedLeft (430).withTrimmedRight (110);
            g.setColour (juce::Colour (0xaa0b1220));
            g.fillRoundedRectangle (preset.toFloat(), 5.0f);
            g.setColour (juce::Colour (cyan).withAlpha (0.55f));
            g.drawRoundedRectangle (preset.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (textHi);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("MONSTER BASS BANK | HYPER / EQ / LIMIT | WT+", preset.reduced (12, 0), juce::Justification::centredLeft);
        }
    };

    class ModRail : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (violet), 10.0f, 5);
            auto slot = getLocalBounds().reduced (8, 10);
            static const char* labels[] { "MOD", "ENV", "LFO", "VEL" };
            for (int i = 0; i < 4; ++i)
            {
                auto s = slot.removeFromTop (juce::jmax (54, (getHeight() - 24) / 4)).reduced (0, 4);
                g.setColour (juce::Colour (0x66090c16));
                g.fillRoundedRectangle (s.toFloat(), 8.0f);
                g.setColour ((i == 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.5f));
                g.drawRoundedRectangle (s.toFloat().reduced (0.5f), 8.0f, 1.0f);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.setColour (textLo);
                g.drawText (labels[i], s.removeFromTop (14), juce::Justification::centred);

                auto k = s.withSizeKeepingCentre (26, 26).toFloat();
                g.setColour (juce::Colour (0xff151a2c));
                g.fillEllipse (k);
                g.setColour ((i == 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.85f));
                g.drawEllipse (k.reduced (1.0f), 2.0f);
                g.drawLine (k.getCentreX(), k.getCentreY(), k.getCentreX() + 7.0f, k.getY() + 5.0f, 2.0f);
            }
        }
    };

    class KeyboardFooter : public juce::Component, private juce::Timer
    {
    public:
        explicit KeyboardFooter (ObsidianAudioProcessor& p) : processor (p)
        {
            startTimerHz (30);
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 8.0f, 3);

            const int lastNote = processor.getLastMidiNote();
            const float lastVel = processor.getLastMidiVelocity();
            const int activeCount = processor.getActiveNoteCount();
            const juce::String noteText = midiNoteName (lastNote);

            auto left = getLocalBounds().reduced (8, 7).removeFromLeft (140);
            auto readout = left.removeFromTop (27).toFloat();

            g.setColour (juce::Colour (0xee070a13));
            g.fillRoundedRectangle (readout, 6.0f);
            g.setColour ((activeCount > 0 ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (0.62f));
            g.drawRoundedRectangle (readout.reduced (0.5f), 6.0f, 1.1f);

            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.setColour (textLo);
            g.drawText ("NOTE", readout.withTrimmedLeft (9.0f).withWidth (38.0f), juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.setColour (activeCount > 0 ? juce::Colour (lime) : textHi.withAlpha (0.55f));
            g.drawText (noteText, readout.toNearestInt().reduced (43, 0), juce::Justification::centredLeft);

            auto stats = left.removeFromTop (24).toFloat();
            g.setColour (juce::Colour (0xaa101624));
            g.fillRoundedRectangle (stats, 5.0f);
            g.setColour (juce::Colour (magenta).withAlpha (0.42f));
            g.drawRoundedRectangle (stats.reduced (0.5f), 5.0f, 1.0f);

            const auto freqText = lastNote >= 0 ? juce::String (midiNoteFrequency (lastNote), 1) + " Hz" : "silent";
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.setColour (textLo);
            g.drawText (freqText, stats.toNearestInt().reduced (8, 0), juce::Justification::centredLeft);
            g.drawText (juce::String (activeCount) + " voices", stats.toNearestInt().reduced (8, 0), juce::Justification::centredRight);

            auto meters = left.removeFromTop (20).toFloat().reduced (0.0f, 3.0f);
            const float velocityNorm = juce::jlimit (0.0f, 1.0f, lastVel);
            const float gateNorm = juce::jlimit (0.0f, 1.0f, activeCount / 8.0f);
            const float leftPeak = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (processor.getOutputPeakL() + 0.0001f, -60.0f) / 60.0f + 1.0f);

            auto drawMiniMeter = [&] (juce::Rectangle<float> area, float value, juce::Colour accent, const juce::String& label)
            {
                g.setColour (juce::Colour (0xdd101624));
                g.fillRoundedRectangle (area, 4.0f);
                g.setColour (accent.withAlpha (0.75f));
                g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, value)).reduced (2.0f), 3.0f);
                g.setColour (textLo.withAlpha (0.75f));
                g.setFont (juce::FontOptions (7.2f, juce::Font::bold));
                g.drawText (label, area.toNearestInt(), juce::Justification::centred);
            };

            drawMiniMeter (meters.removeFromLeft (42.0f), velocityNorm, juce::Colour (lime), "VEL");
            meters.removeFromLeft (6.0f);
            drawMiniMeter (meters.removeFromLeft (42.0f), gateNorm, juce::Colour (cyan), "POLY");
            meters.removeFromLeft (6.0f);
            drawMiniMeter (meters, leftPeak, juce::Colour (magenta), "OUT");

            auto keys = getLocalBounds().reduced (158, 9).withTrimmedLeft (2);
            const int whiteCount = 36;
            const float whiteW = keys.getWidth() / (float) whiteCount;
            const float timePulse = 0.5f + 0.5f * std::sin (phase);

            for (int i = 0; i < whiteCount; ++i)
            {
                const int note = midiNoteForWhiteIndex (i);
                const float vel = processor.getActiveNoteVelocity (note);
                const bool active = vel > 0.0f;
                auto wk = juce::Rectangle<float> (keys.getX() + i * whiteW, (float) keys.getY(), whiteW - 1.0f, (float) keys.getHeight());

                juce::Colour keyFill = active ? juce::Colour (lime).interpolatedWith (juce::Colour (0xffe9fff0), 0.30f)
                                              : juce::Colour (0xffbde7ea);
                g.setColour (keyFill);
                g.fillRoundedRectangle (wk, 3.0f);

                if (active)
                {
                    g.setColour (juce::Colour (lime).withAlpha (0.22f + 0.20f * timePulse));
                    g.fillRoundedRectangle (wk.expanded (2.0f, 3.0f), 5.0f);
                    g.setColour (juce::Colour (0xffffffff).withAlpha (0.42f));
                    g.fillRoundedRectangle (wk.reduced (whiteW * 0.22f, 6.0f).withTrimmedTop (wk.getHeight() * (1.0f - vel)), 3.0f);
                }

                g.setColour ((active ? juce::Colour (lime) : juce::Colour (cyan)).withAlpha (active ? 0.95f : 0.25f));
                g.drawRoundedRectangle (wk.reduced (0.5f), 3.0f, active ? 1.8f : 1.0f);

                if (i % 7 == 0)
                {
                    g.setColour (active ? juce::Colour (0xff06120b) : juce::Colour (0x88080b12));
                    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
                    g.drawText (midiNoteName (note), wk.toNearestInt().withTrimmedTop (wk.getHeight() - 14.0f), juce::Justification::centred);
                }
            }

            for (int midi = midiNoteForWhiteIndex (0); midi < midiNoteForWhiteIndex (whiteCount - 1); ++midi)
            {
                const int semitone = midi % 12;
                if (isWhiteSemitone (semitone))
                    continue;

                int whiteIndexBefore = 0;
                while (whiteIndexBefore + 1 < whiteCount && midiNoteForWhiteIndex (whiteIndexBefore + 1) < midi)
                    ++whiteIndexBefore;

                const float vel = processor.getActiveNoteVelocity (midi);
                const bool active = vel > 0.0f;
                auto bk = juce::Rectangle<float> (keys.getX() + (whiteIndexBefore + 0.68f) * whiteW,
                                                  (float) keys.getY(), whiteW * 0.54f, keys.getHeight() * 0.62f);

                g.setColour (active ? juce::Colour (magenta).interpolatedWith (juce::Colour (0xff12051a), 0.18f)
                                    : juce::Colour (0xff0a1220));
                g.fillRoundedRectangle (bk, 3.0f);

                if (active)
                {
                    g.setColour (juce::Colour (magenta).withAlpha (0.24f + 0.20f * timePulse));
                    g.fillRoundedRectangle (bk.expanded (2.0f, 3.0f), 5.0f);
                    g.setColour (juce::Colour (0xffffffff).withAlpha (0.22f));
                    g.fillRoundedRectangle (bk.reduced (bk.getWidth() * 0.22f, 5.0f).withTrimmedTop (bk.getHeight() * (1.0f - vel)), 2.0f);
                }

                g.setColour ((active ? juce::Colour (magenta) : juce::Colour (cyan)).withAlpha (active ? 0.95f : 0.22f));
                g.drawRoundedRectangle (bk.reduced (0.5f), 3.0f, active ? 1.8f : 1.0f);
            }

            if (activeCount > 0)
            {
                auto scan = keys.toFloat().withTrimmedTop (keys.getHeight() - 4.0f);
                juce::ColourGradient glow (juce::Colour (cyan).withAlpha (0.0f), scan.getTopLeft(),
                                           juce::Colour (lime).withAlpha (0.48f + 0.20f * timePulse), scan.getCentre(), false);
                glow.addColour (1.0, juce::Colour (magenta).withAlpha (0.0f));
                g.setGradientFill (glow);
                g.fillRoundedRectangle (scan, 2.0f);
            }
        }

    private:
        void timerCallback() override
        {
            phase += 0.13f;
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
            repaint();
        }

        ObsidianAudioProcessor& processor;
        float phase = 0.0f;
    };

    //--------------------------------------------------------------------------
    // HeaderBar: logo, preset actions, wavetable import, master volume.
    //--------------------------------------------------------------------------
    class HeaderBar : public juce::Component, private juce::Timer
    {
    public:
        explicit HeaderBar (ObsidianAudioProcessor& p) : processor (p)
        {
            for (auto* b : { &initBtn, &loadBtn, &saveBtn, &wtBtn, &panicBtn })
                addAndMakeVisible (*b);

            addAndMakeVisible (presetBox);
            presetBox.addItem ("Bundled presets", 1);
            for (int i = 0; i < ObsidianBundledPresets::count; ++i)
                presetBox.addItem (ObsidianBundledPresets::presets[i].name, i + 2);
            presetBox.setSelectedId (1, juce::dontSendNotification);
            presetBox.onChange = [this] { loadBundledPreset(); };

            initBtn.onClick  = [this] { initPatch(); };
            loadBtn.onClick  = [this] { loadPreset(); };
            saveBtn.onClick  = [this] { savePreset(); };
            wtBtn.onClick    = [this] { loadWavetable(); };
            panicBtn.onClick = [this] { processor.panicAllNotes(); };
            panicBtn.setTooltip ("Kills every active voice instantly if a host misses a note-off.");

            phat.init (*this, processor.apvts, "phat", "", juce::Colour (lime));
            phat.label.setVisible (false);
            master.init (*this, processor.apvts, "master", "", juce::Colour (amber));
            master.label.setVisible (false);
            startTimerHz (24);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawGlassPanel (g, r, juce::Colour (cyan), 11.0f, 10);

            g.setColour (textHi);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText ("OBSIDIAN", 16, 0, 150, getHeight(), juce::Justification::centredLeft);

            g.setColour (juce::Colour (cyan).withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("WAVETABLE SYNTH", 17, getHeight() / 2 + 6, 150, 12, juce::Justification::topLeft);

            g.setColour (juce::Colour (cyan).withAlpha (0.4f));
            g.fillRect (16, getHeight() / 2 + 4, 118, 1);

            g.setColour (textLo);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("PHAT", phatLabelArea, juce::Justification::centredRight);
            g.drawText ("MASTER", masterLabelArea, juce::Justification::centredRight);

            auto meterBg = meterArea.toFloat();
            g.setColour (juce::Colour (0xaa050812));
            g.fillRoundedRectangle (meterBg, 5.0f);
            g.setColour (juce::Colour (cyan).withAlpha (0.45f));
            g.drawRoundedRectangle (meterBg.reduced (0.5f), 5.0f, 1.0f);

            auto drawMeter = [&] (juce::Rectangle<int> row, float v)
            {
                g.setColour (juce::Colour (0xff12182a));
                g.fillRoundedRectangle (row.toFloat(), 3.0f);
                const float norm = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (juce::jmax (v, 0.0001f), -60.0f) / 60.0f + 1.0f);
                auto fill = row.withWidth (juce::roundToInt (row.getWidth() * norm));
                juce::ColourGradient gr (juce::Colour (cyan), (float) fill.getX(), 0.0f,
                                         juce::Colour (lime), (float) fill.getRight(), 0.0f, false);
                g.setGradientFill (gr);
                g.fillRoundedRectangle (fill.toFloat(), 3.0f);
            };

            auto m = meterArea.reduced (8, 6);
            drawMeter (m.removeFromTop (8), processor.getOutputPeakL());
            m.removeFromTop (4);
            drawMeter (m.removeFromTop (8), processor.getOutputPeakR());
            g.setColour (textLo);
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText ("LEVEL", meterArea.reduced (8, 0), juce::Justification::centredRight);
        }

        void timerCallback() override
        {
            repaint (meterArea.expanded (4));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 5);

            auto knobArea = r.removeFromRight (r.getHeight() + 6);
            master.slider.setBounds (knobArea.reduced (1));
            masterLabelArea = r.removeFromRight (54);
            auto phatArea = r.removeFromRight (r.getHeight() + 6);
            phat.slider.setBounds (phatArea.reduced (1));
            phatLabelArea = r.removeFromRight (42);

            r.removeFromRight (8);
            meterArea = r.removeFromRight (132).reduced (2, 3);
            r.removeFromRight (8);
            panicBtn.setBounds (r.removeFromRight (72).reduced (2, 1));
            wtBtn.setBounds    (r.removeFromRight (118).reduced (2, 1));
            saveBtn.setBounds  (r.removeFromRight (72).reduced (2, 1));
            loadBtn.setBounds  (r.removeFromRight (72).reduced (2, 1));
            presetBox.setBounds (r.removeFromRight (210).reduced (2, 1));
            initBtn.setBounds  (r.removeFromRight (64).reduced (2, 1));
        }

    private:
        void loadBundledPreset()
        {
            const int idx = presetBox.getSelectedId() - 2;
            if (! juce::isPositiveAndBelow (idx, ObsidianBundledPresets::count))
                return;

            juce::String error;
            if (! processor.loadPresetXmlText (ObsidianBundledPresets::presets[idx].xml, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Obsidian", "Couldn't load bundled preset: " + error);
        }

        void loadWavetable()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load wavetable (WAV, 2048-sample frames)",
                                                           juce::File(), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file.existsAsFile() && ! processor.loadUserWavetable (file))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Obsidian", "Couldn't read that file.");
                });
        }

        void savePreset()
        {
            chooser = std::make_unique<juce::FileChooser> ("Save preset", juce::File(), "*.obsn");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File())
                        return;
                    if (auto xml = processor.apvts.copyState().createXml())
                        xml->writeTo (file.withFileExtension ("obsn"));
                });
        }

        void loadPreset()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load preset", juce::File(), "*.obsn");
            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (! file.existsAsFile())
                        return;
                    juce::String error;
                    if (! processor.loadPresetFile (file, &error))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Obsidian", "Couldn't load preset: " + error);
                });
        }

        void initPatch()
        {
            for (auto* param : processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                {
                    ranged->beginChangeGesture();
                    ranged->setValueNotifyingHost (ranged->getDefaultValue());
                    ranged->endChangeGesture();
                }
        }

        ObsidianAudioProcessor& processor;
        juce::TextButton initBtn { "INIT" }, loadBtn { "OPEN" }, saveBtn { "SAVE" }, wtBtn { "LOAD WT" }, panicBtn { "PANIC" };
        juce::ComboBox presetBox;
        Knob phat, master;
        juce::Rectangle<int> phatLabelArea, masterLabelArea, meterArea;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==========================================================================
    // MainView: Serum-style fixed bands.
    //   Row 1: OSC A | OSC B | SUB/NOISE | FILTER
    //   Row 2: ENV 1 | ENV 2 | LFO 1 | LFO 2 | VOICE
    //   Row 3: MOD MATRIX | FX RACK
    //==========================================================================
    class MainView : public juce::Component, private juce::Timer
    {
    public:
        explicit MainView (ObsidianAudioProcessor& p)
            : header (p),
              oscA (p, "oscA", "OSC A", juce::Colour (cyan)),
              oscB (p, "oscB", "OSC B", juce::Colour (magenta)),
              oscC (p, "oscC", "OSC C", juce::Colour (violet)),
              subNoise (p.apvts, juce::Colour (amber)),
              filter (p.apvts, juce::Colour (lime)),
              env1 (p.apvts, "amp",  "ENV 1 - AMP", juce::Colour (amber)),
              env2 (p.apvts, "env2", "ENV 2 - MOD", juce::Colour (amber)),
              lfo1 (p.apvts, 1, true,  "LFO 1", juce::Colour (cyan)),
              lfo2 (p.apvts, 2, false, "LFO 2", juce::Colour (magenta)),
              voice (p.apvts, juce::Colour (amber)),
              matrix (p.apvts, juce::Colour (lime)),
              fx (p.apvts, juce::Colour (violet)),
              keyboard (p)
        {
            setBufferedToImage (true);
            startTimerHz (7);
            for (auto* c : std::initializer_list<juce::Component*> {
                     &topTabs, &header, &rail, &oscA, &oscB, &oscC, &subNoise, &filter,
                     &env1, &env2, &lfo1, &lfo2, &voice, &matrix, &fx, &keyboard })
                addAndMakeVisible (*c);
        }

        void paint (juce::Graphics& g) override
        {
            juce::ColourGradient bg (juce::Colour (bg0), 0.0f, 0.0f,
                                     juce::Colour (bg1), (float) getWidth(), (float) getHeight(), false);
            bg.addColour (0.52, juce::Colour (0xff12142a));
            g.setGradientFill (bg);
            g.fillAll();

            // Static bloom field behind the cards. Kept intentionally static: animated full-editor
            // repaints are a common cause of DAW focus-change crackle on modest systems.
            const float pulse = 0.62f;
            juce::ColourGradient orb1 (juce::Colour (cyan).withAlpha (0.20f + 0.08f * pulse),
                                       getWidth() * (0.18f + 0.03f * pulse), getHeight() * 0.16f,
                                       juce::Colour (0x00000000), getWidth() * 0.78f, getHeight() * 0.92f, true);
            g.setGradientFill (orb1);
            g.fillAll();

            juce::ColourGradient orb2 (juce::Colour (magenta).withAlpha (0.12f + 0.07f * (1.0f - pulse)),
                                       getWidth() * 0.82f, getHeight() * (0.18f + 0.04f * pulse),
                                       juce::Colour (0x00000000), getWidth() * 0.22f, getHeight() * 0.75f, true);
            g.setGradientFill (orb2);
            g.fillAll();

            // Faint spectral grid, closer to Serum: readable alignment over noisy ornament.
            g.setColour (juce::Colour (cyan).withAlpha (0.030f));
            for (int x = 0; x < getWidth(); x += 32)
                g.drawVerticalLine (x, 0.0f, (float) getHeight());
            for (int y = 0; y < getHeight(); y += 28)
                g.drawHorizontalLine (y, 0.0f, (float) getWidth());

            g.setColour (juce::Colour (magenta).withAlpha (0.025f));
            for (int i = 90; i < getWidth(); i += 260)
                g.drawLine ((float) i, (float) getHeight(), (float) i + 80.0f, 0.0f, 1.0f);
        }

        void timerCallback() override
        {
            // Slow heartbeat repaint only. Child displays repaint themselves when parameters change.
            animPhase += 0.004f;
            if (animPhase > juce::MathConstants<float>::twoPi)
                animPhase -= juce::MathConstants<float>::twoPi;
            repaint();
        }

        void resized() override
        {
            constexpr int gap = 6;
            auto r = getLocalBounds().reduced (8);

            topTabs.setBounds (r.removeFromTop (34));
            r.removeFromTop (gap);
            header.setBounds (r.removeFromTop (48));
            r.removeFromTop (gap);

            keyboard.setBounds (r.removeFromBottom (66));
            r.removeFromBottom (gap);

            auto leftColumn = r.removeFromLeft (190);
            r.removeFromLeft (gap);

            // Serum parity pass: SUB / NOISE lives as a dedicated left module instead of
            // being squeezed between the oscillators and filter. This immediately cleans
            // the visual hierarchy and gives the oscillators breathing room.
            auto leftTop = leftColumn.removeFromTop (juce::roundToInt (leftColumn.getHeight() * 0.47f));
            subNoise.setBounds (leftTop);
            leftColumn.removeFromTop (gap);
            rail.setBounds (leftColumn);

            const int h = r.getHeight();
            auto oscBand = r.removeFromTop (juce::roundToInt (h * 0.40f));
            r.removeFromTop (gap);
            auto modBand = r.removeFromTop (juce::roundToInt (h * 0.265f));
            r.removeFromTop (gap);
            auto botBand = r;

            // --- Oscillator band
            {
                const int w = oscBand.getWidth();
                oscA.setBounds   (oscBand.removeFromLeft (juce::roundToInt (w * 0.255f)));
                oscBand.removeFromLeft (gap);
                oscB.setBounds   (oscBand.removeFromLeft (juce::roundToInt (w * 0.255f)));
                oscBand.removeFromLeft (gap);
                oscC.setBounds   (oscBand.removeFromLeft (juce::roundToInt (w * 0.255f)));
                oscBand.removeFromLeft (gap);
                filter.setBounds (oscBand);
            }

            // --- Modulation band
            {
                const int w = modBand.getWidth();
                env1.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                env2.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                lfo1.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.215f)));
                modBand.removeFromLeft (gap);
                lfo2.setBounds (modBand.removeFromLeft (juce::roundToInt (w * 0.185f)));
                modBand.removeFromLeft (gap);
                voice.setBounds (modBand);
            }

            // --- Bottom band
            {
                const int w = botBand.getWidth();
                matrix.setBounds (botBand.removeFromLeft (juce::roundToInt (w * 0.44f)));
                botBand.removeFromLeft (gap);
                fx.setBounds (botBand);
            }
        }

    private:
        TopTabStrip topTabs;
        HeaderBar header;
        ModRail rail;
        OscPanel oscA, oscB, oscC;
        SubNoisePanel subNoise;
        FilterPanel filter;
        EnvPanel env1, env2;
        LfoPanel lfo1, lfo2;
        VoicePanel voice;
        MatrixPanel matrix;
        FxPanel fx;
        KeyboardFooter keyboard;
        float animPhase = 0.0f;
    };
}

//==============================================================================
// Editor
//==============================================================================
ObsidianAudioProcessorEditor::ObsidianAudioProcessorEditor (ObsidianAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    mainView = std::make_unique<MainView> (processor);
    addAndMakeVisible (*mainView);

    setSize (1720, 1080);
    setResizable (true, true);
    setResizeLimits (1360, 940, 2600, 1700);
}

ObsidianAudioProcessorEditor::~ObsidianAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ObsidianAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (bg0));
}

void ObsidianAudioProcessorEditor::resized()
{
    mainView->setBounds (getLocalBounds());
}
