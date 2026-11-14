#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Oscillator.h"
#include "dsp/Lfo.h"

// Central place for parameter IDs and the parameter layout.
// IDs are stable: hosts and saved sessions refer to them. Never rename.
namespace vf::params
{
    constexpr int numOscillators = 3;
    constexpr int numLfos = 2;

    namespace id
    {
        // M1
        inline constexpr const char* waveform = "osc1_wave";
        inline constexpr const char* attack   = "amp_attack";
        inline constexpr const char* decay    = "amp_decay";
        inline constexpr const char* sustain  = "amp_sustain";
        inline constexpr const char* release  = "amp_release";
        inline constexpr const char* gain     = "master_gain";

        // M2: filter + filter envelope
        inline constexpr const char* filterType     = "filter_type";
        inline constexpr const char* filterCutoff   = "filter_cutoff";
        inline constexpr const char* filterRes      = "filter_res";
        inline constexpr const char* filterKeytrack = "filter_keytrack";
        inline constexpr const char* filterEnvAmt   = "filter_envamt";
        inline constexpr const char* fenvAttack     = "fenv_attack";
        inline constexpr const char* fenvDecay      = "fenv_decay";
        inline constexpr const char* fenvSustain    = "fenv_sustain";
        inline constexpr const char* fenvRelease    = "fenv_release";

        // M3
        inline constexpr const char* lfo1ModWheel   = "lfo1_modwheel";

        // M4: effects
        inline constexpr const char* chorusOn       = "chorus_on";
        inline constexpr const char* chorusRate     = "chorus_rate";
        inline constexpr const char* chorusDepth    = "chorus_depth";
        inline constexpr const char* chorusMix      = "chorus_mix";
        inline constexpr const char* delayOn        = "delay_on";
        inline constexpr const char* delayTime      = "delay_time";
        inline constexpr const char* delaySync      = "delay_sync";
        inline constexpr const char* delayDiv       = "delay_div";
        inline constexpr const char* delayFeedback  = "delay_feedback";
        inline constexpr const char* delayPingPong  = "delay_pingpong";
        inline constexpr const char* delayHighCut   = "delay_highcut";
        inline constexpr const char* delayMix       = "delay_mix";
        inline constexpr const char* reverbOn       = "reverb_on";
        inline constexpr const char* reverbSize     = "reverb_size";
        inline constexpr const char* reverbDamping  = "reverb_damping";
        inline constexpr const char* reverbWidth    = "reverb_width";
        inline constexpr const char* reverbPreDelay = "reverb_predelay";
        inline constexpr const char* reverbMix      = "reverb_mix";
    }

    // Per-oscillator IDs: "osc<N>_wave|on|level|coarse|fine" (N = 1..3).
    juce::String oscId (int oscIndex, const char* suffix);

    // Per-LFO IDs: "lfo<N>_wave|rate|sync|div|depth|mode|delay|phase|target|amount" (N = 1..2).
    juce::String lfoId (int lfoIndex, const char* suffix);

    constexpr int parameterVersion = 1;   // M1 parameters
    constexpr int parameterVersionM2 = 2; // parameters added in M2
    constexpr int parameterVersionM3 = 3; // parameters added in M3
    constexpr int parameterVersionM4 = 4; // parameters added in M4

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::StringArray filterTypeNames();

    // Cached raw-value pointers, read lock-free on the audio thread.
    struct Handles
    {
        struct Osc { std::atomic<float>* wave = nullptr, *on = nullptr, *level = nullptr, *coarse = nullptr, *fine = nullptr; };
        std::array<Osc, numOscillators> osc;

        std::atomic<float>* attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr;
        std::atomic<float>* gain = nullptr;

        std::atomic<float>* filterType = nullptr, *filterCutoff = nullptr, *filterRes = nullptr,
                          * filterKeytrack = nullptr, *filterEnvAmt = nullptr;
        std::atomic<float>* fenvAttack = nullptr, *fenvDecay = nullptr, *fenvSustain = nullptr, *fenvRelease = nullptr;

        struct LfoH { std::atomic<float>* wave = nullptr, *rate = nullptr, *sync = nullptr, *div = nullptr, *depth = nullptr,
                                       * mode = nullptr, *delay = nullptr, *phase = nullptr, *target = nullptr, *amount = nullptr; };
        std::array<LfoH, numLfos> lfo;
        std::atomic<float>* lfo1ModWheel = nullptr;

        std::atomic<float>* chorusOn = nullptr, *chorusRate = nullptr, *chorusDepth = nullptr, *chorusMix = nullptr;
        std::atomic<float>* delayOn = nullptr, *delayTime = nullptr, *delaySync = nullptr, *delayDiv = nullptr,
                          * delayFeedback = nullptr, *delayPingPong = nullptr, *delayHighCut = nullptr, *delayMix = nullptr;
        std::atomic<float>* reverbOn = nullptr, *reverbSize = nullptr, *reverbDamping = nullptr, *reverbWidth = nullptr,
                          * reverbPreDelay = nullptr, *reverbMix = nullptr;

        void bind (juce::AudioProcessorValueTreeState& state);
    };
}
