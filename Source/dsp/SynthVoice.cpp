#include "SynthVoice.h"

namespace vf::dsp
{
    SynthVoice::SynthVoice (int voiceIndex)
        : phaseSeed (0x2545F491u + (uint32_t) voiceIndex * 2654435761u),
          lfoSeed (0x6C8E9CF5u + (uint32_t) voiceIndex * 2246822519u)
    {
        for (int i = 0; i < numOscillators; ++i)
            osc[(size_t) i].seedNoise (0x12345u + (uint32_t) (voiceIndex * numOscillators + i) * 2654435761u);
    }

    void SynthVoice::prepare (double sampleRate, int)
    {
        setCurrentPlaybackSampleRate (sampleRate);
        for (auto& o : osc) o.prepare (sampleRate);
        for (auto& l : oscLevel) l.reset (sampleRate, 0.01);
        filter.prepare (sampleRate);
        ampEnv.setSampleRate (sampleRate);
        filterEnv.setSampleRate (sampleRate);
        velocityGain.reset (sampleRate, 0.005);
        sampleRateHz = sampleRate;
        lfoSmoothingCoeff = 1.0f - (float) std::exp (-1.0 / (lfoSmoothingSeconds * sampleRate));
        cutoffOctaves.reset (sampleRate, 0.01);
        cutoffOctaves.setCurrentAndTargetValue (std::log2 (filterParams.cutoffHz));
    }

    void SynthVoice::setParams (const VoiceParams& p) noexcept
    {
        for (size_t i = 0; i < (size_t) numOscillators; ++i)
        {
            osc[i].setWaveform (p.osc[i].waveform);
            oscLevel[i].setTargetValue (p.osc[i].on ? p.osc[i].level : 0.0f);
            oscPitchOffset[i] = p.osc[i].coarseSemis + p.osc[i].fineCents * 0.01f;
        }
        ampEnv.setParameters (p.ampEnv);
        filterEnv.setParameters (p.filterEnv);

        filterParams = p.filter;
        filter.setType (p.filter.type);
        filter.setResonance (p.filter.resonance);
        baseResonance = p.filter.resonance;
        lfoParams = p.lfo;
        cutoffOctaves.setTargetValue (std::log2 (juce::jmax (20.0f, p.filter.cutoffHz)));

        updateFrequencies();
    }

    bool SynthVoice::canPlaySound (juce::SynthesiserSound* s)
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void SynthVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel)
    {
        currentNote = midiNote;
        pitchWheelValue = pitchWheel;
        updateFrequencies();

        const bool wasSounding = ampEnv.isActive();
        if (! wasSounding)
        {
            // Osc 1 starts at phase 0 (as in M1); osc 2/3 get decorrelated start
            // phases so stacked/detuned oscillators don't start phase-aligned.
            for (size_t i = 0; i < osc.size(); ++i)
            {
                phaseSeed = phaseSeed * 1664525u + 1013904223u;
                osc[i].resetPhase (i == 0 ? 0.0 : (double) (phaseSeed >> 8) / 16777216.0);
            }
            for (auto& l : oscLevel) l.setCurrentAndTargetValue (l.getTargetValue());
            filter.reset();
            cutoffOctaves.setCurrentAndTargetValue (cutoffOctaves.getTargetValue());
        }

        const float target = voiceLevel * velocity;
        // Stolen voice still sounding: glide the level; fresh voice: jump.
        if (wasSounding) velocityGain.setTargetValue (target);
        else             velocityGain.setCurrentAndTargetValue (target);

        // LFOs: retrigger-mode LFOs restart at their phase offset; every LFO's
        // delay/fade-in restarts; sample & hold gets a fresh per-note seed.
        samplesSinceNoteOn = 0;
        for (size_t k = 0; k < lfoState.size(); ++k)
        {
            auto& st = lfoState[k];
            // Separate generator: must not disturb phaseSeed (osc start phases),
            // otherwise sessions without LFOs would no longer match v0.2.0.
            lfoSeed = lfoSeed * 1664525u + 1013904223u;
            st.seed = lfoSeed;
            st.position = 0.0;
            if (! wasSounding) { st.smoothedValue = 0.0f; st.smoothedAbs = 0.0f; }
        }

        ampEnv.noteOn();
        filterEnv.noteOn();
    }

    void SynthVoice::stopNote (float, bool allowTailOff)
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            filterEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            filterEnv.reset();
            clearCurrentNote();
            currentNote = -1;
        }
    }

    void SynthVoice::pitchWheelMoved (int newValue)
    {
        pitchWheelValue = newValue;
        updateFrequencies();
    }

    void SynthVoice::updateFrequencies() noexcept
    {
        if (currentNote < 0) return;
        const double bend = ((double) pitchWheelValue - 8192.0) / 8192.0 * pitchBendRangeSemis;
        for (size_t i = 0; i < osc.size(); ++i)
            osc[i].setFrequency (440.0 * std::pow (2.0, ((double) currentNote + bend + oscPitchOffset[i] - 69.0) / 12.0));
    }

    void SynthVoice::applyPitchModulation (const float* oscSemis) noexcept
    {
        if (currentNote < 0) return;
        const double bend = ((double) pitchWheelValue - 8192.0) / 8192.0 * pitchBendRangeSemis;
        for (size_t i = 0; i < osc.size(); ++i)
            osc[i].setFrequency (440.0 * std::pow (2.0, ((double) currentNote + bend + oscPitchOffset[i] + oscSemis[i] - 69.0) / 12.0));
    }

    void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
    {
        if (! isVoiceActive() || ! ampEnv.isActive())
            return;

        const int numCh = output.getNumChannels();
        auto* const* ch = output.getArrayOfWritePointers();
        const float keyOct = filterParams.keytrack * (float) (currentNote - 60) / 12.0f;
        const float envOct = filterParams.envAmount * envOctaves;

        // Which LFOs need evaluating (active, or still gliding back to zero).
        bool anyLfo = false;
        std::array<bool, numLfos> lfoOn {};
        for (size_t k = 0; k < lfoParams.size(); ++k)
        {
            auto& st = lfoState[k];
            lfoOn[k] = lfoParams[k].target != LfoTarget::none
                       && (lfoParams[k].isActive() || std::abs (st.smoothedValue) > 1.0e-6f || st.smoothedAbs > 1.0e-6f);
            if (! lfoOn[k]) { st.smoothedValue = 0.0f; st.smoothedAbs = 0.0f; }
            anyLfo |= lfoOn[k];
        }

        if (! anyLfo)
        {
            // Unmodulated path (bit-identical to v0.2.0).
            for (int i = 0; i < numSamples; ++i)
            {
                const float env = ampEnv.getNextSample();
                const float fenv = filterEnv.getNextSample();
                const float cutOct = cutoffOctaves.getNextValue();

                if ((i % controlInterval) == 0)
                    filter.setCutoff (std::exp2 ((double) (cutOct + keyOct + envOct * fenv)));

                float mix = 0.0f;
                for (size_t o = 0; o < osc.size(); ++o)
                {
                    const float lvl = oscLevel[o].getNextValue();
                    if (lvl > 0.0f || oscLevel[o].isSmoothing())
                        mix += osc[o].nextSample() * lvl;
                    else
                        osc[o].nextSample(); // keep phase running so re-enabling is phase-continuous
                }

                const float s = filter.process (mix) * env * velocityGain.getNextValue();
                const int idx = startSample + i;
                for (int c = 0; c < numCh; ++c)
                    ch[c][idx] += s;

                ++samplesSinceNoteOn;
                if (! ampEnv.isActive())
                {
                    clearCurrentNote();
                    currentNote = -1;
                    break;
                }
            }
            return;
        }

        // Modulated path. LFOs run per sample (smoothed); pitch, cutoff and
        // resonance are applied at control rate (every controlInterval samples),
        // levels and amp per sample.
        const float fadeScale = (float) sampleRateHz;
        bool pitchTouched = false;

        for (int i = 0; i < numSamples; ++i)
        {
            float cutMod = 0.0f, resMod = 0.0f, pitchAll = 0.0f;
            float oscPitch[numOscillators] = { 0.0f, 0.0f, 0.0f };
            float oscGain[numOscillators] = { 1.0f, 1.0f, 1.0f };
            float ampGain = 1.0f;
            bool resTouched = false, pitchNow = false;

            for (size_t k = 0; k < lfoParams.size(); ++k)
            {
                if (! lfoOn[k]) continue;
                const auto& lp = lfoParams[k];
                auto& st = lfoState[k];

                const double pos = lp.freeRunning ? lp.globalPosition + (double) (startSample + i) * lp.increment
                                                  : st.position;
                const uint32_t seed = lp.freeRunning ? lp.globalSeed : st.seed;
                const float v = Lfo::evaluate (lp.shape, pos + (double) lp.phaseOffset, seed);
                if (! lp.freeRunning) st.position += lp.increment;

                const float fade = lp.delaySeconds > 0.0f
                                     ? juce::jmin (1.0f, (float) samplesSinceNoteOn / (lp.delaySeconds * fadeScale))
                                     : 1.0f;
                const float a = lp.depth * fade * lp.amount;
                st.smoothedValue += lfoSmoothingCoeff * (v * a - st.smoothedValue);
                st.smoothedAbs   += lfoSmoothingCoeff * (std::abs (a) - st.smoothedAbs);

                const float m = st.smoothedValue;
                // Gain targets: range [1 - |a|, 1], never boosts (safe tremolo).
                const float gain = 1.0f - 0.5f * st.smoothedAbs + 0.5f * m;

                switch (lp.target)
                {
                    case LfoTarget::cutoff:    cutMod += m * LfoRanges::cutoffOctaves; break;
                    case LfoTarget::resonance: resMod += m * LfoRanges::resonance; resTouched = true; break;
                    case LfoTarget::pitch:     pitchAll += m * LfoRanges::pitchSemitones; pitchNow = true; break;
                    case LfoTarget::osc1Pitch: oscPitch[0] += m * LfoRanges::pitchSemitones; pitchNow = true; break;
                    case LfoTarget::osc2Pitch: oscPitch[1] += m * LfoRanges::pitchSemitones; pitchNow = true; break;
                    case LfoTarget::osc3Pitch: oscPitch[2] += m * LfoRanges::pitchSemitones; pitchNow = true; break;
                    case LfoTarget::osc1Level: oscGain[0] *= gain; break;
                    case LfoTarget::osc2Level: oscGain[1] *= gain; break;
                    case LfoTarget::osc3Level: oscGain[2] *= gain; break;
                    case LfoTarget::amp:       ampGain *= gain; break;
                    case LfoTarget::none:
                    case LfoTarget::numTargets:
                    default: break;
                }
            }

            const float env = ampEnv.getNextSample();
            const float fenv = filterEnv.getNextSample();
            const float cutOct = cutoffOctaves.getNextValue();

            if ((i % controlInterval) == 0)
            {
                if (resTouched)
                    filter.setResonance (juce::jlimit (0.0, 1.0, (double) (baseResonance + resMod)));
                filter.setCutoff (std::exp2 ((double) (cutOct + keyOct + envOct * fenv + cutMod)));
                if (pitchNow)
                {
                    for (auto& op : oscPitch) op += pitchAll;
                    applyPitchModulation (oscPitch);
                    pitchTouched = true;
                }
            }

            float mix = 0.0f;
            for (size_t o = 0; o < osc.size(); ++o)
            {
                const float lvl = oscLevel[o].getNextValue();
                if (lvl > 0.0f || oscLevel[o].isSmoothing())
                    mix += osc[o].nextSample() * lvl * oscGain[o];
                else
                    osc[o].nextSample();
            }

            const float s = filter.process (mix) * env * velocityGain.getNextValue() * ampGain;
            const int idx = startSample + i;
            for (int c = 0; c < numCh; ++c)
                ch[c][idx] += s;

            ++samplesSinceNoteOn;
            if (! ampEnv.isActive())
            {
                clearCurrentNote();
                currentNote = -1;
                break;
            }
        }

        juce::ignoreUnused (pitchTouched); // base pitch is restored by setParams() at the next block
    }
}
