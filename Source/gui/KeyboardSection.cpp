#include "KeyboardSection.h"
#include "PluginProcessor.h"

namespace vf::gui
{
    //==========================================================================
    class KeyboardSection::Arrow final : public juce::Button
    {
    public:
        Arrow (bool pointsRight) : juce::Button (pointsRight ? "Octave up" : "Octave down"), right (pointsRight) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (colours::background);
            g.fillRect (r);
            auto a = r.withSizeKeepingCentre (26.0f, 12.0f);
            juce::Path p; // hollow block arrow, like SynprezFM's
            if (right)
                p.addArrow ({ a.getX(), a.getCentreY(), a.getRight(), a.getCentreY() }, 5.0f, 12.0f, 9.0f);
            else
                p.addArrow ({ a.getRight(), a.getCentreY(), a.getX(), a.getCentreY() }, 5.0f, 12.0f, 9.0f);
            g.setColour ((down || over) ? colours::accent : colours::trace);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }

    private:
        bool right;
    };

    //==========================================================================
    // Mini 128-key keyboard showing (and setting) the visible range.
    class KeyboardSection::Overview final : public juce::Component
    {
    public:
        explicit Overview (juce::MidiKeyboardComponent& k) : kb (k) {}

        std::function<void (int)> onJump;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (colours::background);
            g.fillRect (r);
            const float kw = r.getWidth() / 75.0f; // 75 white keys in 0..127
            int white = 0;
            for (int n = 0; n < 128; ++n)
                if (! juce::MidiMessage::isMidiNoteBlack (n))
                {
                    g.setColour (juce::Colours::white);
                    g.fillRect (r.getX() + white * kw + 0.5f, r.getY() + 2.0f, kw - 1.0f, r.getHeight() - 4.0f);
                    ++white;
                }
            white = 0;
            for (int n = 0; n < 128; ++n)
            {
                if (juce::MidiMessage::isMidiNoteBlack (n))
                {
                    g.setColour (juce::Colours::black);
                    g.fillRect (r.getX() + white * kw - kw * 0.3f, r.getY() + 2.0f, kw * 0.6f, (r.getHeight() - 4.0f) * 0.6f);
                }
                else ++white;
            }

            const float x0 = xForNote (kb.getLowestVisibleKey(), kw);
            int last = kb.getLowestVisibleKey(), whites = 0;
            while (last < 127 && whites < visibleWhiteKeys)
                if (! juce::MidiMessage::isMidiNoteBlack (last++)) ++whites;
            const float x1 = xForNote (last, kw);
            g.setColour (colours::accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()));
            g.setColour (colours::accent);
            g.drawRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()), 1.5f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const float kw = (float) getWidth() / 75.0f;
            const int whiteIndex = (int) ((float) e.x / kw);
            const int octave = whiteIndex / 7;
            if (onJump) onJump (octave * 12);
        }

    private:
        static float xForNote (int note, float kw)
        {
            int white = 0;
            for (int n = 0; n < juce::jmin (note, 128); ++n)
                if (! juce::MidiMessage::isMidiNoteBlack (n)) ++white;
            return (float) white * kw;
        }
        juce::MidiKeyboardComponent& kb;
    };

    //==========================================================================
    KeyboardSection::KeyboardSection (VSTForgeAudioProcessor& p)
        : processor (p),
          keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
          down (std::make_unique<Arrow> (false)),
          up (std::make_unique<Arrow> (true)),
          overview (std::make_unique<Overview> (keyboard))
    {
        keyboard.setScrollButtonsVisible (false);
        keyboard.setOctaveForMiddleC (4);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colours::white);
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff101010));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff4a5fd0));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::accent.withAlpha (0.85f));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::trace.withAlpha (0.35f));
        keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff303030));
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, colours::background);
        addAndMakeVisible (keyboard);

        // Pitch wheel: springs back to centre. Mod wheel: CC1 (not routed yet).
        pitch.setRange (-1.0, 1.0);
        pitch.setValue (0.0, juce::dontSendNotification);
        pitch.getProperties().set ("bipolar", true);
        pitch.onValueChange = [this]
        {
            processor.setUiPitchWheel (juce::jlimit (0, 16383, 8192 + juce::roundToInt (pitch.getValue() * 8191.0)));
        };
        pitch.onDragEnd = [this] { pitch.setValue (0.0, juce::sendNotificationSync); };
        mod.setRange (0.0, 1.0);
        mod.onValueChange = [this] { processor.setUiModWheel (juce::roundToInt (mod.getValue() * 127.0)); };
        addAndMakeVisible (pitch);
        addAndMakeVisible (mod);

        down->onClick = [this] { shiftOctave (-1); };
        up->onClick   = [this] { shiftOctave (+1); };
        overview->onJump = [this] (int note) { setLowestKey (note); };
        addAndMakeVisible (*down);
        addAndMakeVisible (*up);
        addAndMakeVisible (*overview);
    }

    KeyboardSection::~KeyboardSection() = default;

    void KeyboardSection::shiftOctave (int delta)
    {
        setLowestKey (lowestKey + 12 * delta);
    }

    void KeyboardSection::setLowestKey (int note)
    {
        lowestKey = juce::jlimit (0, 72, (note / 12) * 12); // keep whole octaves, C0..C6
        // Without scroll buttons JUCE pins the view to the range start, so the
        // octave arrows move the available range (4 octaves + top C).
        keyboard.setAvailableRange (lowestKey, juce::jmin (127, lowestKey + 48));
        overview->repaint();
    }

    void KeyboardSection::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRect (r);

        auto bar = r.removeFromTop ((float) barHeight);
        auto lbl = bar.removeFromLeft ((float) wheelColumnWidth);
        ForgeLookAndFeel::drawCell (g, lbl);
        g.setColour (colours::label);
        g.setFont (ForgeLookAndFeel::uiFont (11.5f, true));
        g.drawText ("Pitch/Mod.", lbl, juce::Justification::centred);

        auto wheels = r.removeFromLeft ((float) wheelColumnWidth);
        g.setColour (colours::cell);
        g.fillRect (wheels);
        g.setColour (colours::outlineDim);
        g.drawRect (wheels, 1.0f);
    }

    void KeyboardSection::resized()
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight);
        bar.removeFromLeft (wheelColumnWidth + 4);
        down->setBounds (bar.removeFromLeft (40));
        up->setBounds (bar.removeFromRight (40));
        overview->setBounds (bar.reduced (6, 2));

        auto wheels = r.removeFromLeft (wheelColumnWidth).reduced (5, 6);
        pitch.setBounds (wheels.removeFromLeft (wheels.getWidth() / 2).reduced (3, 0));
        mod.setBounds (wheels.reduced (3, 0));

        r.removeFromLeft (2);
        keyboard.setBounds (r);
        keyboard.setKeyWidth ((float) r.getWidth() / (float) visibleWhiteKeys);
        setLowestKey (lowestKey);
    }
}
