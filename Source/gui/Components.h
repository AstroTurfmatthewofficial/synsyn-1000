#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ForgeLookAndFeel.h"

// Reusable building blocks for the strip/section layout. New modules (Filter,
// LFO, Effects, Presets...) are added by subclassing SectionPanel and adding it
// to a Strip in the editor; no layout code elsewhere needs to change.
namespace vf::gui
{
    //==========================================================================
    // A titled panel: header bar + teal-outlined body. Subclasses lay out their
    // children inside getBodyBounds().
    class SectionPanel : public juce::Component
    {
    public:
        explicit SectionPanel (juce::String title);
        void paint (juce::Graphics&) override;

        static constexpr int headerHeight = 18;
        static constexpr int rowHeight = 30;   // standard height of one parameter cell
        juce::Rectangle<int> getBodyBounds() const;

        // Stack components top-to-bottom in equal rows.
        static void layoutRows (juce::Rectangle<int> area, std::initializer_list<juce::Component*> rows);

    private:
        juce::String title;
    };

    //==========================================================================
    // A horizontal row of sections with relative widths.
    class Strip final : public juce::Component
    {
    public:
        void addSection (std::unique_ptr<SectionPanel> section, float relativeWidth);
        void resized() override;

    private:
        struct Item { std::unique_ptr<SectionPanel> section; float weight; };
        std::vector<Item> items;
    };

    //==========================================================================
    // A parameter cell: LCD readout + label, bound to one host parameter.
    // Drag vertically / mouse-wheel to change, double-click to reset; choice
    // parameters open a menu on click.
    class LcdParam final : public juce::Component
    {
    public:
        LcdParam (juce::RangedAudioParameter& param, juce::String label, float lcdFraction = 0.58f);

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { repaint(); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void showChoiceMenu();
        float normalised() const { return param.convertTo0to1 (value); }

        juce::RangedAudioParameter& param;
        juce::String label;
        float lcdFraction;
        float value = 0.0f;
        float dragStartNorm = 0.0f;
        bool dragging = false, moved = false;
        juce::AudioParameterChoice* choice = nullptr;
        juce::AudioParameterBool* toggle = nullptr;
        juce::ParameterAttachment attachment;
    };

    //==========================================================================
    // Vertical fader bound to a parameter (SynprezFM-style).
    class ParamFader final : public juce::Component
    {
    public:
        ParamFader (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
        void resized() override { slider.setBounds (getLocalBounds()); }

    private:
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    //==========================================================================
    // Small graphic display that polls a few parameters and repaints on change.
    class ParamDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit ParamDisplay (std::vector<std::atomic<float>*> sources);
        void paint (juce::Graphics&) override;

    protected:
        virtual void drawContent (juce::Graphics&, juce::Rectangle<float> area) = 0;
        float get (size_t index) const { return lastValues[index]; }
        size_t numSources() const      { return lastValues.size(); }

    private:
        void timerCallback() override;
        std::vector<std::atomic<float>*> sources;
        std::vector<float> lastValues;
    };

    class EnvelopeDisplay final : public ParamDisplay
    {
    public:
        // sources: attack, decay, sustain, release
        using ParamDisplay::ParamDisplay;
    private:
        void drawContent (juce::Graphics&, juce::Rectangle<float>) override;
    };

    class FilterResponseDisplay final : public ParamDisplay
    {
    public:
        // sources: type, cutoff (Hz), resonance (0..1)
        using ParamDisplay::ParamDisplay;
    private:
        void drawContent (juce::Graphics&, juce::Rectangle<float>) override;
    };

    // LFO shape with a moving playhead dot. Animates continuously (30 Hz).
    class LfoDisplay final : public juce::Component, private juce::Timer
    {
    public:
        // sources: shape, target, amount
        LfoDisplay (std::vector<std::atomic<float>*> sources, std::function<float()> phaseGetter);
        void paint (juce::Graphics&) override;
    private:
        void timerCallback() override { repaint(); }
        std::vector<std::atomic<float>*> sources;
        std::function<float()> getPhase;
    };

    // Small effect illustrations. Chorus: sources rate, depth.
    // Delay: time (ms), feedback, ping-pong. Reverb: size, damping, pre-delay (ms).
    class FxDisplay final : public ParamDisplay
    {
    public:
        enum class Kind { chorus, delay, reverb };
        FxDisplay (Kind k, std::vector<std::atomic<float>*> srcs, std::atomic<float>* onFlag)
            : ParamDisplay (std::move (srcs)), kind (k), on (onFlag) {}
    private:
        void drawContent (juce::Graphics&, juce::Rectangle<float>) override;
        Kind kind;
        std::atomic<float>* on;
    };

    class WaveformDisplay final : public ParamDisplay
    {
    public:
        // sources: waveform index [, on/off]
        using ParamDisplay::ParamDisplay;
    private:
        void drawContent (juce::Graphics&, juce::Rectangle<float>) override;
    };

    //==========================================================================
    // MIDI input LED (M6) for the title bar: "MIDI" label + round red lamp.
    //  - dark:   no MIDI input confirmed
    //  - red:    MIDI input connected (see VSTForgeAudioProcessor::isMidiInputConnected)
    //  - bright: brief flash on every incoming channel message
    // Polls two lock-free getters from a 30 Hz UI timer; repaints only on change.
    class MidiLed final : public juce::Component, private juce::Timer
    {
    public:
        enum class State { off, on, flash };

        MidiLed (std::function<bool()> isConnected, std::function<uint32_t()> activityCount);
        ~MidiLed() override { stopTimer(); }

        void paint (juce::Graphics&) override;
        State getState() const noexcept { return state; }
        void poll();   // timer body (public for tests)

        static constexpr int flashMs = 90;

    private:
        void timerCallback() override { poll(); }

        std::function<bool()> connected;
        std::function<uint32_t()> activity;
        uint32_t lastCount = 0;
        double flashUntilMs = 0.0;
        State state = State::off;
    };
}
