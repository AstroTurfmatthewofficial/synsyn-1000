#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace vf::dsp
{
    // Output safety stage: instant-attack peak limiter (gain never lets a sample
    // exceed the threshold) with a smooth release, followed by a NaN/Inf guard.
    // Stateless apart from the gain envelope; no allocation.
    class SafetyLimiter
    {
    public:
        static constexpr float threshold = 0.891f; // -1 dBFS

        void prepare (double sampleRate) noexcept
        {
            releaseCoeff = (float) std::exp (-1.0 / (0.08 * sampleRate)); // ~80 ms release
            gain = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const int numCh = buffer.getNumChannels();
            auto* const* ch = buffer.getArrayOfWritePointers();
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float peak = 0.0f;
                for (int c = 0; c < numCh; ++c)
                {
                    float& x = ch[c][i];
                    if (! std::isfinite (x)) x = 0.0f;
                    peak = juce::jmax (peak, std::abs (x));
                }

                const float target = peak > threshold ? threshold / peak : 1.0f;
                gain = target < gain ? target : target + (gain - target) * releaseCoeff;

                for (int c = 0; c < numCh; ++c)
                    ch[c][i] *= gain;
            }
            if (gain > 0.99999f) gain = 1.0f; // avoid creeping denormal-ish tails
        }

        float getCurrentGain() const noexcept { return gain; }

    private:
        float releaseCoeff = 0.9997f;
        float gain = 1.0f;
    };
}
