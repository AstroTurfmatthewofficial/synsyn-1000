#include "Components.h"
#include "dsp/Oscillator.h"
#include "dsp/Filter.h"
#include "dsp/Lfo.h"

namespace vf::gui
{
    //==========================================================================
    SectionPanel::SectionPanel (juce::String t) : title (std::move (t)) {}

    juce::Rectangle<int> SectionPanel::getBodyBounds() const
    {
        return getLocalBounds().withTrimmedTop (headerHeight + 2).reduced (3, 3);
    }

    void SectionPanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto header = r.removeFromTop ((float) headerHeight);

        g.setColour (colours::header);
        g.fillRect (header);
        g.setColour (colours::outline);
        g.drawRect (header, 1.0f);
        g.setColour (colours::headerText);
        g.setFont (ForgeLookAndFeel::uiFont (11.5f, true));
        g.drawText (title, header, juce::Justification::centred);

        r.removeFromTop (2.0f);
        g.setColour (colours::background);
        g.fillRect (r);
        g.setColour (colours::outlineDim);
        g.drawRect (r, 1.0f);
    }

    void SectionPanel::layoutRows (juce::Rectangle<int> area, std::initializer_list<juce::Component*> rows)
    {
        const int n = (int) rows.size();
        if (n == 0) return;
        const int h = area.getHeight() / n;
        for (auto* c : rows)
            c->setBounds (area.removeFromTop (h));
    }

    //==========================================================================
    void Strip::addSection (std::unique_ptr<SectionPanel> section, float relativeWidth)
    {
        addAndMakeVisible (*section);
        items.push_back ({ std::move (section), relativeWidth });
        resized();
    }

    void Strip::resized()
    {
        float total = 0.0f;
        for (auto& i : items) total += i.weight;
        if (total <= 0.0f) return;

        constexpr int gap = 3;
        auto r = getLocalBounds();
        const int avail = r.getWidth() - gap * ((int) items.size() - 1);
        int x = r.getX();
        for (size_t n = 0; n < items.size(); ++n)
        {
            const int w = n + 1 == items.size() ? r.getRight() - x
                                                : juce::roundToInt ((float) avail * items[n].weight / total);
            items[n].section->setBounds (x, r.getY(), w, r.getHeight());
            x += w + gap;
        }
    }

    //==========================================================================
    LcdParam::LcdParam (juce::RangedAudioParameter& p, juce::String l, float frac)
        : param (p), label (std::move (l)), lcdFraction (frac),
          choice (dynamic_cast<juce::AudioParameterChoice*> (&p)),
          toggle (dynamic_cast<juce::AudioParameterBool*> (&p)),
          attachment (p, [this] (float v) { value = v; repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
        setRepaintsOnMouseActivity (false);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void LcdParam::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool hot = dragging || isMouseOver (true);
        ForgeLookAndFeel::drawCell (g, r, hot);

        auto inner = r.reduced (3.0f);
        const bool hasLabel = label.isNotEmpty();
        auto lcd = hasLabel ? inner.removeFromLeft (inner.getWidth() * lcdFraction) : inner;

        auto text = param.getText (normalised(), 16);
        if (choice != nullptr || toggle != nullptr) text = text.toUpperCase();
        const bool lit = dragging || (toggle != nullptr && value >= 0.5f);
        ForgeLookAndFeel::drawLcd (g, lcd, text, juce::Justification::centredLeft, lit);

        if (hasLabel)
        {
            g.setColour (hot ? colours::accent : colours::label);
            g.setFont (ForgeLookAndFeel::uiFont (12.0f, true));
            g.drawFittedText (label, inner.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    void LcdParam::mouseDown (const juce::MouseEvent&)
    {
        dragging = true;
        moved = false;
        dragStartNorm = normalised();
        attachment.beginGesture();
        repaint();
    }

    void LcdParam::mouseDrag (const juce::MouseEvent& e)
    {
        const int dy = e.getDistanceFromDragStartY();
        if (! moved && std::abs (dy) < 3) return;
        moved = true;

        const float sensitivity = e.mods.isShiftDown() ? 0.0008f : 0.005f;
        const float norm = juce::jlimit (0.0f, 1.0f, dragStartNorm - (float) dy * sensitivity);
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
    }

    void LcdParam::mouseUp (const juce::MouseEvent&)
    {
        attachment.endGesture();
        dragging = false;
        repaint();
        if (! moved && choice != nullptr)
            showChoiceMenu();
        else if (! moved && toggle != nullptr)
            attachment.setValueAsCompleteGesture (value >= 0.5f ? 0.0f : 1.0f);
    }

    void LcdParam::mouseDoubleClick (const juce::MouseEvent&)
    {
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }

    void LcdParam::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        const float dir = (std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX) > 0.0f ? 1.0f : -1.0f;
        if (choice != nullptr)
        {
            const int idx = juce::jlimit (0, choice->choices.size() - 1, juce::roundToInt (value) + (int) dir);
            attachment.setValueAsCompleteGesture ((float) idx);
        }
        else
        {
            const float norm = juce::jlimit (0.0f, 1.0f, normalised() + dir * 0.01f);
            attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm));
        }
    }

    void LcdParam::showChoiceMenu()
    {
        juce::PopupMenu menu;
        const int current = juce::roundToInt (value);
        for (int i = 0; i < choice->choices.size(); ++i)
            menu.addItem (i + 1, choice->choices[i].toUpperCase(), true, i == current);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<LcdParam> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                            });
    }

    //==========================================================================
    ParamFader::ParamFader (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
        : attachment (state, paramId, slider)
    {
        slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (
                                                      state.getParameter (paramId)->getDefaultValue()));
        addAndMakeVisible (slider);
    }

    //==========================================================================
    ParamDisplay::ParamDisplay (std::vector<std::atomic<float>*> s)
        : sources (std::move (s)), lastValues (sources.size(), 0.0f)
    {
        for (size_t i = 0; i < sources.size(); ++i)
            lastValues[i] = sources[i]->load();
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void ParamDisplay::timerCallback()
    {
        bool changed = false;
        for (size_t i = 0; i < sources.size(); ++i)
        {
            const float v = sources[i]->load();
            if (std::abs (v - lastValues[i]) > 1.0e-6f) { lastValues[i] = v; changed = true; }
        }
        if (changed) repaint();
    }

    void ParamDisplay::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::display);
        g.fillRect (r);
        g.setColour (colours::outline);
        g.drawRect (r, 1.0f);

        // faint grid
        auto inner = r.reduced (6.0f);
        g.setColour (colours::outlineDim.withAlpha (0.5f));
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());

        drawContent (g, inner);
    }

    //==========================================================================
    void EnvelopeDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // Segment widths use a sqrt time scale so short and long times are both readable.
        const float a = std::sqrt (get (0)), d = std::sqrt (get (1)), s = get (2), rel = std::sqrt (get (3));
        const float hold = 0.6f;
        const float total = a + d + hold + rel;
        const float k = r.getWidth() / juce::jmax (total, 1.0e-3f);

        const float x0 = r.getX(), yb = r.getBottom(), yt = r.getY();
        const float ys = yb - s * (yb - yt);
        const juce::Point<float> p0 { x0, yb },
                                 p1 { x0 + a * k, yt },
                                 p2 { p1.x + d * k, ys },
                                 p3 { p2.x + hold * k, ys },
                                 p4 { p3.x + rel * k, yb };

        juce::Path fill;
        fill.startNewSubPath (p0);
        for (auto p : { p1, p2, p3, p4 }) fill.lineTo (p);
        fill.closeSubPath();
        g.setColour (colours::trace.withAlpha (0.15f));
        g.fillPath (fill);

        juce::Path line;
        line.startNewSubPath (p0);
        for (auto p : { p1, p2, p3, p4 }) line.lineTo (p);
        g.setColour (colours::trace);
        g.strokePath (line, juce::PathStrokeType (1.6f));

        g.setColour (colours::accent);
        for (auto p : { p1, p2, p3 })
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));

        g.setFont (ForgeLookAndFeel::uiFont (10.0f, true));
        g.setColour (colours::headerText.withAlpha (0.7f));
        auto lbl = [&] (const char* t, float xa, float xb)
        { g.drawText (t, juce::Rectangle<float> (xa, yb - 12.0f, juce::jmax (8.0f, xb - xa), 12.0f), juce::Justification::centred); };
        lbl ("A", p0.x, p1.x); lbl ("D", p1.x, p2.x); lbl ("S", p2.x, p3.x); lbl ("R", p3.x, p4.x);
    }

    //==========================================================================
    void FilterResponseDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> r)
    {
        using vf::dsp::FilterType;
        const auto type = (FilterType) juce::jlimit (0, 2, juce::roundToInt (get (0)));
        const double cutoff = juce::jlimit (20.0, 20000.0, (double) get (1));
        const double res = get (2);
        constexpr double fMin = 20.0, fMax = 20000.0, dbTop = 24.0, dbBottom = -36.0;

        auto yForDb = [&] (double db) { return r.getY() + (float) ((dbTop - db) / (dbTop - dbBottom)) * r.getHeight(); };
        auto xForF  = [&] (double f)  { return r.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * r.getWidth(); };

        // Grid: 0 dB line, decades.
        g.setColour (colours::outlineDim.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yForDb (0.0), r.getX(), r.getRight());
        for (double f : { 100.0, 1000.0, 10000.0 })
            g.drawVerticalLine ((int) xForF (f), r.getY(), r.getBottom());

        juce::Path curve, fill;
        const int n = juce::jmax (2, (int) r.getWidth());
        for (int i = 0; i < n; ++i)
        {
            const double f = fMin * std::pow (fMax / fMin, (double) i / (double) (n - 1));
            const double mag = vf::dsp::SvfFilter::magnitude (type, f, cutoff, res);
            const double db = juce::jlimit (dbBottom - 6.0, dbTop, 20.0 * std::log10 (juce::jmax (mag, 1.0e-6)));
            const float x = r.getX() + (float) i, y = juce::jmin (r.getBottom(), yForDb (db));
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); }
            else curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();
        g.setColour (colours::trace.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (colours::trace);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        // Cutoff marker
        g.setColour (colours::accent);
        const float cx = xForF (cutoff);
        g.drawVerticalLine ((int) cx, r.getY(), r.getBottom());
        g.setFont (ForgeLookAndFeel::uiFont (10.0f, true));
        g.setColour (colours::headerText.withAlpha (0.7f));
        g.drawText ("100", juce::Rectangle<float> (xForF (100.0) + 2.0f, r.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
        g.drawText ("1k",  juce::Rectangle<float> (xForF (1000.0) + 2.0f, r.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
        g.drawText ("10k", juce::Rectangle<float> (xForF (10000.0) + 2.0f, r.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    }

    //==========================================================================
    void FxDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const bool active = on != nullptr && on->load() >= 0.5f;
        const auto c = active ? colours::accent : colours::accent.withAlpha (0.3f);
        const auto t = active ? colours::trace : colours::trace.withAlpha (0.3f);

        if (kind == Kind::chorus)
        {
            const float depth = juce::jlimit (0.0f, 1.0f, get (1));
            const float cycles = 1.0f + 2.0f * std::sqrt (juce::jlimit (0.0f, 1.0f, get (0) / 5.0f));
            for (int v = 0; v < 2; ++v)
            {
                juce::Path p;
                for (int i = 0; i < (int) r.getWidth(); ++i)
                {
                    const float x = (float) i / r.getWidth();
                    const float y = r.getCentreY() - (0.15f + 0.3f * depth) * r.getHeight()
                                    * std::sin (juce::MathConstants<float>::twoPi * (cycles * x + (float) v * 0.25f));
                    if (i == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + (float) i, y);
                }
                g.setColour (v == 0 ? c : t);
                g.strokePath (p, juce::PathStrokeType (1.4f));
            }
        }
        else if (kind == Kind::delay)
        {
            const float timeMs = juce::jmax (1.0f, get (0)), fb = get (1);
            const bool pingPong = get (2) >= 0.5f;
            const float span = 2000.0f;   // ms shown across the display
            float level = 1.0f;
            g.setColour (c);
            g.fillRect (r.getX(), r.getY(), 3.0f, r.getHeight());   // dry hit
            for (int k = 1; k < 32; ++k)
            {
                const float x = r.getX() + (float) k * timeMs / span * r.getWidth();
                if (x > r.getRight() || level < 0.02f) break;
                level = k == 1 ? 1.0f : level * fb;
                const float h = level * r.getHeight() * (pingPong ? 0.5f : 1.0f);
                g.setColour (pingPong && (k % 2 == 0) ? t : c);
                if (pingPong)
                    g.fillRect (x, (k % 2 == 1) ? r.getCentreY() - h : r.getCentreY(), 3.0f, h);
                else
                    g.fillRect (x, r.getBottom() - h, 3.0f, h);
            }
        }
        else
        {
            const float size = get (0), damp = get (1), pre = get (2);
            const float span = 4000.0f; // ms
            const float decayMs = 300.0f + 3500.0f * size * size;
            const float x0 = r.getX() + pre / span * r.getWidth();
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getBottom());
            p.lineTo (x0, r.getBottom());
            for (float x = x0; x <= r.getRight(); x += 2.0f)
            {
                const float ms = (x - x0) / r.getWidth() * span;
                const float env = std::exp (-ms / (decayMs * (1.0f - 0.4f * damp)) * 3.0f);
                p.lineTo (x, r.getBottom() - env * r.getHeight() * 0.95f);
            }
            p.lineTo (r.getRight(), r.getBottom());
            p.closeSubPath();
            g.setColour (t.withMultipliedAlpha (0.25f));
            g.fillPath (p);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }
    }

    //==========================================================================
    LfoDisplay::LfoDisplay (std::vector<std::atomic<float>*> s, std::function<float()> g)
        : sources (std::move (s)), getPhase (std::move (g))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void LfoDisplay::paint (juce::Graphics& g)
    {
        using namespace vf::dsp;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::display);
        g.fillRect (r);
        g.setColour (colours::outline);
        g.drawRect (r, 1.0f);
        auto inner = r.reduced (5.0f);
        g.setColour (colours::outlineDim.withAlpha (0.5f));
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());

        const auto shape = (LfoShape) juce::jlimit (0, (int) LfoShape::numShapes - 1, juce::roundToInt (sources[0]->load()));
        const bool active = juce::roundToInt (sources[1]->load()) != 0 && std::abs (sources[2]->load()) > 0.0f;
        const auto colour = active ? colours::accent : colours::accent.withAlpha (0.3f);

        auto yFor = [&] (float v) { return inner.getCentreY() - v * inner.getHeight() * 0.45f; };
        juce::Path p;
        const int n = juce::jmax (2, (int) inner.getWidth());
        for (int i = 0; i < n; ++i)
        {
            const double pos = (double) i / (double) (n - 1) * 0.9999;
            const float y = yFor (Lfo::evaluate (shape, shape == LfoShape::sampleHold ? pos * 6.0 : pos, 7u));
            const float x = inner.getX() + (float) i;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        // Playhead
        const float ph = getPhase ? getPhase() : 0.0f;
        const double dotPos = shape == LfoShape::sampleHold ? (double) ph * 6.0 * 0.9999 : (double) ph;
        const float x = inner.getX() + ph * inner.getWidth();
        const float y = yFor (Lfo::evaluate (shape, dotPos, 7u));
        g.setColour (active ? colours::trace : colours::trace.withAlpha (0.3f));
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, y }));
    }

    //==========================================================================
    void WaveformDisplay::drawContent (juce::Graphics& g, juce::Rectangle<float> r)
    {
        using vf::dsp::Waveform;
        const auto w = (Waveform) juce::jlimit (0, (int) Waveform::numWaveforms - 1, juce::roundToInt (get (0)));
        const int n = juce::jmax (2, (int) r.getWidth());
        const float cycles = 2.0f;
        juce::Random rng (1234);

        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float t = std::fmod ((float) i / (float) (n - 1) * cycles, 1.0f);
            float v = 0.0f;
            switch (w)
            {
                case Waveform::sine:     v = std::sin (juce::MathConstants<float>::twoPi * t); break;
                case Waveform::saw:      v = 2.0f * t - 1.0f; break;
                case Waveform::square:   v = t < 0.5f ? 1.0f : -1.0f; break;
                case Waveform::triangle: v = 4.0f * std::abs (t - 0.5f) - 1.0f; break;
                case Waveform::noise:    v = rng.nextFloat() * 2.0f - 1.0f; break;
                case Waveform::numWaveforms: default: break;
            }
            const float x = r.getX() + (float) i;
            const float y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const bool on = numSources() < 2 || get (1) >= 0.5f;
        g.setColour (on ? colours::accent : colours::accent.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    //==========================================================================
    MidiLed::MidiLed (std::function<bool()> isConnected, std::function<uint32_t()> activityCount)
        : connected (std::move (isConnected)), activity (std::move (activityCount))
    {
        setInterceptsMouseClicks (false, false);
        lastCount = activity ? activity() : 0u;
        poll();
        startTimerHz (30);
    }

    void MidiLed::poll()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const uint32_t c = activity ? activity() : 0u;
        if (c != lastCount)
        {
            lastCount = c;
            flashUntilMs = now + flashMs;
        }
        const bool on = connected && connected();
        const State s = now < flashUntilMs ? State::flash : (on ? State::on : State::off);
        if (s != state)
        {
            state = s;
            repaint();
        }
    }

    void MidiLed::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const float d = juce::jmin (r.getHeight() * 0.55f, 12.0f);
        auto lamp = r.removeFromRight (d + 6.0f).withSizeKeepingCentre (d, d);

        g.setColour (state == State::off ? colours::headerText.withAlpha (0.45f) : colours::headerText);
        g.setFont (ForgeLookAndFeel::uiFont (11.0f, true));
        g.drawText ("MIDI", r.withTrimmedRight (4.0f), juce::Justification::centredRight);

        const juce::Colour body = state == State::flash ? juce::Colour (0xffff6a55)
                                : state == State::on    ? juce::Colour (0xffd81e1e)
                                                        : juce::Colour (0xff3a0c0c);
        if (state != State::off)
        {
            // glow
            const float glow = state == State::flash ? d * 0.75f : d * 0.45f;
            g.setGradientFill (juce::ColourGradient (body.withAlpha (state == State::flash ? 0.55f : 0.35f), lamp.getCentre(),
                                                     body.withAlpha (0.0f), lamp.getCentre().translated (d * 0.5f + glow, 0.0f), true));
            g.fillEllipse (lamp.expanded (glow));
        }
        g.setColour (body);
        g.fillEllipse (lamp);
        // specular highlight
        g.setColour (juce::Colours::white.withAlpha (state == State::off ? 0.08f : (state == State::flash ? 0.6f : 0.35f)));
        g.fillEllipse (lamp.withSizeKeepingCentre (d * 0.38f, d * 0.38f).translated (-d * 0.16f, -d * 0.16f));
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawEllipse (lamp, 1.0f);
    }
}
