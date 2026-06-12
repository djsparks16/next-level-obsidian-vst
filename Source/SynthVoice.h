#pragma once
#include <JuceHeader.h>
#include "Wavetable.h"
#include "Modulation.h"

//==============================================================================
class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// SynthVoice
//
// Per-voice signal path:
//   Osc A (warp/FM) + Osc B (warp), each with up to 7 unison copies
//   + sub oscillator + noise
//     -> drive -> filter (SVF LP/BP/HP or Ladder 24dB)
//     -> amp envelope -> pan
//
// Modulation: Env2 + LFO1 hardwired to cutoff (amount knobs) plus an 8-slot
// mod matrix (Env2/LFO1/LFO2/velocity/mod wheel/note/random -> 11 dests),
// evaluated at control rate (every 32 samples).
//==============================================================================
class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int maxUnison = 16;
    static constexpr int controlInterval = 16; // tighter control blocks reduce filter/LFO zipper clicks

    SynthVoice (juce::AudioProcessorValueTreeState& state,
                const WavetableBank& b,
                const std::atomic<double>& bpm)
        : apvts (state), bank (b), hostBpm (bpm)
    {
        auto p = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

        pAType = p ("oscAType"); pATable = p ("oscATable"); pAInterp = p ("oscAInterp"); pAMorph = p ("oscAMorph"); pALevel = p ("oscALevel");
        pAWarpMode = p ("oscAWarpMode"); pAWarpAmt = p ("oscAWarpAmt");
        pASemi = p ("oscASemi"); pAFine = p ("oscAFine"); pAPan = p ("oscAPan"); pAScan = p ("oscAScan"); pADensity = p ("oscADensity");

        pBType = p ("oscBType"); pBTable = p ("oscBTable"); pBInterp = p ("oscBInterp"); pBMorph = p ("oscBMorph"); pBLevel = p ("oscBLevel");
        pBWarpMode = p ("oscBWarpMode"); pBWarpAmt = p ("oscBWarpAmt");
        pBSemi = p ("oscBSemi"); pBFine = p ("oscBFine"); pBPan = p ("oscBPan"); pBScan = p ("oscBScan"); pBDensity = p ("oscBDensity");

        pCType = p ("oscCType"); pCTable = p ("oscCTable"); pCInterp = p ("oscCInterp"); pCMorph = p ("oscCMorph"); pCLevel = p ("oscCLevel");
        pCWarpMode = p ("oscCWarpMode"); pCWarpAmt = p ("oscCWarpAmt");
        pCSemi = p ("oscCSemi"); pCFine = p ("oscCFine"); pCPan = p ("oscCPan"); pCScan = p ("oscCScan"); pCDensity = p ("oscCDensity");

        pSubLevel = p ("subLevel"); pSubOct = p ("subOct"); pSubWave = p ("subWave"); pSubPan = p ("subPan");
        pNoiseLevel = p ("noiseLevel"); pNoiseColour = p ("noiseColour"); pNoisePan = p ("noisePan");

        pUniCount = p ("uniCount"); pUniDetune = p ("uniDetune"); pUniWidth = p ("uniWidth");
        pGlide = p ("glideTime"); pBendRange = p ("bendRange"); pOscQuality = p ("oscQuality");

        pAmpA = p ("ampA"); pAmpD = p ("ampD"); pAmpS = p ("ampS"); pAmpR = p ("ampR");
        pEnv2A = p ("env2A"); pEnv2D = p ("env2D"); pEnv2S = p ("env2S"); pEnv2R = p ("env2R");

        pFltRouting = p ("fltRouting"); pFltModel = p ("fltModel"); pCutoff = p ("cutoff"); pReso = p ("reso");
        pFltEnvAmt = p ("fltEnvAmt"); pFltDrive = p ("fltDrive");
        pFlt2Model = p ("flt2Model"); pCutoff2 = p ("cutoff2"); pReso2 = p ("reso2"); pFlt2Drive = p ("flt2Drive"); pFltBlend = p ("fltBlend");

        pLfo1Shape = p ("lfo1Shape"); pLfo1Rate = p ("lfo1Rate");
        pLfo1Sync = p ("lfo1Sync"); pLfo1Div = p ("lfo1Div"); pLfo1Cut = p ("lfo1Cut");
        pLfo1DrawA = p ("lfo1DrawA"); pLfo1DrawB = p ("lfo1DrawB");
        pLfo2Shape = p ("lfo2Shape"); pLfo2Rate = p ("lfo2Rate");
        pLfo2Sync = p ("lfo2Sync"); pLfo2Div = p ("lfo2Div");
        pLfo2DrawA = p ("lfo2DrawA"); pLfo2DrawB = p ("lfo2DrawB");

        for (int s = 0; s < Mod::numSlots; ++s)
        {
            const auto n = juce::String (s + 1);
            pModSrc[(size_t) s] = p ("mod" + n + "Src");
            pModDst[(size_t) s] = p ("mod" + n + "Dst");
            pModAmt[(size_t) s] = p ("mod" + n + "Amt");
        }
    }

    void prepare (double sr, int blockSize)
    {
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
        svf.prepare (spec);
        svf2.prepare (spec);
        ladder.prepare (spec);
        ladder2.prepare (spec);
        ampEnv.setSampleRate (sr);
        env2.setSampleRate (sr);
        for (auto& o : oscA) o.setSampleRate (sr);
        for (auto& o : oscB) o.setSampleRate (sr);
        for (auto& o : oscC) o.setSampleRate (sr);
        scratch.setSize (2, blockSize);
        glideHz.reset (sr, 0.0);
        glideHz.setCurrentAndTargetValue (440.0);
        cutoffHz.reset (sr, 0.010);
        resoSmooth.reset (sr, 0.010);
        cutoff2Hz.reset (sr, 0.010);
        reso2Smooth.reset (sr, 0.010);
        cutoffHz.setCurrentAndTargetValue (18000.0f);
        resoSmooth.setCurrentAndTargetValue (0.707f);
        cutoff2Hz.setCurrentAndTargetValue (18000.0f);
        reso2Smooth.setCurrentAndTargetValue (0.707f);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int wheelPos) override
    {
        const double targetHz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        const double glideSecs = pGlide->load();
        const double startHz   = (haveLastNote && glideSecs > 0.001) ? glideHz.getCurrentValue()
                                                                     : targetHz;
        glideHz.reset (getSampleRate(), glideSecs);
        glideHz.setCurrentAndTargetValue (startHz);
        glideHz.setTargetValue (targetHz);
        haveLastNote = true;

        vel       = velocity;
        noteVal   = juce::jlimit (-1.0f, 1.0f, (midiNote - 60) / 36.0f);
        randVal   = rng.nextFloat() * 2.0f - 1.0f;
        bendNorm  = (wheelPos - 8192) / 8192.0f;

        updateEnvelopes();
        ampEnv.noteOn();
        env2.noteOn();

        svf.reset(); svf2.reset();
        ladder.reset(); ladder2.reset();
        lfo1.reset (rng);
        lfo2.reset (rng);
        subPhase = 0.0;
        cutoffHz.setCurrentAndTargetValue (juce::jlimit (20.0f, 20000.0f, pCutoff->load()));
        resoSmooth.setCurrentAndTargetValue (juce::jlimit (0.1f, 8.0f, pReso->load()));

        const int count = juce::jlimit (1, maxUnison, (int) pUniCount->load());
        for (int u = 0; u < count; ++u)
        {
            const double ph = count == 1 ? 0.0 : rng.nextDouble();
            oscA[(size_t) u].resetPhase (ph);
            oscB[(size_t) u].resetPhase (count == 1 ? 0.0 : rng.nextDouble());
            oscC[(size_t) u].resetPhase (count == 1 ? 0.0 : rng.nextDouble());
        }
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            env2.noteOff();
        }
        else
        {
            clearCurrentNote();
            ampEnv.reset();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        bendNorm = (newValue - 8192) / 8192.0f;
    }

    void controllerMoved (int controller, int newValue) override
    {
        if (controller == 1)
            modWheel = newValue / 127.0f;
    }

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        updateEnvelopes();

        const auto* tableA = &bank.get ((int) pATable->load());
        const auto* tableB = &bank.get ((int) pBTable->load());
        const auto* tableC = &bank.get ((int) pCTable->load());
        for (auto& o : oscA) o.setTable (tableA);
        for (auto& o : oscB) o.setTable (tableB);
        for (auto& o : oscC) o.setTable (tableC);

        const int   warpModeA = (int) pAWarpMode->load();
        const int   warpModeBRaw = (int) pBWarpMode->load();
        const int   warpModeB = warpModeBRaw <= 6 ? warpModeBRaw : warpModeBRaw + 3; // B/C menu skips A-only FM/AM/RM
        const int   warpModeCRaw = (int) pCWarpMode->load();
        const int   warpModeC = warpModeCRaw <= 6 ? warpModeCRaw : warpModeCRaw + 3;
        const float lvlSub    = pSubLevel->load();
        const float lvlNoise  = pNoiseLevel->load();
        const int   subOctave = (int) pSubOct->load() + 1;
        const int   subWave   = (int) pSubWave->load(); // 0 sine, 1 triangle, 2 square, 3 saw
        const float drive     = pFltDrive->load();
        const float drive2    = pFlt2Drive->load();
        const int   fltModel  = (int) pFltModel->load();
        const int   flt2Model = (int) pFlt2Model->load();
        const bool  parallelFilters = pFltRouting->load() > 0.5f;
        const float filterBlend = pFltBlend->load();
        const int   uniCount  = juce::jlimit (1, maxUnison, (int) pUniCount->load());
        const int   osFactor  = 1 << juce::jlimit (0, 3, (int) pOscQuality->load());
        const float uniNorm   = 1.0f / std::sqrt ((float) uniCount);
        const float uniWidth  = pUniWidth->load();
        const double sr       = getSampleRate();
        const double bpm      = juce::jmax (20.0, hostBpm.load());

        scratch.clear();

        int pos = 0;
        while (pos < numSamples)
        {
            const int len = juce::jmin (controlInterval, numSamples - pos);
            const double dt = len / sr;

            //==========================================================
            // 1) Control-rate: advance mod sources
            //==========================================================
            float env2v = 0.0f;
            for (int i = 0; i < len; ++i)
                env2v = env2.getNextSample();

            const double lfo1Rate = pLfo1Sync->load() > 0.5f
                ? (bpm / 60.0) / Mod::divBeats[(int) pLfo1Div->load()]
                : (double) pLfo1Rate->load();
            const double lfo2Rate = pLfo2Sync->load() > 0.5f
                ? (bpm / 60.0) / Mod::divBeats[(int) pLfo2Div->load()]
                : (double) pLfo2Rate->load();

            const float l1 = lfo1.tick (dt, (int) pLfo1Shape->load(), lfo1Rate, rng,
                                      pLfo1DrawA->load(), pLfo1DrawB->load());
            const float l2 = lfo2.tick (dt, (int) pLfo2Shape->load(), lfo2Rate, rng,
                                      pLfo2DrawA->load(), pLfo2DrawB->load());

            const float sources[Mod::numSources] =
                { 0.0f, env2v, l1, l2, vel, modWheel, noteVal, randVal };

            //==========================================================
            // 2) Evaluate the mod matrix
            //==========================================================
            float dest[Mod::numDests] = {};
            for (int s = 0; s < Mod::numSlots; ++s)
            {
                const int src = (int) pModSrc[(size_t) s]->load();
                const int dst = (int) pModDst[(size_t) s]->load();
                if (src > 0 && dst > 0)
                    dest[dst] += pModAmt[(size_t) s]->load() * sources[src];
            }

            //==========================================================
            // 3) Apply modulation to per-block targets
            //==========================================================
            const float morphA = juce::jlimit (0.0f, 1.0f, pAMorph->load() + dest[Mod::dAMorph]);
            const float morphB = juce::jlimit (0.0f, 1.0f, pBMorph->load() + dest[Mod::dBMorph]);
            const float morphC = juce::jlimit (0.0f, 1.0f, pCMorph->load());
            const float lvlA   = juce::jlimit (0.0f, 1.0f, pALevel->load() + dest[Mod::dALevel]);
            const float lvlB   = juce::jlimit (0.0f, 1.0f, pBLevel->load() + dest[Mod::dBLevel]);
            const float lvlC   = juce::jlimit (0.0f, 1.0f, pCLevel->load());
            const float warpA  = juce::jlimit (0.0f, 1.0f, pAWarpAmt->load() + dest[Mod::dWarpA]);
            const float warpB  = juce::jlimit (0.0f, 1.0f, pBWarpAmt->load() + dest[Mod::dWarpB]);
            const float warpC  = juce::jlimit (0.0f, 1.0f, pCWarpAmt->load());

            const double baseHz    = glideHz.skip (len);
            const double pitchSemi = pBendRange->load() * bendNorm + 24.0 * dest[Mod::dPitch];
            const double noteHz    = baseHz * std::pow (2.0, pitchSemi / 12.0);

            const double semiA = pASemi->load() + pAFine->load() / 100.0;
            const double semiB = pBSemi->load() + pBFine->load() / 100.0;
            const double semiC = pCSemi->load() + pCFine->load() / 100.0;
            const double hzA   = noteHz * std::pow (2.0, semiA / 12.0);
            const double hzB   = noteHz * std::pow (2.0, semiB / 12.0);
            const double hzC   = noteHz * std::pow (2.0, semiC / 12.0);
            const double hzSub = noteHz / std::pow (2.0, (double) subOctave);

            const float detune = juce::jmax (0.0f, pUniDetune->load()
                                                     + 100.0f * dest[Mod::dUniDetune]);

            for (int u = 0; u < uniCount; ++u)
            {
                const float offset = uniCount == 1
                                       ? 0.0f
                                       : (u / float (uniCount - 1) - 0.5f) * 2.0f;
                const double ratio = std::pow (2.0, offset * detune / 1200.0);

                oscA[(size_t) u].setFrequency (hzA * ratio);
                oscB[(size_t) u].setFrequency (hzB * ratio);
                oscC[(size_t) u].setFrequency (hzC * ratio);
                oscA[(size_t) u].setMorph (morphA);
                oscB[(size_t) u].setMorph (morphB);
                oscC[(size_t) u].setMorph (morphC);

                const float panPos = 0.5f + offset * 0.5f * uniWidth;
                gainL[(size_t) u] = std::cos (panPos * juce::MathConstants<float>::halfPi);
                gainR[(size_t) u] = std::sin (panPos * juce::MathConstants<float>::halfPi);
            }

            // Voice pan (mod only): simple balance law
            const float panMod = juce::jlimit (-1.0f, 1.0f, dest[Mod::dPan]);
            const float panL   = panMod <= 0.0f ? 1.0f : 1.0f - panMod;
            const float panR   = panMod >= 0.0f ? 1.0f : 1.0f + panMod;
            auto panPair = [] (float pan) { return std::array<float, 2> { pan <= 0.0f ? 1.0f : 1.0f - pan,
                                                                          pan >= 0.0f ? 1.0f : 1.0f + pan }; };
            const auto panA = panPair (pAPan->load());
            const auto panB = panPair (pBPan->load());
            const auto panC = panPair (pCPan->load());
            const auto panSub = panPair (pSubPan->load());
            const auto panNoise = panPair (pNoisePan->load());

            // Filter setup
            const float cutOct = pFltEnvAmt->load() * 5.0f * env2v
                               + pLfo1Cut->load()   * 3.0f * l1
                               + 5.0f * dest[Mod::dCutoff];
            const float cutoffTarget = juce::jlimit (20.0f, 20000.0f,
                                                     pCutoff->load() * std::pow (2.0f, cutOct));
            const float resoTarget  = juce::jlimit (0.1f, 8.0f, pReso->load() + 8.0f * dest[Mod::dReso]);
            const float cutoff2Target = juce::jlimit (20.0f, 20000.0f, pCutoff2->load() * std::pow (2.0f, cutOct * 0.5f));
            const float reso2Target  = juce::jlimit (0.1f, 8.0f, pReso2->load() + 4.0f * dest[Mod::dReso]);
            cutoffHz.setTargetValue (cutoffTarget);
            resoSmooth.setTargetValue (resoTarget);
            cutoff2Hz.setTargetValue (cutoff2Target);
            reso2Smooth.setTargetValue (reso2Target);
            const float cutoff = juce::jlimit (20.0f, 20000.0f, cutoffHz.skip (len));
            const float resoV  = juce::jlimit (0.1f, 8.0f, resoSmooth.skip (len));
            const float cutoff2 = juce::jlimit (20.0f, 20000.0f, cutoff2Hz.skip (len));
            const float reso2V  = juce::jlimit (0.1f, 8.0f, reso2Smooth.skip (len));

            //==========================================================
            // 4) Audio-rate rendering
            //==========================================================
            auto* L = scratch.getWritePointer (0, pos);
            auto* R = scratch.getWritePointer (1, pos);

            const double subInc   = hzSub / sr;
            const float  driveAmt = 1.0f + 9.0f * drive;

            for (int i = 0; i < len; ++i)
            {
                float sl = 0.0f, srgt = 0.0f;

                for (int u = 0; u < uniCount; ++u)
                {
                    const float bs = oscB[(size_t) u].getNextSample (warpModeB, warpB, 0.0f, osFactor);
                    const float fm = warpModeA == WavetableOscillator::warpFM
                                       ? warpA * 0.5f * bs : 0.0f;
                    const int   modeA = (warpModeA == WavetableOscillator::warpFM
                                       || warpModeA == WavetableOscillator::warpAM
                                       || warpModeA == WavetableOscillator::warpRM)
                                          ? WavetableOscillator::warpOff : warpModeA;
                    float as = oscA[(size_t) u].getNextSample (modeA, warpA, fm, osFactor);
                    if (warpModeA == WavetableOscillator::warpAM)
                        as *= 1.0f - warpA + warpA * (0.5f + 0.5f * bs);
                    else if (warpModeA == WavetableOscillator::warpRM)
                        as *= (1.0f - warpA) + warpA * bs;

                    float cs = oscC[(size_t) u].getNextSample (warpModeC, warpC, 0.0f, osFactor);
                    auto modeColour = [] (float x, int type, float scan, float density) noexcept
                    {
                        switch (type)
                        {
                            case 1:  return std::tanh (x * (1.0f + 1.8f * density)); // multisample-style saturated layer
                            case 2:  return x * (0.75f + 0.25f * std::sin (scan * juce::MathConstants<float>::twoPi)); // sample/tape rate colour
                            case 3:  return x * (0.55f + 0.45f * std::sin ((x + scan) * 32.0f * (0.25f + density))); // granular shimmer
                            case 4:  return std::sin (std::asin (juce::jlimit (-0.999f, 0.999f, x)) * (1.0f + 3.0f * scan)); // spectral partial bend
                            default: return x;
                        }
                    };
                    as = modeColour (as, (int) pAType->load(), pAScan->load(), pADensity->load());
                    const float bCol = modeColour (bs, (int) pBType->load(), pBScan->load(), pBDensity->load());
                    cs = modeColour (cs, (int) pCType->load(), pCScan->load(), pCDensity->load());

                    sl   += (as * lvlA * panA[0] + bCol * lvlB * panB[0] + cs * lvlC * panC[0]) * gainL[(size_t) u];
                    srgt += (as * lvlA * panA[1] + bCol * lvlB * panB[1] + cs * lvlC * panC[1]) * gainR[(size_t) u];
                }

                sl   *= uniNorm;
                srgt *= uniNorm;

                float subShape = 0.0f;
                if (subWave == 1)
                    subShape = 1.0f - 4.0f * std::abs ((float) subPhase - 0.5f);
                else if (subWave == 2)
                    subShape = subPhase < 0.5 ? 1.0f : -1.0f;
                else if (subWave == 3)
                    subShape = 2.0f * (float) subPhase - 1.0f;
                else
                    subShape = (float) std::sin (juce::MathConstants<double>::twoPi * subPhase);

                const float sub = subShape * lvlSub;
                subPhase += subInc;
                subPhase -= std::floor (subPhase);

                float rawNoise = rng.nextFloat() * 2.0f - 1.0f;
                const int ncol = (int) pNoiseColour->load();
                noiseState = 0.96f * noiseState + 0.04f * rawNoise;
                const float noiseShaped = ncol == 1 ? noiseState * 1.8f
                                        : ncol == 2 ? noiseState * 0.9f
                                        : ncol == 3 ? rawNoise - noiseState
                                                    : rawNoise;
                const float noise = noiseShaped * lvlNoise;

                float l = (sl + sub * panSub[0] + noise * panNoise[0]) * vel;
                float r = (srgt + sub * panSub[1] + noise * panNoise[1]) * vel;

                if (drive > 0.01f)
                {
                    l = std::tanh (l * driveAmt);
                    r = std::tanh (r * driveAmt);
                }

                L[i] = l * panL;
                R[i] = r * panR;
            }

            //==========================================================
            // 5) Filter + amp envelope for this sub-block
            //==========================================================
            auto block = juce::dsp::AudioBlock<float> (scratch)
                             .getSubBlock ((size_t) pos, (size_t) len);
            auto ctx = juce::dsp::ProcessContextReplacing<float> (block);

            auto runFilter = [] (int model, float cutoffIn, float resoIn,
                                  juce::dsp::StateVariableTPTFilter<float>& svfRef,
                                  juce::dsp::LadderFilter<float>& ladderRef,
                                  juce::dsp::ProcessContextReplacing<float>& context)
            {
                if (model == 3 || model == 4 || model == 5 || model == 6)
                {
                    ladderRef.setMode (juce::dsp::LadderFilterMode::LPF24);
                    ladderRef.setCutoffFrequencyHz (cutoffIn);
                    ladderRef.setResonance (juce::jlimit (0.0f, 1.0f, (resoIn - 0.1f) / 7.9f));
                    ladderRef.process (context);
                }
                else
                {
                    svfRef.setType (model == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                 : model == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                              : juce::dsp::StateVariableTPTFilterType::lowpass);
                    svfRef.setCutoffFrequency (cutoffIn);
                    svfRef.setResonance (resoIn);
                    svfRef.process (context);
                }
            };

            if (parallelFilters)
            {
                parallelScratch.setSize (2, numSamples, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    parallelScratch.copyFrom (ch, pos, scratch, ch, pos, len);

                auto block2 = juce::dsp::AudioBlock<float> (parallelScratch).getSubBlock ((size_t) pos, (size_t) len);
                auto ctx2 = juce::dsp::ProcessContextReplacing<float> (block2);
                runFilter (fltModel, cutoff, resoV, svf, ladder, ctx);
                runFilter (flt2Model, cutoff2, reso2V, svf2, ladder2, ctx2);

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* a = scratch.getWritePointer (ch, pos);
                    auto* b = parallelScratch.getReadPointer (ch, pos);
                    for (int i = 0; i < len; ++i)
                        a[i] = a[i] * (1.0f - filterBlend) + b[i] * filterBlend;
                }
            }
            else
            {
                runFilter (fltModel, cutoff, resoV, svf, ladder, ctx);
                if (drive2 > 0.01f)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* d = scratch.getWritePointer (ch, pos);
                        const float amt = 1.0f + 8.0f * drive2;
                        for (int i = 0; i < len; ++i)
                            d[i] = std::tanh (d[i] * amt);
                    }
                }
                runFilter (flt2Model, cutoff2, reso2V, svf2, ladder2, ctx);
            }

            ampEnv.applyEnvelopeToBuffer (scratch, pos, len);
            pos += len;
        }

        for (int ch = 0; ch < juce::jmin (2, output.getNumChannels()); ++ch)
            output.addFrom (ch, startSample, scratch, ch, 0, numSamples);

        if (! ampEnv.isActive())
            clearCurrentNote();
    }

private:
    void updateEnvelopes()
    {
        ampEnv.setParameters ({ pAmpA->load(), pAmpD->load(), pAmpS->load(), pAmpR->load() });
        env2.setParameters ({ pEnv2A->load(), pEnv2D->load(), pEnv2S->load(), pEnv2R->load() });
    }

    juce::AudioProcessorValueTreeState& apvts;
    const WavetableBank& bank;
    const std::atomic<double>& hostBpm;

    std::array<WavetableOscillator, maxUnison> oscA, oscB, oscC;
    std::array<float, maxUnison> gainL {}, gainR {};

    juce::ADSR ampEnv, env2;
    LFO lfo1, lfo2;
    juce::dsp::StateVariableTPTFilter<float> svf, svf2;
    juce::dsp::LadderFilter<float> ladder, ladder2;
    juce::AudioBuffer<float> scratch, parallelScratch;
    juce::Random rng;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> glideHz;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHz;
    juce::SmoothedValue<float> resoSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoff2Hz;
    juce::SmoothedValue<float> reso2Smooth;
    bool haveLastNote = false;

    double subPhase = 0.0;
    float  vel = 1.0f, modWheel = 0.0f, noteVal = 0.0f, randVal = 0.0f, bendNorm = 0.0f, noiseState = 0.0f;

    // Cached parameter pointers
    std::atomic<float> *pAType {}, *pATable {}, *pAInterp {}, *pAMorph {}, *pALevel {}, *pAWarpMode {}, *pAWarpAmt {},
                       *pASemi {}, *pAFine {}, *pAPan {}, *pAScan {}, *pADensity {},
                       *pBType {}, *pBTable {}, *pBInterp {}, *pBMorph {}, *pBLevel {}, *pBWarpMode {}, *pBWarpAmt {},
                       *pBSemi {}, *pBFine {}, *pBPan {}, *pBScan {}, *pBDensity {},
                       *pCType {}, *pCTable {}, *pCInterp {}, *pCMorph {}, *pCLevel {}, *pCWarpMode {}, *pCWarpAmt {},
                       *pCSemi {}, *pCFine {}, *pCPan {}, *pCScan {}, *pCDensity {},
                       *pSubLevel {}, *pSubOct {}, *pSubWave {}, *pSubPan {}, *pNoiseLevel {}, *pNoiseColour {}, *pNoisePan {},
                       *pUniCount {}, *pUniDetune {}, *pUniWidth {},
                       *pGlide {}, *pBendRange {}, *pOscQuality {},
                       *pAmpA {}, *pAmpD {}, *pAmpS {}, *pAmpR {},
                       *pEnv2A {}, *pEnv2D {}, *pEnv2S {}, *pEnv2R {},
                       *pFltRouting {}, *pFltModel {}, *pCutoff {}, *pReso {}, *pFltEnvAmt {}, *pFltDrive {},
                       *pFlt2Model {}, *pCutoff2 {}, *pReso2 {}, *pFlt2Drive {}, *pFltBlend {},
                       *pLfo1Shape {}, *pLfo1Rate {}, *pLfo1Sync {}, *pLfo1Div {}, *pLfo1Cut {},
                       *pLfo1DrawA {}, *pLfo1DrawB {},
                       *pLfo2Shape {}, *pLfo2Rate {}, *pLfo2Sync {}, *pLfo2Div {},
                       *pLfo2DrawA {}, *pLfo2DrawB {};

    std::array<std::atomic<float>*, Mod::numSlots> pModSrc {}, pModDst {}, pModAmt {};
};
