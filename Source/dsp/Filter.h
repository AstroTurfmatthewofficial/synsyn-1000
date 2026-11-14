#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace vf::dsp
{
    enum class FilterType : int { lowPass = 0, highPass, bandPass };

    // Topology-preserving-transform state-variable filter (Zavalishin / Simper,
    // "trapezoidal SVF"). Unconditionally stable for any cutoff modulation as
    // long as k > 0, which the resonance mapping guarantees (Q <= 20).
    class SvfFilter
    {
    public:
        static constexpr double minQ = 0.7071067811865476;
        static constexpr double maxQ = 20.0;

        void prepare (double sr) noexcept { sampleRate = sr; maxCutoff = juce::jmin (20000.0, 0.45 * sr); reset(); }
        void reset() noexcept             { ic1 = ic2 = 0.0; }
        void setType (FilterType t) noexcept { type = t; }

        // resonance 0..1 -> Q 0.707 (Butterworth, flat) .. maxQ (exponential)
        static double resonanceToQ (double res) noexcept { return minQ * std::pow (maxQ / minQ, juce::jlimit (0.0, 1.0, res)); }
        void setResonance (double res) noexcept { k = 1.0 / resonanceToQ (res); }

        void setCutoff (double hz) noexcept
        {
            const double fc = juce::jlimit (20.0, maxCutoff, hz);
            const double g = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float in) noexcept
        {
            const double v0 = in;
            const double v3 = v0 - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;

            switch (type)
            {
                case FilterType::highPass: return (float) (v0 - k * v1 - v2);
                case FilterType::bandPass: return (float) (k * v1);   // unity peak gain
                case FilterType::lowPass:
                default:                   return (float) v2;
            }
        }

        // Analog-prototype magnitude, used by the editor's response display.
        static double magnitude (FilterType t, double freq, double cutoff, double res) noexcept
        {
            const double kk = 1.0 / resonanceToQ (res);
            const double w = freq / cutoff;
            const double re = 1.0 - w * w, im = kk * w;
            const double den = std::sqrt (re * re + im * im);
            switch (t)
            {
                case FilterType::highPass: return (w * w) / den;
                case FilterType::bandPass: return (kk * w) / den;
                case FilterType::lowPass:
                default:                   return 1.0 / den;
            }
        }

    private:
        double sampleRate = 44100.0, maxCutoff = 19845.0;
        double k = 2.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
        double ic1 = 0.0, ic2 = 0.0;
        FilterType type = FilterType::lowPass;
    };
}
