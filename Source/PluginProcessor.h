#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Parameters.h"
#include "dsp/SynthVoice.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Effects.h"
#include "presets/PresetManager.h"

class VSTForgeAudioProcessor final : public juce::AudioProcessor
{
public:
    // APVTS / session-state root tag. Pre-0.6.1 builds (VST Forge Synth) wrote "VSTForgeSynth".
    static constexpr const char* stateTag = "SynSyn1000";
    static constexpr const char* legacyStateTag = "VSTForgeSynth";

    static constexpr int numVoices = 16;

    VSTForgeAudioProcessor();
    ~VSTForgeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SynSyn 1000"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Host programs = Init + the factory bank (in-memory, no file I/O).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept       { return state; }
    juce::MidiKeyboardState& getKeyboardState() noexcept          { return keyboardState; }
    vf::presets::PresetManager& getPresetManager() noexcept       { return presets; }

    // Number of preset-change fades performed by the audio thread (tests/diagnostics).
    int getPresetFadeCount() const noexcept        { return presetFadeCount.load(); }
    int getNumActiveVoices() const noexcept
    {
        int n = 0;
        for (auto* v : voices) n += v->isVoiceActive() ? 1 : 0;
        return n;
    }

    // On-screen wheels (message thread) -> injected as MIDI on the audio thread.
    void setUiPitchWheel (int value14bit) noexcept { uiPitchWheel.store (value14bit); }
    void setUiModWheel (int value7bit) noexcept    { uiModWheel.store (value7bit); }

    // Editor size persisted with the plugin state (0 = default).
    int getSavedEditorWidth() const noexcept       { return editorWidth.load(); }
    void setSavedEditorWidth (int w) noexcept      { editorWidth.store (w); }

    float getLimiterGain() const noexcept          { return limiterGain.load(); }

    // LFO phase (0..1) for the editor's live displays.
    float getLfoDisplayPhase (int lfo) const noexcept { return lfoDisplayPhase[(size_t) lfo].load(); }
    // Effective tempo used for LFO sync (host BPM, or 120 when there is no host tempo).
    double getSyncBpm() const noexcept              { return syncBpm.load(); }
    static constexpr double fallbackBpm = 120.0;

    //==========================================================================
    // MIDI input status (M6), shown by the editor's MIDI LED.
    //
    // "External" MIDI = whatever arrives in processBlock's MidiBuffer from the
    // wrapper (host MIDI in a plugin, opened MIDI devices in the Standalone),
    // counted *before* the on-screen keyboard/wheels are merged in. The audio
    // thread only does relaxed atomic increments: no locks, no allocation.
    //
    // Channel-voice messages (notes, CC, pitch bend, aftertouch, program
    // change) bump the activity counter (LED flash). Any message, including
    // clock/sysex/active sensing, marks external MIDI as seen.
    uint32_t getMidiActivityCount() const noexcept   { return midiActivityCount.load (std::memory_order_relaxed); }
    bool hasReceivedExternalMidi() const noexcept    { return externalMidiSeen.load (std::memory_order_relaxed); }

    // Standalone only (message thread): number of enabled + open MIDI input
    // devices and their names. -1 = not running as the Standalone.
    void setStandaloneMidiInputs (int numOpen, const juce::String& names)
    {
        standaloneMidiNames = names;
        standaloneMidiInputs.store (numOpen);
    }
    int getStandaloneMidiInputs() const noexcept     { return standaloneMidiInputs.load(); }
    const juce::String& getStandaloneMidiNames() const noexcept { return standaloneMidiNames; }

    // LED "connected" definition:
    //  - Standalone: at least one MIDI input device is enabled and open.
    //  - Plugin (VST3/LV2) or before the Standalone helper has attached: the
    //    host has delivered at least one MIDI message to this instance
    //    (latched; hosts don't report device connections to plugins).
    bool isMidiInputConnected() const noexcept
    {
        const int n = standaloneMidiInputs.load();
        return n >= 0 ? n > 0 : hasReceivedExternalMidi();
    }

    // Owned helper objects (e.g. the Standalone MIDI auto-connect, created by
    // the plugin entry point only). Destroyed first in the destructor.
    struct Extension { virtual ~Extension() = default; };
    void setStandaloneMidiHelper (std::unique_ptr<Extension> e) { standaloneMidiHelper = std::move (e); }

private:
    vf::dsp::VoiceParams readVoiceParams() const noexcept;
    void updateLfos (vf::dsp::VoiceParams&, int numSamples) noexcept;
    vf::dsp::FxParams readFxParams() const noexcept;
    void handlePresetChange (juce::MidiBuffer& midi, int numSamples) noexcept;
    void applyPresetFade (juce::AudioBuffer<float>&) noexcept;
    void silenceEverything() noexcept;

    juce::AudioProcessorValueTreeState state;
    vf::params::Handles params;
    vf::presets::PresetManager presets;
    int lastProgram = 0;

    // Preset-change handling (audio thread): a preset load bumps the counter;
    // if anything is sounding the output fades out over ~10 ms, then all voices
    // and effect tails are cleared. Note on/off events arriving during the fade
    // are deferred and replayed afterwards (no lost or stuck notes).
    std::atomic<uint32_t> presetChangeCounter { 0 };
    uint32_t seenPresetChange = 0;
    int fadeTotal = 0, fadeLeft = 0;
    float lastOutputPeak = 0.0f;
    juce::MidiBuffer deferredMidi, scratchMidi;
    // Parameter snapshots of the previous block: while fading out, the old
    // voices keep sounding with the old preset's parameters.
    vf::dsp::VoiceParams lastVoiceParams;
    vf::dsp::FxParams lastFxParams;
    std::atomic<int> presetFadeCount { 0 };
    juce::MidiKeyboardState keyboardState;
    juce::Synthesiser synth;
    std::array<vf::dsp::SynthVoice*, numVoices> voices {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain;
    vf::dsp::FxChain effects;
    vf::dsp::SafetyLimiter limiter;
    std::atomic<float> limiterGain { 1.0f };
    std::atomic<int> editorWidth { 0 };
    std::array<double, vf::dsp::numLfos> lfoGlobalPosition {};
    std::array<std::atomic<float>, vf::dsp::numLfos> lfoDisplayPhase {};
    std::atomic<double> syncBpm { fallbackBpm };
    float modWheel = 0.0f;     // CC1, 0..1 (host MIDI or on-screen wheel)
    double currentSampleRate = 44100.0;
    std::atomic<int> uiPitchWheel { 8192 }, uiModWheel { 0 };
    int lastUiPitchWheel = 8192, lastUiModWheel = 0;
    std::atomic<uint32_t> midiActivityCount { 0 };
    std::atomic<bool> externalMidiSeen { false };
    std::atomic<int> standaloneMidiInputs { -1 };
    juce::String standaloneMidiNames;              // message thread only
    std::unique_ptr<Extension> standaloneMidiHelper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTForgeAudioProcessor)
};
