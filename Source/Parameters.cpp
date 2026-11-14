#include "Parameters.h"

namespace vf::params
{
    using juce::ParameterID;

    juce::String oscId (int oscIndex, const char* suffix)
    {
        return "osc" + juce::String (oscIndex + 1) + "_" + suffix;
    }

    juce::String lfoId (int lfoIndex, const char* suffix)
    {
        return "lfo" + juce::String (lfoIndex + 1) + "_" + suffix;
    }

    juce::StringArray filterTypeNames() { return { "Low-pass", "High-pass", "Band-pass" }; }

    static juce::NormalisableRange<float> timeRange (float minS, float maxS, float centreS)
    {
        juce::NormalisableRange<float> r (minS, maxS);
        r.setSkewForCentre (centreS);
        return r;
    }

    static juce::AudioParameterFloatAttributes secondsAttr()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("s")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                                : juce::String (v, 2) + " s";
            });
    }

    static juce::AudioParameterFloatAttributes percentAttr (bool bipolar = false)
    {
        return juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([bipolar] (float v, int)
            {
                const int pc = juce::roundToInt (v * 100.0f);
                return (bipolar && pc > 0 ? "+" : "") + juce::String (pc) + " %";
            });
    }

    static void addEnvelope (juce::AudioProcessorParameterGroup& g, const char* a, const char* d, const char* s, const char* r,
                             const juce::String& prefix, int version, float defA, float defD, float defS, float defR)
    {
        g.addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { a, version }, prefix + " Attack",  timeRange (0.001f, 5.0f, 0.3f),  defA, secondsAttr()));
        g.addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { d, version }, prefix + " Decay",   timeRange (0.001f, 5.0f, 0.3f),  defD, secondsAttr()));
        g.addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { s, version }, prefix + " Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), defS, percentAttr()));
        g.addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { r, version }, prefix + " Release", timeRange (0.001f, 10.0f, 0.5f), defR, secondsAttr()));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // Oscillators. Osc 1 keeps its M1 wave ID/version; its new params and
        // osc 2/3 are M2. Defaults keep M1 sessions sounding the same
        // (osc 1 on at full level, osc 2/3 off, filter fully open).
        for (int i = 0; i < numOscillators; ++i)
        {
            const auto name = "Osc " + juce::String (i + 1);
            auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("osc" + juce::String (i + 1), "Oscillator " + juce::String (i + 1), "|");
            g->addChild (std::make_unique<juce::AudioParameterChoice> (
                ParameterID { oscId (i, "wave"), i == 0 ? parameterVersion : parameterVersionM2 }, name + " Waveform",
                vf::dsp::waveformNames(), (int) vf::dsp::Waveform::saw));
            g->addChild (std::make_unique<juce::AudioParameterBool> (
                ParameterID { oscId (i, "on"), parameterVersionM2 }, name + " On", i == 0));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (
                ParameterID { oscId (i, "level"), parameterVersionM2 }, name + " Level",
                juce::NormalisableRange<float> (0.0f, 1.0f), i == 0 ? 1.0f : 0.8f, percentAttr()));
            g->addChild (std::make_unique<juce::AudioParameterInt> (
                ParameterID { oscId (i, "coarse"), parameterVersionM2 }, name + " Coarse", -24, 24, 0,
                juce::AudioParameterIntAttributes().withLabel ("st")
                    .withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (
                ParameterID { oscId (i, "fine"), parameterVersionM2 }, name + " Fine",
                juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ct")
                    .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " ct"; })));
            layout.add (std::move (g));
        }

        auto amp = std::make_unique<juce::AudioProcessorParameterGroup> ("ampenv", "Amp Envelope", "|");
        addEnvelope (*amp, id::attack, id::decay, id::sustain, id::release, "Amp", parameterVersion, 0.005f, 0.25f, 0.7f, 0.3f);

        auto filter = std::make_unique<juce::AudioProcessorParameterGroup> ("filter", "Filter", "|");
        filter->addChild (std::make_unique<juce::AudioParameterChoice> (
            ParameterID { id::filterType, parameterVersionM2 }, "Filter Type", filterTypeNames(), 0));
        juce::NormalisableRange<float> cutoffRange (20.0f, 20000.0f,
            [] (float s, float e, float n) { return s * std::pow (e / s, n); },
            [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); });
        filter->addChild (std::make_unique<juce::AudioParameterFloat> (
            ParameterID { id::filterCutoff, parameterVersionM2 }, "Filter Cutoff", cutoffRange, 20000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int)
                    { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; })));
        filter->addChild (std::make_unique<juce::AudioParameterFloat> (
            ParameterID { id::filterRes, parameterVersionM2 }, "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttr()));
        filter->addChild (std::make_unique<juce::AudioParameterFloat> (
            ParameterID { id::filterKeytrack, parameterVersionM2 }, "Filter Key Track", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttr()));
        filter->addChild (std::make_unique<juce::AudioParameterFloat> (
            ParameterID { id::filterEnvAmt, parameterVersionM2 }, "Filter Env Amount", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percentAttr (true)));

        auto fenv = std::make_unique<juce::AudioProcessorParameterGroup> ("filterenv", "Filter Envelope", "|");
        addEnvelope (*fenv, id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease, "Filter Env", parameterVersionM2, 0.005f, 0.4f, 0.3f, 0.4f);

        // LFOs (M3). Defaults: target None, amount 0 -> no effect on older sessions.
        std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> lfoGroups;
        for (int i = 0; i < numLfos; ++i)
        {
            const auto name = "LFO " + juce::String (i + 1);
            const int v = parameterVersionM3;
            auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("lfo" + juce::String (i + 1), name, "|");
            g->addChild (std::make_unique<juce::AudioParameterChoice> (ParameterID { lfoId (i, "wave"), v }, name + " Waveform",
                                                                       vf::dsp::lfoShapeNames(), 0));
            juce::NormalisableRange<float> rateRange (0.01f, 40.0f,
                [] (float a, float b, float n) { return a * std::pow (b / a, n); },
                [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); });
            g->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { lfoId (i, "rate"), v }, name + " Rate", rateRange, 2.0f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float x, int)
                    { return juce::String (x, x < 1.0f ? 3 : (x < 10.0f ? 2 : 1)) + " Hz"; })));
            g->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { lfoId (i, "sync"), v }, name + " Tempo Sync", false));
            g->addChild (std::make_unique<juce::AudioParameterChoice> (ParameterID { lfoId (i, "div"), v }, name + " Sync Division",
                                                                       vf::dsp::lfoDivisionNames(), vf::dsp::defaultLfoDivision));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { lfoId (i, "depth"), v }, name + " Depth",
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttr()));
            g->addChild (std::make_unique<juce::AudioParameterChoice> (ParameterID { lfoId (i, "mode"), v }, name + " Mode",
                                                                       vf::dsp::lfoModeNames(), 0));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { lfoId (i, "delay"), v }, name + " Delay (fade-in)",
                                                                      timeRange (0.0f, 5.0f, 0.5f), 0.0f, secondsAttr()));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { lfoId (i, "phase"), v }, name + " Phase",
                juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("deg").withStringFromValueFunction ([] (float x, int)
                    { return juce::String (juce::roundToInt (x)) + " deg"; })));
            g->addChild (std::make_unique<juce::AudioParameterChoice> (ParameterID { lfoId (i, "target"), v }, name + " Target",
                                                                       vf::dsp::lfoTargetNames(), 0));
            g->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { lfoId (i, "amount"), v }, name + " Amount",
                                                                      juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percentAttr (true)));
            if (i == 0)
                g->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::lfo1ModWheel, v }, "LFO 1 Mod Wheel Depth", false));
            lfoGroups.push_back (std::move (g));
        }

        // Effects (M4). All default OFF -> older sessions are unchanged.
        auto logRange = [] (float a, float b)
        {
            return juce::NormalisableRange<float> (a, b,
                [] (float s0, float e, float n) { return s0 * std::pow (e / s0, n); },
                [] (float s0, float e, float v) { return std::log (v / s0) / std::log (e / s0); });
        };
        const int v4 = parameterVersionM4;
        auto hzAttr = juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float x, int)
            { return x < 1000.0f ? juce::String (x, x < 10.0f ? 2 : 0) + " Hz" : juce::String (x / 1000.0f, 1) + " kHz"; });
        auto msAttr = juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float x, int)
            { return x < 1000.0f ? juce::String (x, x < 10.0f ? 1 : 0) + " ms" : juce::String (x / 1000.0f, 2) + " s"; });

        auto chorus = std::make_unique<juce::AudioProcessorParameterGroup> ("chorus", "Chorus", "|");
        chorus->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::chorusOn, v4 }, "Chorus On", false));
        chorus->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::chorusRate, v4 }, "Chorus Rate", logRange (0.05f, 5.0f), 0.6f, hzAttr));
        chorus->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::chorusDepth, v4 }, "Chorus Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentAttr()));
        chorus->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::chorusMix, v4 }, "Chorus Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentAttr()));

        auto delay = std::make_unique<juce::AudioProcessorParameterGroup> ("delay", "Delay", "|");
        delay->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::delayOn, v4 }, "Delay On", false));
        delay->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::delayTime, v4 }, "Delay Time", logRange (1.0f, 2000.0f), 375.0f, msAttr));
        delay->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::delaySync, v4 }, "Delay Tempo Sync", false));
        delay->addChild (std::make_unique<juce::AudioParameterChoice> (ParameterID { id::delayDiv, v4 }, "Delay Sync Division",
                                                                       vf::dsp::lfoDivisionNames(), 10 /* 1/8 D */));
        delay->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::delayFeedback, v4 }, "Delay Feedback",
                                                                      juce::NormalisableRange<float> (0.0f, 0.95f), 0.35f, percentAttr()));
        delay->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::delayPingPong, v4 }, "Delay Ping-Pong", false));
        delay->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::delayHighCut, v4 }, "Delay High Cut", logRange (500.0f, 20000.0f), 6000.0f, hzAttr));
        delay->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::delayMix, v4 }, "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, percentAttr()));

        auto reverb = std::make_unique<juce::AudioProcessorParameterGroup> ("reverb", "Reverb", "|");
        reverb->addChild (std::make_unique<juce::AudioParameterBool> (ParameterID { id::reverbOn, v4 }, "Reverb On", false));
        reverb->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::reverbSize, v4 }, "Reverb Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f, percentAttr()));
        reverb->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::reverbDamping, v4 }, "Reverb Damping", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentAttr()));
        reverb->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::reverbWidth, v4 }, "Reverb Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttr()));
        reverb->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::reverbPreDelay, v4 }, "Reverb Pre-delay",
                                                                       juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 20.0f, msAttr));
        reverb->addChild (std::make_unique<juce::AudioParameterFloat> (ParameterID { id::reverbMix, v4 }, "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, percentAttr()));

        auto master = std::make_unique<juce::AudioProcessorParameterGroup> ("master", "Master", "|");
        master->addChild (std::make_unique<juce::AudioParameterFloat> (
            ParameterID { id::gain, parameterVersion }, "Master Gain",
            juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                    { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; })));

        layout.add (std::move (amp), std::move (filter), std::move (fenv));
        for (auto& g : lfoGroups) layout.add (std::move (g));
        layout.add (std::move (chorus), std::move (delay), std::move (reverb), std::move (master));
        return layout;
    }

    void Handles::bind (juce::AudioProcessorValueTreeState& s)
    {
        auto get = [&s] (const juce::String& pid)
        {
            auto* p = s.getRawParameterValue (pid);
            jassert (p != nullptr);
            return p;
        };
        for (int i = 0; i < numOscillators; ++i)
            osc[(size_t) i] = { get (oscId (i, "wave")), get (oscId (i, "on")), get (oscId (i, "level")),
                                get (oscId (i, "coarse")), get (oscId (i, "fine")) };
        attack = get (id::attack); decay = get (id::decay); sustain = get (id::sustain); release = get (id::release);
        gain = get (id::gain);
        filterType = get (id::filterType); filterCutoff = get (id::filterCutoff); filterRes = get (id::filterRes);
        filterKeytrack = get (id::filterKeytrack); filterEnvAmt = get (id::filterEnvAmt);
        for (int i = 0; i < numLfos; ++i)
            lfo[(size_t) i] = { get (lfoId (i, "wave")), get (lfoId (i, "rate")), get (lfoId (i, "sync")), get (lfoId (i, "div")),
                                get (lfoId (i, "depth")), get (lfoId (i, "mode")), get (lfoId (i, "delay")), get (lfoId (i, "phase")),
                                get (lfoId (i, "target")), get (lfoId (i, "amount")) };
        lfo1ModWheel = get (id::lfo1ModWheel);
        chorusOn = get (id::chorusOn); chorusRate = get (id::chorusRate); chorusDepth = get (id::chorusDepth); chorusMix = get (id::chorusMix);
        delayOn = get (id::delayOn); delayTime = get (id::delayTime); delaySync = get (id::delaySync); delayDiv = get (id::delayDiv);
        delayFeedback = get (id::delayFeedback); delayPingPong = get (id::delayPingPong); delayHighCut = get (id::delayHighCut); delayMix = get (id::delayMix);
        reverbOn = get (id::reverbOn); reverbSize = get (id::reverbSize); reverbDamping = get (id::reverbDamping);
        reverbWidth = get (id::reverbWidth); reverbPreDelay = get (id::reverbPreDelay); reverbMix = get (id::reverbMix);
        fenvAttack = get (id::fenvAttack); fenvDecay = get (id::fenvDecay); fenvSustain = get (id::fenvSustain); fenvRelease = get (id::fenvRelease);
    }
}
