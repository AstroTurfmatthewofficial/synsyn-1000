#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Oscillator.h"
#include "Filter.h"
#include "Lfo.h"

namespace vf::dsp
{
    constexpr int numOscillators = 3;

    struct OscParams
    {
        bool on = true;
        Waveform waveform = Waveform::saw;
        float level = 1.0f;
        float coarseSemis = 0.0f;
        float fineCents = 0.0f;
    };

    struct FilterParams
    {
        FilterType type = FilterType::lowPass;
        float cutoffHz = 20000.0f;
        float resonance = 0.0f;   // 0..1
        float keytrack = 0.0f;    // 0..1 (1 = cutoff follows pitch 1:1)
        float envAmount = 0.0f;   // -1..1 (= +/- envOctaves)
    };

    struct LfoParams
    {
        LfoShape shape = LfoShape::sine;
        LfoTarget target = LfoTarget::none;
        double increment = 0.0;        // cycles per sample (rate or tempo-synced rate)
        float depth = 1.0f;            // 0..1 (already scaled by the mod wheel if enabled)
        float amount = 0.0f;           // -1..1
        float phaseOffset = 0.0f;      // cycles, 0..1
        float delaySeconds = 0.0f;     // linear fade-in time after note-on
        bool freeRunning = false;      // false = retrigger per voice on note-on
        double globalPosition = 0.0;   // free mode: position at block start (shared by all voices)
        uint32_t globalSeed = 0;       // free mode: sample & hold seed shared by all voices

        bool isActive() const noexcept { return target != LfoTarget::none && std::abs (amount) > 0.0f; }
    };

    // Per-block parameter snapshot, filled by the processor from the APVTS and
    // pushed to every voice. Extend this as modules are added (LFOs, mod matrix...).
    struct VoiceParams
    {
        std::array<OscParams, numOscillators> osc {};
        juce::ADSR::Parameters ampEnv { 0.005f, 0.25f, 0.7f, 0.3f };
        FilterParams filter;
        juce::ADSR::Parameters filterEnv { 0.005f, 0.4f, 0.3f, 0.4f };
        std::array<LfoParams, numLfos> lfo {};
    };

    struct SynthSound final : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class SynthVoice final : public juce::SynthesiserVoice
    {
    public:
        explicit SynthVoice (int voiceIndex);

        void prepare (double sampleRate, int maxBlockSize);
        void setParams (const VoiceParams& p) noexcept;

        bool canPlaySound (juce::SynthesiserSound* s) override;
        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int newValue) override;
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;
        using juce::SynthesiserVoice::renderNextBlock;

        // Per-voice headroom before the master gain (a 4-note chord of one
        // oscillator at full velocity peaks around -10 dBFS at default gain).
        static constexpr float voiceLevel = 0.2f;
        static constexpr float envOctaves = 7.0f;        // filter env amount range
        static constexpr int   controlInterval = 8;      // filter coefficient update period (samples)

        static constexpr float lfoSmoothingSeconds = 0.002f; // de-zipper LFO steps (S&H, square, automation)

    private:
        void updateFrequencies() noexcept;
        void applyPitchModulation (const float* oscSemis) noexcept;

        // Per-LFO per-voice state.
        struct LfoState
        {
            double position = 0.0;     // retrigger mode position (cycles since note-on)
            float smoothedValue = 0.0f; // smoothed signed modulation (value * depth * fade * amount)
            float smoothedAbs = 0.0f;   // smoothed |depth * fade * amount| (for gain targets)
            uint32_t seed = 0;
            bool wasActive = false;
        };
        std::array<LfoState, numLfos> lfoState {};
        std::array<LfoParams, numLfos> lfoParams {};
        float lfoSmoothingCoeff = 1.0f;
        int64_t samplesSinceNoteOn = 0;
        double sampleRateHz = 44100.0;
        float baseResonance = 0.0f;

        std::array<Oscillator, numOscillators> osc;
        std::array<juce::SmoothedValue<float>, numOscillators> oscLevel;
        std::array<float, numOscillators> oscPitchOffset {}; // semitones (coarse + fine)
        SvfFilter filter;
        juce::ADSR ampEnv, filterEnv;
        juce::SmoothedValue<float> velocityGain;
        juce::SmoothedValue<float> cutoffOctaves;    // log2(Hz), smoothed
        FilterParams filterParams;
        uint32_t phaseSeed;
        uint32_t lfoSeed;
        int currentNote = -1;
        int pitchWheelValue = 8192;
        double pitchBendRangeSemis = 2.0;
    };
}
