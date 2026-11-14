#pragma once

#include "Components.h"

// Editor sections. Add new modules as further SectionPanel subclasses.
namespace vf::gui
{
    class VolumeSection final : public SectionPanel
    {
    public:
        explicit VolumeSection (juce::AudioProcessorValueTreeState&);
        void resized() override;
    private:
        LcdParam readout;
        ParamFader fader;
    };

    // Oscillator N: wave, level, coarse, fine | on/off + waveform display.
    class OscillatorSection final : public SectionPanel
    {
    public:
        OscillatorSection (juce::AudioProcessorValueTreeState&, int oscIndex);
        void resized() override;
    private:
        LcdParam wave, level, coarse, fine, on;
        WaveformDisplay display;
    };

    // Generic ADSR section (amp envelope, filter envelope, later mod envelopes).
    class EnvelopeSection final : public SectionPanel
    {
    public:
        EnvelopeSection (juce::AudioProcessorValueTreeState&, const juce::String& title,
                         const char* attackId, const char* decayId, const char* sustainId, const char* releaseId);
        void resized() override;
    private:
        LcdParam attack, decay, sustain, release;
        EnvelopeDisplay display;
    };

    // LFO N: wave, rate, sync, division | target, amount, depth, mode |
    // delay, phase, (mod wheel for LFO 1) | live display.
    class LfoSection final : public SectionPanel
    {
    public:
        LfoSection (juce::AudioProcessorValueTreeState&, int lfoIndex, std::function<float()> phaseGetter);
        void resized() override;
    private:
        LcdParam wave, rate, sync, division, target, amount, depth, mode, delay, phase;
        std::unique_ptr<LcdParam> modWheel;
        LfoDisplay display;
    };

    class ChorusSection final : public SectionPanel
    {
    public:
        explicit ChorusSection (juce::AudioProcessorValueTreeState&);
        void resized() override;
    private:
        LcdParam on, rate, depth, mix;
        FxDisplay display;
    };

    class DelaySection final : public SectionPanel
    {
    public:
        explicit DelaySection (juce::AudioProcessorValueTreeState&);
        void resized() override;
    private:
        LcdParam on, time, sync, division, feedback, pingPong, highCut, mix;
        FxDisplay display;
    };

    class ReverbSection final : public SectionPanel
    {
    public:
        explicit ReverbSection (juce::AudioProcessorValueTreeState&);
        void resized() override;
    private:
        LcdParam on, size, damping, width, preDelay, mix;
        FxDisplay display;
    };

    // Filter: type, cutoff, resonance, key track | env amount + response curve.
    class FilterSection final : public SectionPanel
    {
    public:
        explicit FilterSection (juce::AudioProcessorValueTreeState&);
        void resized() override;
    private:
        LcdParam type, cutoff, resonance, keytrack, envAmount;
        FilterResponseDisplay display;
    };
}
