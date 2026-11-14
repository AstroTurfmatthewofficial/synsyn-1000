#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>

namespace vf::dsp
{
    constexpr int numLfos = 2;

    enum class LfoShape : int { sine = 0, triangle, sawUp, sawDown, square, sampleHold, numShapes };
    enum class LfoTarget : int { none = 0, cutoff, resonance, pitch, osc1Pitch, osc2Pitch, osc3Pitch,
                                 osc1Level, osc2Level, osc3Level, amp, numTargets };

    inline juce::StringArray lfoShapeNames()  { return { "Sine", "Tri", "Saw Up", "Saw Dn", "Square", "S&H" }; }
    inline juce::StringArray lfoTargetNames() { return { "None", "Cutoff", "Reso", "Pitch", "Pitch 1", "Pitch 2", "Pitch 3",
                                                         "Level 1", "Level 2", "Level 3", "Amp" }; }
    inline juce::StringArray lfoModeNames()   { return { "Retrig", "Free" }; }

    // Tempo-sync divisions: name + length in quarter-note beats.
    struct LfoDivision { const char* name; double beats; };
    inline constexpr LfoDivision lfoDivisions[] = {
        { "4/1", 16.0 }, { "2/1", 8.0 }, { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/2 D", 3.0 }, { "1/2 T", 4.0 / 3.0 },
        { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/4 T", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/8 D", 0.75 }, { "1/8 T", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/16 T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/32 T", 1.0 / 12.0 } };
    inline constexpr int numLfoDivisions = (int) (sizeof (lfoDivisions) / sizeof (lfoDivisions[0]));
    inline constexpr int defaultLfoDivision = 6; // 1/4

    inline juce::StringArray lfoDivisionNames()
    {
        juce::StringArray s;
        for (auto& d : lfoDivisions) s.add (d.name);
        return s;
    }

    // Modulation depth at amount = +/-100 %.
    struct LfoRanges
    {
        static constexpr float cutoffOctaves = 4.0f;
        static constexpr float pitchSemitones = 12.0f;
        static constexpr float resonance = 1.0f;
    };

    // Stateless LFO evaluation from a running position (integer part = cycle
    // index, fraction = phase). Sample & hold is a hash of the cycle index, so
    // a free-running (global) LFO can be evaluated by every voice from just the
    // block-start position, with no shared buffers.
    struct Lfo
    {
        static float hashToBipolar (uint32_t x) noexcept
        {
            x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
            return (float) ((double) x * (2.0 / 4294967296.0) - 1.0);
        }

        static float evaluate (LfoShape shape, double position, uint32_t seed) noexcept
        {
            const double cycle = std::floor (position);
            const float p = (float) (position - cycle);
            switch (shape)
            {
                case LfoShape::sine:     return std::sin (juce::MathConstants<float>::twoPi * p);
                case LfoShape::triangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
                case LfoShape::sawUp:    return 2.0f * p - 1.0f;
                case LfoShape::sawDown:  return 1.0f - 2.0f * p;
                case LfoShape::square:   return p < 0.5f ? 1.0f : -1.0f;
                case LfoShape::sampleHold: return hashToBipolar (seed ^ (uint32_t) (int64_t) cycle * 0x9E3779B1u);
                case LfoShape::numShapes:
                default: return 0.0f;
            }
        }
    };
}
