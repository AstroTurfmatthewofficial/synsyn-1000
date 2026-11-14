#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Visual style for SynSyn 1000. Layout reference: the SynprezFM app
// (black background, teal-outlined section strips, pale-green LCD readouts,
// orange accents). All colours and drawing primitives live here so new
// sections only compose existing pieces.
namespace vf::gui
{
    namespace colours
    {
        inline const juce::Colour background  { 0xff000000 };
        inline const juce::Colour cell        { 0xff262626 };
        inline const juce::Colour cellHover   { 0xff2e2e2e };
        inline const juce::Colour outline     { 0xff2f8f8f }; // teal
        inline const juce::Colour outlineDim  { 0xff1d5656 };
        inline const juce::Colour header      { 0xff050a0a };
        inline const juce::Colour headerText  { 0xff9fdede };
        inline const juce::Colour label       { 0xffd8d8d8 };
        inline const juce::Colour accent      { 0xffe8822a }; // orange
        inline const juce::Colour lcdBack     { 0xffa9cdb9 }; // pale green
        inline const juce::Colour lcdBackDim  { 0xff8fb3a0 };
        inline const juce::Colour lcdText     { 0xff17221d };
        inline const juce::Colour lcdGhost    { 0x1a17221d };
        inline const juce::Colour display     { 0xff000000 };
        inline const juce::Colour trace       { 0xff3fb7b7 };
    }

    class ForgeLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        ForgeLookAndFeel();

        // Shared primitives used by the custom components.
        static juce::Font lcdFont (float height);
        static juce::Font uiFont (float height, bool bold = false);
        static void drawLcd (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text,
                             juce::Justification just = juce::Justification::centredLeft, bool active = false);
        static void drawCell (juce::Graphics&, juce::Rectangle<float> area, bool hover = false,
                              juce::Colour outline = colours::outline);

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;

        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        juce::Font getPopupMenuFont() override { return lcdFont (16.0f); }
    };
}
