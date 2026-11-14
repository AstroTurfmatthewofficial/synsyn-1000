#include "PluginEditor.h"
#include "Sections.h"
#include "Legal.h"
#include "SynSynResources.h"

using namespace vf::gui;

//==============================================================================
VSTForgeAudioProcessorEditor::MainPanel::MainPanel (VSTForgeAudioProcessor& p)
    : presetUi (p.getPresetManager(), *this),
      midiLed ([&p] { return p.isMidiInputConnected(); }, [&p] { return p.getMidiActivityCount(); }),
      keyboard (p)
{
    auto& s = p.getState();
    p.getPresetManager().rescanUserPresets();   // message thread: pick up user presets (and resolve a restored one)

    namespace id = vf::params::id;

    // Strip 1: Volume | Memory (presets) | Oscillator 1 | Oscillator 2 | Oscillator 3
    auto osc = std::make_unique<Strip>();
    osc->addSection (std::make_unique<VolumeSection> (s), 0.75f);
    osc->addSection (std::make_unique<MemorySection> (p.getPresetManager(), presetUi), 2.95f);
    for (int i = 0; i < vf::params::numOscillators; ++i)
        osc->addSection (std::make_unique<OscillatorSection> (s, i), 2.6f);

    // Strip 2: Amp Envelope | Filter | Filter Envelope
    auto shape = std::make_unique<Strip>();
    shape->addSection (std::make_unique<EnvelopeSection> (s, "Amp Envelope", id::attack, id::decay, id::sustain, id::release), 1.0f);
    shape->addSection (std::make_unique<FilterSection> (s), 1.1f);
    shape->addSection (std::make_unique<EnvelopeSection> (s, "Filter Envelope", id::fenvAttack, id::fenvDecay, id::fenvSustain, id::fenvRelease), 1.0f);

    // Strip 3: LFO 1 | LFO 2
    auto mod = std::make_unique<Strip>();
    for (int i = 0; i < vf::params::numLfos; ++i)
        mod->addSection (std::make_unique<LfoSection> (s, i, [&p, i] { return p.getLfoDisplayPhase (i); }), 1.0f);

    // Strip 4: Chorus | Delay | Reverb
    auto fx = std::make_unique<Strip>();
    fx->addSection (std::make_unique<ChorusSection> (s), 1.0f);
    fx->addSection (std::make_unique<DelaySection> (s), 1.55f);
    fx->addSection (std::make_unique<ReverbSection> (s), 1.45f);

    for (auto* st : { osc.get(), shape.get(), mod.get(), fx.get() })
        addAndMakeVisible (st);
    strips.push_back (std::move (osc));
    strips.push_back (std::move (shape));
    strips.push_back (std::move (mod));
    strips.push_back (std::move (fx));

    addAndMakeVisible (keyboard);
    addAndMakeVisible (midiLed);

    aboutButton.setTooltip ("About SynSyn 1000 - licence (AGPLv3) and source code");
    aboutButton.onClick = [this] { about.setVisible (true); about.toFront (false); };
    addAndMakeVisible (aboutButton);
    addChildComponent (about);
}

int VSTForgeAudioProcessorEditor::MainPanel::getDesignHeight() const
{
    return margin + titleHeight + gap + (int) strips.size() * (stripHeight + gap) + keyboardHeight + margin;
}

void VSTForgeAudioProcessorEditor::MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto title = getLocalBounds().reduced (margin, 0).withTrimmedTop (margin).removeFromTop (titleHeight).toFloat();
    g.setColour (colours::header);
    g.fillRect (title);
    g.setColour (colours::outline);
    g.drawHorizontalLine ((int) title.getBottom() - 1, title.getX(), title.getRight());

    g.setColour (colours::accent);
    g.setFont (ForgeLookAndFeel::uiFont (16.0f, true));
    g.drawText ("SYNSYN 1000", title.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (colours::headerText.withAlpha (0.6f));
    g.setFont (ForgeLookAndFeel::uiFont (11.0f, false));
    g.drawText ("by DoubleDouble Instrumentations", title.reduced (8.0f, 0.0f).withTrimmedLeft (112.0f),
                juce::Justification::centredLeft);
    g.setColour (colours::headerText.withAlpha (0.6f));
    g.setFont (ForgeLookAndFeel::lcdFont (12.0f));
    g.drawText ("v" JUCE_STRINGIFY (VSTFORGE_VERSION_STRING), title.reduced (8.0f, 0.0f), juce::Justification::centredRight);
}

void VSTForgeAudioProcessorEditor::MainPanel::resized()
{
    auto r = getLocalBounds().reduced (margin);
    // Title bar: MIDI LED just left of the version text (right-aligned, ~56 px).
    midiLed.setBounds (r.getRight() - 8 - 56 - 58, r.getY(), 58, titleHeight - 1);
    aboutButton.setBounds (midiLed.getX() - 6 - 52, r.getY() + 3, 52, titleHeight - 7);
    about.setBounds (getLocalBounds());
    r.removeFromTop (titleHeight + gap);
    for (auto& s : strips)
    {
        s->setBounds (r.removeFromTop (stripHeight));
        r.removeFromTop (gap);
    }
    keyboard.setBounds (r);

    // Preset browser covers strips 2-4 (Memory strip and keyboard stay usable);
    // dialogs cover the whole panel.
    if (strips.size() >= 2)
        presetUi.setOverlayAreas (strips[1]->getBounds().getUnion (strips.back()->getBounds()), getLocalBounds());
}

//==============================================================================
VSTForgeAudioProcessorEditor::AboutPanel::AboutPanel()
{
    // Steinberg "VST Compatible" logo (negative version, for dark backgrounds), embedded unaltered.
    logo = juce::Drawable::createFromImageData (SynSynResources::VST_Compatible_Logo_Steinberg_negative_svg,
                                                (size_t) SynSynResources::VST_Compatible_Logo_Steinberg_negative_svgSize);
    okButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (okButton);
}

juce::Rectangle<int> VSTForgeAudioProcessorEditor::AboutPanel::boxArea() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), juce::jmin (470, getHeight() - 40));
}

void VSTForgeAudioProcessorEditor::AboutPanel::mouseDown (const juce::MouseEvent& e)
{
    if (mouseClickOutsideCloses && ! boxArea().contains (e.getPosition()))
        setVisible (false);
}

void VSTForgeAudioProcessorEditor::AboutPanel::resized()
{
    auto b = boxArea().reduced (18);
    okButton.setBounds (b.removeFromBottom (28).withSizeKeepingCentre (90, 28));
}

void VSTForgeAudioProcessorEditor::AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    const auto box = boxArea();
    g.setColour (colours::header);
    g.fillRect (box);
    g.setColour (colours::outline);
    g.drawRect (box, 1);

    auto b = box.reduced (18);
    b.removeFromBottom (36);
    auto top = b.removeFromTop (92);
    if (logo != nullptr)
        logo->drawWithin (g, top.removeFromRight (140).toFloat().reduced (2.0f), juce::RectanglePlacement::centred, 1.0f);
    g.setColour (colours::accent);
    g.setFont (ForgeLookAndFeel::uiFont (22.0f, true));
    g.drawText ("SYNSYN 1000", top.removeFromTop (34), juce::Justification::bottomLeft);
    g.setColour (colours::headerText);
    g.setFont (ForgeLookAndFeel::uiFont (14.0f, false));
    g.drawText ("by DoubleDouble Instrumentations  -  v" JUCE_STRINGIFY (VSTFORGE_VERSION_STRING), top.removeFromTop (24),
                juce::Justification::centredLeft);

    b.removeFromTop (8);
    g.setColour (colours::label);
    g.setFont (ForgeLookAndFeel::uiFont (13.5f, false));
    juce::AttributedString text;
    text.append (vf::legal::aboutText(), ForgeLookAndFeel::uiFont (13.5f, false), colours::label);
    text.setWordWrap (juce::AttributedString::byWord);
    text.draw (g, b.toFloat());
}

VSTForgeAudioProcessorEditor::VSTForgeAudioProcessorEditor (VSTForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);

    const int h = panel.getDesignHeight();
    panel.setBounds (0, 0, designWidth, h);

    setResizable (true, true);
    setResizeLimits (designWidth * 6 / 10, h * 6 / 10, designWidth * 5 / 2, h * 5 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / (double) h);

    // Restore the saved window size (persisted in the plugin state).
    const int saved = p.getSavedEditorWidth();
    int w = saved > 0 ? juce::jlimit (designWidth * 6 / 10, designWidth * 5 / 2, saved) : designWidth;
    if (saved <= 0)
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            // First open on a small screen: shrink so the whole window fits.
            const auto area = display->userBounds;
            const double fit = juce::jmin ((double) area.getWidth() * 0.95 / designWidth, (double) area.getHeight() * 0.9 / h);
            if (fit < 1.0)
                w = juce::jmax (designWidth * 6 / 10, (int) (designWidth * fit));
        }
    setSize (w, juce::roundToInt ((double) w * h / designWidth));
    restoredSize = true;
}

VSTForgeAudioProcessorEditor::~VSTForgeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void VSTForgeAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    if (restoredSize)
        processor.setSavedEditorWidth (getWidth());
}
