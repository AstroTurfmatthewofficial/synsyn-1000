#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>

namespace vf::dsp
{
    enum class Waveform : int { sine = 0, saw, square, triangle, noise, numWaveforms };

    inline juce::StringArray waveformNames()
    {
        return { "Sine", "Saw", "Square", "Triangle", "Noise" };
    }

    // Band-limited (PolyBLEP / PolyBLAMP) oscillator. Header-only, no allocation,
    // safe to use on the audio thread. One instance per voice per oscillator slot.
    class Oscillator
    {
    public:
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            crossfadeLength = juce::jmax (1, (int) (0.005 * sampleRate)); // 5 ms waveform crossfade
        }

        // Changing waveform while sounding crossfades old -> new over 5 ms at the
        // same (continuous) phase, so switches don't click.
        void setWaveform (Waveform w) noexcept
        {
            if (w == waveform) return;
            previousWaveform = waveform;
            waveform = w;
            crossfadeRemaining = crossfadeLength;
        }

        void setFrequency (double hz) noexcept
        {
            // Keep below Nyquist so the BLEP residuals stay well-defined.
            increment = juce::jlimit (0.0, 0.45, hz / sampleRate);
        }

        // Called at note start: also cancels any pending crossfade.
        void resetPhase (double startPhase = 0.0) noexcept { phase = startPhase; crossfadeRemaining = 0; }
        void seedNoise (uint32_t seed) noexcept            { noiseState = seed != 0 ? seed : 0x9e3779b9u; }

        float nextSample() noexcept
        {
            double out = render (waveform, phase, increment);

            if (crossfadeRemaining > 0)
            {
                const double a = (double) crossfadeRemaining / (double) crossfadeLength;
                out = out * (1.0 - a) + render (previousWaveform, phase, increment) * a;
                --crossfadeRemaining;
            }

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            return (float) out;
        }

        double render (Waveform w, double t, double dt) noexcept
        {
            switch (w)
            {
                case Waveform::sine:
                    return std::sin (juce::MathConstants<double>::twoPi * t);

                case Waveform::saw:
                    return 2.0 * t - 1.0 - polyBlep (t, dt);

                case Waveform::square:
                    return (t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (std::fmod (t + 0.5, 1.0), dt);

                case Waveform::triangle:
                    // Naive triangle: +1 at t=0, -1 at t=0.5. Slope changes by -8 at t=0, +8 at t=0.5.
                    return 4.0 * std::abs (t - 0.5) - 1.0
                           - 8.0 * dt * polyBlamp (t, dt)
                           + 8.0 * dt * polyBlamp (std::fmod (t + 0.5, 1.0), dt);

                case Waveform::noise:
                    return nextNoise();

                case Waveform::numWaveforms:
                default:
                    return 0.0;
            }
        }

        // Polynomial band-limited step residual (2-sample).
        static double polyBlep (double t, double dt) noexcept
        {
            if (dt <= 0.0) return 0.0;
            if (t < dt)       { t /= dt;               return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return t * t + t + t + 1.0; }
            return 0.0;
        }

        // Integrated PolyBLEP (band-limited ramp residual) for slope discontinuities.
        static double polyBlamp (double t, double dt) noexcept
        {
            if (dt <= 0.0) return 0.0;
            if (t < dt)       { t = t / dt - 1.0;         return -t * t * t / 3.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt + 1.0; return  t * t * t / 3.0; }
            return 0.0;
        }

    private:
        float nextNoise() noexcept
        {
            // xorshift32 -> uniform [-1, 1)
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            return (float) ((double) noiseState * (2.0 / 4294967296.0) - 1.0);
        }

        double sampleRate = 44100.0;
        double phase = 0.0;
        double increment = 0.0;
        Waveform waveform = Waveform::saw, previousWaveform = Waveform::saw;
        int crossfadeLength = 220, crossfadeRemaining = 0;
        uint32_t noiseState = 0x9e3779b9u;
    };
}
