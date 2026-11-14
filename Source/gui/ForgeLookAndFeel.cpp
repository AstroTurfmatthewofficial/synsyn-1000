#include "ForgeLookAndFeel.h"

namespace vf::gui
{
    ForgeLookAndFeel::ForgeLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::PopupMenu::backgroundColourId, colours::cell);
        setColour (juce::PopupMenu::textColourId, colours::label);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::outline);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::background);
        setColour (juce::TextButton::buttonColourId, colours::cell);
        setColour (juce::TextButton::textColourOffId, colours::trace);
        setColour (juce::TextButton::textColourOnId, colours::accent);
        setColour (juce::Label::textColourId, colours::label);
    }

    juce::Font ForgeLookAndFeel::lcdFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::bold));
    }

    juce::Font ForgeLookAndFeel::uiFont (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    void ForgeLookAndFeel::drawLcd (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                                    juce::Justification just, bool active)
    {
        // Bezel
        g.setColour (juce::Colour (0xff111111));
        g.fillRect (r);
        auto glass = r.reduced (2.0f);
        g.setColour (active ? colours::lcdBack.brighter (0.08f) : colours::lcdBack);
        g.fillRect (glass);

        // Dot-matrix feel: faint pixel grid.
        g.setColour (colours::lcdGhost);
        for (float x = glass.getX() + 1.5f; x < glass.getRight(); x += 3.0f)
            g.drawVerticalLine ((int) x, glass.getY(), glass.getBottom());
        for (float y = glass.getY() + 1.5f; y < glass.getBottom(); y += 3.0f)
            g.drawHorizontalLine ((int) y, glass.getX(), glass.getRight());

        g.setColour (colours::lcdText);
        g.setFont (lcdFont (juce::jmin (glass.getHeight() * 0.8f, 20.0f)));
        g.drawFittedText (text, glass.reduced (4.0f, 0.0f).toNearestInt(), just, 1, 0.7f);

        if (active)
        {
            g.setColour (colours::accent);
            g.drawRect (r, 1.0f);
        }
    }

    void ForgeLookAndFeel::drawCell (juce::Graphics& g, juce::Rectangle<float> r, bool hover, juce::Colour outline)
    {
        g.setColour (hover ? colours::cellHover : colours::cell);
        g.fillRect (r);
        g.setColour (outline);
        g.drawRect (r, 1.0f);
    }

    void ForgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float sliderPos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, slider);
            return;
        }

        // SynprezFM-style fader: dark well, orange outline, thumb drawn as twin bars.
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        g.setColour (colours::cell);
        g.fillRect (r);
        g.setColour (colours::accent.withAlpha (0.8f));
        g.drawRect (r, 1.0f);

        auto well = r.reduced (4.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (well.withSizeKeepingCentre (4.0f, well.getHeight()));

        // Filled part below the thumb.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                             && slider.getProperties().contains ("bipolar");
        const float zeroY = bipolar ? (float) slider.getPositionOfValue (0.0) : well.getBottom();
        g.setColour (colours::accent.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (well.getX(), juce::jmin (zeroY, sliderPos),
                                                                 well.getRight(), juce::jmax (zeroY, sliderPos)));

        g.setColour (colours::accent);
        const float tw = well.getWidth();
        g.fillRect (well.getX(), sliderPos - 4.0f, tw, 2.0f);
        g.fillRect (well.getX(), sliderPos + 2.0f, tw, 2.0f);
        g.setColour (colours::accent.withAlpha (0.5f));
        g.fillRect (well.getX(), sliderPos - 1.0f, tw, 2.0f);
    }

    void ForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat();
        drawCell (g, r, highlighted || down, down ? colours::accent : colours::outline);
    }

    void ForgeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (colours::cell);
        g.setColour (colours::outline);
        g.drawRect (0, 0, width, height, 1);
    }
}
