#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ForgeLookAndFeel.h"
#include "Components.h"
#include "KeyboardSection.h"
#include "PresetBrowser.h"
#include "Parameters.h"

// Editor layout (SynprezFM-inspired): title bar, N horizontal strips of titled
// sections, keyboard section at the bottom. Everything is laid out at a fixed
// design size and uniformly scaled, so the window can be resized freely while
// keeping proportions. Add a strip -> design height grows automatically.
class VSTForgeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VSTForgeAudioProcessorEditor (VSTForgeAudioProcessor&);
    ~VSTForgeAudioProcessorEditor() override;

    void resized() override;

    // Preset browser overlay (also used by the tests for screenshots).
    void showPresetBrowser (bool show)      { panel.presetUi.showBrowser (show); }
    vf::gui::PresetUi& getPresetUi()        { return panel.presetUi; }
    vf::gui::MidiLed& getMidiLed()          { return panel.midiLed; }

    static constexpr int designWidth = 1200;
    static constexpr int titleHeight = 26;
    static constexpr int stripHeight = 138;
    static constexpr int keyboardHeight = 150;
    static constexpr int margin = 6;
    static constexpr int gap = 4;

    // About box (0.6.1): in-editor overlay with the licence notice, source location,
    // trademark line and the Steinberg "VST Compatible" logo. An overlay rather than a
    // separate top-level window, so it behaves the same in every host.
    class AboutPanel final : public juce::Component
    {
    public:
        AboutPanel();
        void paint (juce::Graphics&) override;
        void resized() override;
        bool mouseClickOutsideCloses = true;
        void mouseDown (const juce::MouseEvent&) override;
        bool hasLogo() const noexcept { return logo != nullptr; }
    private:
        juce::Rectangle<int> boxArea() const;
        std::unique_ptr<juce::Drawable> logo;
        juce::TextButton okButton { "OK" };
    };

    bool aboutHasLogo() const noexcept   { return panel.about.hasLogo(); }
    bool isAboutVisible() const noexcept { return panel.about.isVisible(); }
    void showAbout (bool shouldShow)     { panel.about.setVisible (shouldShow); if (shouldShow) panel.about.toFront (false); }

private:
    class MainPanel final : public juce::Component
    {
    public:
        explicit MainPanel (VSTForgeAudioProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        int getDesignHeight() const;

        vf::gui::PresetUi presetUi;   // overlays (browser, dialogs) are added to this panel
        vf::gui::MidiLed midiLed;     // title bar MIDI indicator (M6)
        juce::TextButton aboutButton { "About" };   // licence / source notice (0.6.1, AGPLv3)
        AboutPanel about;

    private:
        std::vector<std::unique_ptr<vf::gui::Strip>> strips;
        vf::gui::KeyboardSection keyboard;
    };

    VSTForgeAudioProcessor& processor;
    vf::gui::ForgeLookAndFeel lnf;
    MainPanel panel;
    bool restoredSize = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTForgeAudioProcessorEditor)
};
