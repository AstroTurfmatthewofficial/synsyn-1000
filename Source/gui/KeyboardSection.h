#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Components.h"

class VSTForgeAudioProcessor;

namespace vf::gui
{
    // Bottom strip: header bar (Pitch/Mod. label, octave arrows, range overview)
    // and a full-width on-screen keyboard with pitch/mod wheels at the left.
    class KeyboardSection final : public juce::Component
    {
    public:
        explicit KeyboardSection (VSTForgeAudioProcessor&);
        ~KeyboardSection() override;
        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int barHeight = 22;
        static constexpr int wheelColumnWidth = 74;
        static constexpr int visibleWhiteKeys = 29; // ~4 octaves

    private:
        class Arrow;
        class Overview;

        void shiftOctave (int delta);
        void setLowestKey (int note);

        int lowestKey = 36; // C2 (middle C = C4)

        VSTForgeAudioProcessor& processor;
        juce::MidiKeyboardComponent keyboard;
        juce::Slider pitch { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::Slider mod   { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        std::unique_ptr<Arrow> down, up;
        std::unique_ptr<Overview> overview;
    };
}
