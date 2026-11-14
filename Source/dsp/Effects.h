#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

// Effects chain: chorus -> delay -> reverb (stereo). All buffers are allocated
// in prepare(); process() never allocates. Each effect has a smoothed bypass
// ramp; when fully bypassed it is skipped entirely (no CPU), and it is cleared
// when switched back on so no stale audio reappears.
namespace vf::dsp
{
    //==========================================================================
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }
        int capacity() const noexcept { return mask - 3; }

        void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }

        // Delay in samples (>= 1) relative to the most recently pushed sample; cubic Hermite.
        float read (float delay) const noexcept
        {
            delay = juce::jlimit (1.0f, (float) capacity(), delay);
            const int k = (int) delay;
            const float t = delay - (float) k;
            const float y0 = at (k - 1), y1 = at (k), y2 = at (k + 1), y3 = at (k + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }

    private:
        float at (int k) const noexcept { return buffer[(size_t) ((write - 1 - k) & mask)]; }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    //==========================================================================
    // Smooth on/off ramp (30 ms). Tells the effect when it can be skipped and
    // when it has to clear its state before resuming.
    class BypassRamp
    {
    public:
        void prepare (double sr) { ramp.reset (sr, 0.03); ramp.setCurrentAndTargetValue (0.0f); }
        // Returns true if the effect must run this block. needsReset is set when
        // it is coming back from a fully-off state.
        bool begin (bool on, bool& needsReset) noexcept
        {
            const bool fullyOff = ! ramp.isSmoothing() && ramp.getCurrentValue() <= 0.0f;
            needsReset = fullyOff && on;
            ramp.setTargetValue (on ? 1.0f : 0.0f);
            return on || ! fullyOff;
        }
        float next() noexcept { return ramp.getNextValue(); }
        void forceOff() noexcept { ramp.setCurrentAndTargetValue (0.0f); }
    private:
        juce::SmoothedValue<float> ramp;
    };

    // Transparent below |x| = 1, soft-saturates above (bounded to +/-2). Used in
    // the delay feedback path so extreme settings can't build up unbounded.
    inline float feedbackSaturate (float x) noexcept
    {
        const float a = std::abs (x);
        return a <= 1.0f ? x : std::copysign (1.0f + std::tanh (a - 1.0f), x);
    }

    // Dry/wet law: unity dry up to 50 % mix, unity wet from 50 % mix.
    inline float dryGainFor (float mix) noexcept { return juce::jmin (1.0f, 2.0f * (1.0f - mix)); }
    inline float wetGainFor (float mix) noexcept { return juce::jmin (1.0f, 2.0f * mix); }

    //==========================================================================
    struct ChorusParams { bool on = false; float rateHz = 0.6f; float depth = 0.5f; float mix = 0.5f; };
    struct DelayParams  { bool on = false; float timeMs = 375.0f; float feedback = 0.35f; bool pingPong = false;
                          float highCutHz = 6000.0f; float mix = 0.3f; };
    struct ReverbParams { bool on = false; float size = 0.6f; float damping = 0.5f; float width = 1.0f;
                          float preDelayMs = 20.0f; float mix = 0.25f; };
    struct FxParams { ChorusParams chorus; DelayParams delay; ReverbParams reverb; };

    //==========================================================================
    // Stereo 4-tap chorus (2 modulated taps per side, quadrature LFO phases).
    class Chorus
    {
    public:
        static constexpr float baseMs = 9.0f, sweepMs = 8.0f;

        void prepare (double sr)
        {
            sampleRate = sr;
            const int maxSamples = (int) std::ceil ((baseMs + sweepMs + 2.0f) * 0.001 * sr);
            lineL.prepare (maxSamples); lineR.prepare (maxSamples);
            bypass.prepare (sr);
            depth.reset (sr, 0.05); mix.reset (sr, 0.05);
            reset();
        }
        void reset() noexcept { lineL.clear(); lineR.clear(); phase = 0.0; }

        void process (float* L, float* R, int n, const ChorusParams& p) noexcept
        {
            bool needsReset = false;
            if (! bypass.begin (p.on, needsReset)) return;
            if (needsReset) { reset(); depth.setCurrentAndTargetValue (p.depth); mix.setCurrentAndTargetValue (p.mix); }
            depth.setTargetValue (p.depth);
            mix.setTargetValue (p.mix);
            const double inc = (double) p.rateHz / sampleRate;
            const float msToSamples = (float) (0.001 * sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const float dl = L[i], dr = R[i];
                lineL.push (dl); lineR.push (dr);

                const float sweep = depth.getNextValue() * sweepMs;
                auto tap = [&] (double offset)
                {
                    const float s = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * (float) std::fmod (phase + offset, 1.0));
                    return (baseMs + sweep * s) * msToSamples;
                };
                const float wl = 0.5f * (lineL.read (tap (0.0)) + lineL.read (tap (0.5)));
                const float wr = 0.5f * (lineR.read (tap (0.25)) + lineR.read (tap (0.75)));
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;

                const float m = mix.getNextValue(), dg = dryGainFor (m), wg = wetGainFor (m);
                const float e = bypass.next();
                L[i] = dl + e * (dl * dg + wl * wg - dl);
                R[i] = dr + e * (dr * dg + wr * wg - dr);
            }
        }

    private:
        double sampleRate = 44100.0, phase = 0.0;
        DelayLine lineL, lineR;
        BypassRamp bypass;
        juce::SmoothedValue<float> depth, mix;
    };

    //==========================================================================
    // Stereo / ping-pong feedback delay with a one-pole high-cut in the loop.
    class StereoDelay
    {
    public:
        static constexpr double maxDelaySeconds = 4.0;
        static constexpr float maxFeedback = 0.95f;

        void prepare (double sr)
        {
            sampleRate = sr;
            const int maxSamples = (int) std::ceil (maxDelaySeconds * sr) + 8;
            lineL.prepare (maxSamples); lineR.prepare (maxSamples);
            bypass.prepare (sr);
            delaySamples.reset (sr, 0.2);
            feedback.reset (sr, 0.05); mix.reset (sr, 0.05);
            reset();
        }
        void reset() noexcept { lineL.clear(); lineR.clear(); lpL = lpR = 0.0f; }

        void process (float* L, float* R, int n, const DelayParams& p) noexcept
        {
            bool needsReset = false;
            if (! bypass.begin (p.on, needsReset)) return;
            const float target = juce::jlimit (1.0f, (float) (maxDelaySeconds * sampleRate), p.timeMs * 0.001f * (float) sampleRate);
            const float fb = juce::jlimit (0.0f, maxFeedback, p.feedback);
            if (needsReset)
            {
                reset();
                delaySamples.setCurrentAndTargetValue (target);
                feedback.setCurrentAndTargetValue (fb);
                mix.setCurrentAndTargetValue (p.mix);
            }
            delaySamples.setTargetValue (target);
            feedback.setTargetValue (fb);
            mix.setTargetValue (p.mix);
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                             * juce::jmin (p.highCutHz, 0.45f * (float) sampleRate) / (float) sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const float dl = L[i], dr = R[i];
                const float d = delaySamples.getNextValue();
                const float g = feedback.getNextValue();
                const float wl = lineL.read (d), wr = lineR.read (d);
                lpL += a * (wl - lpL);
                lpR += a * (wr - lpR);

                if (p.pingPong)
                {
                    lineL.push (feedbackSaturate (0.5f * (dl + dr) + g * lpR));
                    lineR.push (feedbackSaturate (g * lpL));
                }
                else
                {
                    lineL.push (feedbackSaturate (dl + g * lpL));
                    lineR.push (feedbackSaturate (dr + g * lpR));
                }

                const float m = mix.getNextValue(), dg = dryGainFor (m), wg = wetGainFor (m);
                const float e = bypass.next();
                L[i] = dl + e * (dl * dg + wl * wg - dl);
                R[i] = dr + e * (dr * dg + wr * wg - dr);
            }
        }

    private:
        double sampleRate = 44100.0;
        DelayLine lineL, lineR;
        float lpL = 0.0f, lpR = 0.0f;
        BypassRamp bypass;
        juce::SmoothedValue<float> delaySamples, feedback, mix;
    };

    //==========================================================================
    // Pre-delay + juce::Reverb (Freeverb-style), wet-only, mixed here.
    class ReverbFx
    {
    public:
        static constexpr float maxPreDelayMs = 250.0f;

        void prepare (double sr, int maxBlock)
        {
            sampleRate = sr;
            reverb.setSampleRate (sr);
            preL.prepare ((int) std::ceil (maxPreDelayMs * 0.001 * sr) + 8);
            preR.prepare ((int) std::ceil (maxPreDelayMs * 0.001 * sr) + 8);
            wetL.assign ((size_t) maxBlock, 0.0f);
            wetR.assign ((size_t) maxBlock, 0.0f);
            bypass.prepare (sr);
            preDelay.reset (sr, 0.1);
            mix.reset (sr, 0.05);
            reset();
        }
        int maxBlock() const noexcept { return (int) wetL.size(); }
        void reset() noexcept { reverb.reset(); preL.clear(); preR.clear(); }

        // n must be <= maxBlock() (the chain splits larger blocks).
        void process (float* L, float* R, int n, const ReverbParams& p) noexcept
        {
            bool needsReset = false;
            if (! bypass.begin (p.on, needsReset)) return;
            const float pd = juce::jlimit (1.0f, maxPreDelayMs * 0.001f * (float) sampleRate, p.preDelayMs * 0.001f * (float) sampleRate);
            if (needsReset) { reset(); preDelay.setCurrentAndTargetValue (pd); mix.setCurrentAndTargetValue (p.mix); }
            preDelay.setTargetValue (pd);
            mix.setTargetValue (p.mix);

            juce::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit (0.0f, 1.0f, p.size);
            rp.damping = juce::jlimit (0.0f, 1.0f, p.damping);
            rp.width = juce::jlimit (0.0f, 1.0f, p.width);
            rp.wetLevel = 0.5f;
            rp.dryLevel = 0.0f;
            rp.freezeMode = 0.0f;
            const auto& cur = reverb.getParameters();
            if (needsReset || ! juce::exactlyEqual (cur.roomSize, rp.roomSize) || ! juce::exactlyEqual (cur.damping, rp.damping)
                || ! juce::exactlyEqual (cur.width, rp.width))
                reverb.setParameters (rp);

            for (int i = 0; i < n; ++i)
            {
                preL.push (L[i]); preR.push (R[i]);
                const float d = preDelay.getNextValue();
                wetL[(size_t) i] = preL.read (d);
                wetR[(size_t) i] = preR.read (d);
            }
            reverb.processStereo (wetL.data(), wetR.data(), n);

            for (int i = 0; i < n; ++i)
            {
                const float m = mix.getNextValue(), dg = dryGainFor (m), wg = wetGainFor (m);
                const float e = bypass.next();
                const float dl = L[i], dr = R[i];
                L[i] = dl + e * (dl * dg + wetL[(size_t) i] * wg - dl);
                R[i] = dr + e * (dr * dg + wetR[(size_t) i] * wg - dr);
            }
        }

    private:
        double sampleRate = 44100.0;
        juce::Reverb reverb;
        DelayLine preL, preR;
        std::vector<float> wetL, wetR;
        BypassRamp bypass;
        juce::SmoothedValue<float> preDelay, mix;
    };

    //==========================================================================
    class FxChain
    {
    public:
        void prepare (double sr, int maxBlockSize)
        {
            const int block = juce::jmax (512, maxBlockSize);
            chorus.prepare (sr);
            delay.prepare (sr);
            reverb.prepare (sr, block);
            monoScratch.assign ((size_t) block, 0.0f);
        }

        void reset() noexcept { chorus.reset(); delay.reset(); reverb.reset(); }

        // In-place on a mono or stereo buffer. A mono buffer is processed as
        // stereo internally and folded back to mono.
        void process (juce::AudioBuffer<float>& buffer, const FxParams& p) noexcept
        {
            // Each effect returns immediately when it is off and fully ramped
            // down, so with everything off the buffer is untouched (bit-exact).
            const int numCh = buffer.getNumChannels();
            if (numCh == 0) return;
            const int total = buffer.getNumSamples();
            const int maxChunk = reverb.maxBlock();

            for (int start = 0; start < total; start += maxChunk)
            {
                const int n = juce::jmin (maxChunk, total - start);
                float* L = buffer.getWritePointer (0, start);
                float* R = numCh > 1 ? buffer.getWritePointer (1, start) : monoScratch.data();
                if (numCh == 1) std::copy (L, L + n, R);

                chorus.process (L, R, n, p.chorus);
                delay.process (L, R, n, p.delay);
                reverb.process (L, R, n, p.reverb);

                if (numCh == 1)
                    for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
            }
        }

    private:
        Chorus chorus;
        StereoDelay delay;
        ReverbFx reverb;
        std::vector<float> monoScratch;
    };
}
