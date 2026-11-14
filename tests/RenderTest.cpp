// Offline render + sanity checks for SynSyn 1000 (formerly VST Forge Synth).
// Usage: VSTForgeRenderTest [outputDir]
// Writes WAV renders, m2_demo.wav and m2_editor.png. Returns non-zero on failure.

#include "Legal.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace
{
    constexpr double sampleRate = 48000.0;
    double renderRate = sampleRate;   // rate used by render()/makeProcessor()/writeWav()
    constexpr int blockSize = 256;
    namespace id = vf::params::id;
    using vf::params::oscId;

    struct Note { double start, length; int note; float velocity; };

    std::vector<Note> phrase (double offset)
    {
        return {
            { offset + 0.00, 0.70, 48, 0.9f }, { offset + 0.00, 0.70, 55, 0.8f },
            { offset + 0.00, 0.70, 60, 0.8f }, { offset + 0.00, 0.70, 64, 0.8f },
            { offset + 0.80, 0.15, 72, 1.0f }, { offset + 0.95, 0.15, 74, 0.9f },
            { offset + 1.10, 0.15, 76, 0.9f }, { offset + 1.25, 0.25, 79, 1.0f },
        };
    }

    std::vector<Note> chord (double start, double len, std::initializer_list<int> notes, float vel = 0.9f)
    {
        std::vector<Note> v;
        for (int n : notes) v.push_back ({ start, len, n, vel });
        return v;
    }

    void append (std::vector<Note>& a, const std::vector<Note>& b) { a.insert (a.end(), b.begin(), b.end()); }

    void setParam (VSTForgeAudioProcessor& p, const juce::String& pid, float plainValue)
    {
        auto* param = p.getState().getParameter (pid);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    float getParam (VSTForgeAudioProcessor& p, const juce::String& pid)
    {
        return p.getState().getRawParameterValue (pid)->load();
    }

    struct Stats { float peak = 0; double rms = 0; int64_t nonFinite = 0, denormals = 0; };

    Stats analyse (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
    {
        if (len < 0) len = b.getNumSamples() - start;
        Stats s; double sumSq = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = start; i < start + len; ++i)
            {
                const float x = b.getSample (c, i);
                if (! std::isfinite (x)) { ++s.nonFinite; continue; }
                if (std::fpclassify (x) == FP_SUBNORMAL) ++s.denormals;
                s.peak = std::max (s.peak, std::abs (x));
                sumSq += (double) x * x;
            }
        s.rms = std::sqrt (sumSq / std::max<int64_t> (1, (int64_t) len * b.getNumChannels()));
        return s;
    }

    float db (double g) { return juce::Decibels::gainToDecibels ((float) g, -200.0f); }

    juce::AudioBuffer<float> render (VSTForgeAudioProcessor& p, const std::vector<Note>& notes, double seconds,
                                     std::function<void (double)> onBlock = {})
    {
        const int total = (int) std::ceil (seconds * renderRate);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        struct Ev { int64_t pos; juce::MidiMessage msg; };
        std::vector<Ev> events;
        for (auto& n : notes)
        {
            events.push_back ({ (int64_t) (n.start * renderRate), juce::MidiMessage::noteOn (1, n.note, n.velocity) });
            events.push_back ({ (int64_t) ((n.start + n.length) * renderRate), juce::MidiMessage::noteOff (1, n.note) });
        }

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const int n = std::min (blockSize, total - pos);
            if (onBlock) onBlock (pos / renderRate);
            midi.clear();
            for (auto& e : events)
                if (e.pos >= pos && e.pos < pos + n)
                    midi.addEvent (e.msg, (int) (e.pos - pos));
            block.setSize (2, n, false, false, true);
            p.processBlock (block, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, pos, block, c, 0, n);
        }
        return out;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        if (os == nullptr) return false;
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (renderRate).withNumChannels (2).withBitsPerSample (24);
        auto writer = wav.createWriterFor (os, opts);
        return writer != nullptr && writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    std::unique_ptr<VSTForgeAudioProcessor> makeProcessor()
    {
        auto p = std::make_unique<VSTForgeAudioProcessor>();
        p->setPlayConfigDetails (0, 2, renderRate, blockSize);
        p->prepareToPlay (renderRate, blockSize);
        return p;
    }

    bool ok = true;
    juce::File outDir;

    // Standard audio checks + table row. minRmsDb guards against silence.
    void check (const char* name, const juce::AudioBuffer<float>& buf, const char* wavName, float minRmsDb = -40.0f)
    {
        const auto s = analyse (buf);
        const bool pass = s.nonFinite == 0 && s.denormals == 0 && s.peak < 1.0f && db (s.rms) > minRmsDb;
        ok &= pass;
        std::printf ("%-24s %9.2f %9.2f %8lld %9lld  %s\n", name, db (s.peak), db (s.rms),
                     (long long) s.nonFinite, (long long) s.denormals, pass ? "PASS" : "FAIL");
        if (wavName != nullptr && ! writeWav (outDir.getChildFile (wavName), buf))
        { std::printf ("  failed to write %s\n", wavName); ok = false; }
    }

    void result (const char* what, bool pass, const juce::String& detail)
    {
        ok &= pass;
        std::printf ("%-44s %s  (%s)\n", what, pass ? "PASS" : "FAIL", detail.toRawUTF8());
    }

    // "Brightness" = RMS of first difference / RMS (rises as a low-pass opens).
    double brightness (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double d = 0, e = 0;
        for (int i = start + 1; i < start + len; ++i)
        {
            const double x = b.getSample (0, i), dx = x - b.getSample (0, i - 1);
            d += dx * dx; e += x * x;
        }
        return std::sqrt (d / juce::jmax (e, 1.0e-12));
    }

    //==========================================================================
    // Backward-compatibility scenarios. References were rendered with v0.2.0
    // (tests/reference/*.f32, raw interleaved float32 stereo) and every later
    // version must reproduce them (old sessions must sound identical).
    struct CompatScenario { const char* name; std::function<juce::AudioBuffer<float>()> render; };

    juce::File referenceDir()
    {
        return juce::File (VSTFORGE_SOURCE_DIR).getChildFile ("tests/reference");
    }

    std::unique_ptr<VSTForgeAudioProcessor> loadSession (const juce::File& f)
    {
        auto p = makeProcessor();
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        return p;
    }

    void configureM2Session (VSTForgeAudioProcessor& p)
    {
        for (int i = 0; i < 3; ++i) { setParam (p, oscId (i, "on"), 1.0f); setParam (p, oscId (i, "level"), 0.7f); }
        setParam (p, oscId (0, "wave"), 1.0f);
        setParam (p, oscId (1, "wave"), 2.0f); setParam (p, oscId (1, "fine"), 9.0f);
        setParam (p, oscId (2, "wave"), 3.0f); setParam (p, oscId (2, "coarse"), -12.0f);
        setParam (p, id::filterType, 0.0f); setParam (p, id::filterCutoff, 600.0f); setParam (p, id::filterRes, 0.7f);
        setParam (p, id::filterKeytrack, 0.5f); setParam (p, id::filterEnvAmt, 0.6f);
        setParam (p, id::fenvDecay, 0.3f); setParam (p, id::fenvSustain, 0.2f);
        setParam (p, id::gain, -3.0f);
    }

    void configureM3Session (VSTForgeAudioProcessor& p);
    void configureM4Session (VSTForgeAudioProcessor& p);

    std::vector<CompatScenario> compatScenarios()
    {
        std::vector<CompatScenario> v;
        v.push_back ({ "default_phrase", [] { auto p = makeProcessor(); return render (*p, phrase (0.05), 2.2); } });
        v.push_back ({ "m1_session", []
        {
            juce::XmlElement m1 ("VSTForgeSynth");
            auto add = [&] (const char* pid, double val) { auto* e = m1.createNewChildElement ("PARAM"); e->setAttribute ("id", pid); e->setAttribute ("value", val); };
            add ("osc1_wave", 2); add ("amp_attack", 0.02); add ("amp_decay", 0.3); add ("amp_sustain", 0.5);
            add ("amp_release", 0.4); add ("master_gain", -4.0);
            juce::MemoryBlock d; juce::AudioProcessor::copyXmlToBinary (m1, d);
            auto p = makeProcessor(); p->setStateInformation (d.getData(), (int) d.getSize());
            return render (*p, phrase (0.05), 2.2);
        } });
        v.push_back ({ "m2_session", []
        {
            auto p = loadSession (referenceDir().getChildFile ("m2_session.state"));
            auto notes = chord (0.0, 1.2, { 45, 52, 57, 60 });
            append (notes, phrase (1.3));
            return render (*p, notes, 3.2);
        } });
        v.push_back ({ "m2_sweep_switch", []
        {
            auto p = makeProcessor();
            configureM2Session (*p);
            return render (*p, chord (0.0, 1.6, { 41, 48, 53 }), 2.0, [&] (double t)
            {
                setParam (*p, id::filterCutoff, (float) (100.0 * std::pow (60.0, t / 2.0)));
                setParam (*p, oscId (0, "wave"), t > 0.8 ? 3.0f : 1.0f);
                p->setUiPitchWheel (t > 1.0 ? 12000 : 8192);
            });
        } });
        v.push_back ({ "m3_session", []
        {
            auto p = loadSession (referenceDir().getChildFile ("m3_session.state"));
            p->setUiModWheel (100);
            auto notes = chord (0.0, 1.4, { 45, 52, 57, 64 });
            append (notes, phrase (1.5));
            return render (*p, notes, 3.6);
        } });
        v.push_back ({ "m4_session", []
        {
            auto p = loadSession (referenceDir().getChildFile ("m4_session.state"));
            p->setUiModWheel (90);
            auto notes = chord (0.0, 1.4, { 45, 52, 57, 64 });
            append (notes, phrase (1.5));
            return render (*p, notes, 4.5);
        } });
        return v;
    }

    void configureM3Session (VSTForgeAudioProcessor& p)
    {
        configureM2Session (p);
        using vf::params::lfoId;
        // LFO 1: delayed sine vibrato, mod wheel scaled. LFO 2: free, synced S&H -> cutoff.
        setParam (p, lfoId (0, "wave"), 0.0f); setParam (p, lfoId (0, "rate"), 5.5f); setParam (p, lfoId (0, "target"), 3.0f);
        setParam (p, lfoId (0, "amount"), 0.04f); setParam (p, lfoId (0, "delay"), 0.3f);
        setParam (p, id::lfo1ModWheel, 1.0f);
        setParam (p, lfoId (1, "wave"), 5.0f); setParam (p, lfoId (1, "sync"), 1.0f); setParam (p, lfoId (1, "div"), 12.0f);
        setParam (p, lfoId (1, "mode"), 1.0f); setParam (p, lfoId (1, "target"), 1.0f); setParam (p, lfoId (1, "amount"), 0.5f);
    }

    void configureM4Session (VSTForgeAudioProcessor& p)
    {
        configureM3Session (p);
        auto set = [&] (const char* pid, float v) { setParam (p, pid, v); };
        set (id::chorusOn, 1); set (id::chorusRate, 0.8f); set (id::chorusDepth, 0.6f); set (id::chorusMix, 0.45f);
        set (id::delayOn, 1); set (id::delaySync, 1); set (id::delayDiv, 10); set (id::delayFeedback, 0.5f);
        set (id::delayPingPong, 1); set (id::delayHighCut, 4500.0f); set (id::delayMix, 0.3f);
        set (id::reverbOn, 1); set (id::reverbSize, 0.75f); set (id::reverbDamping, 0.4f); set (id::reverbWidth, 0.9f);
        set (id::reverbPreDelay, 35.0f); set (id::reverbMix, 0.3f);
    }

    bool writeRaw (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        juce::MemoryOutputStream mo;
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c) mo.writeFloat (b.getSample (c, i));
        return f.replaceWithData (mo.getData(), mo.getDataSize());
    }

    int dumpReferences()
    {
        auto dir = referenceDir();
        dir.createDirectory();
        {
            auto p = makeProcessor();
            configureM2Session (*p);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            if (! dir.getChildFile ("m2_session.state").existsAsFile())
                dir.getChildFile ("m2_session.state").replaceWithData (mb.getData(), mb.getSize());
        }
        {
            auto p = makeProcessor();
            configureM3Session (*p);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            if (! dir.getChildFile ("m3_session.state").existsAsFile())
                dir.getChildFile ("m3_session.state").replaceWithData (mb.getData(), mb.getSize());
        }
        {
            auto p = makeProcessor();
            configureM4Session (*p);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            if (! dir.getChildFile ("m4_session.state").existsAsFile())
                dir.getChildFile ("m4_session.state").replaceWithData (mb.getData(), mb.getSize());
        }
        for (auto& sc : compatScenarios())
        {
            auto f = dir.getChildFile (juce::String (sc.name) + ".f32");
            if (! writeRaw (f, sc.render())) return 1;
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        return 0;
    }

    juce::AudioBuffer<float> readRaw (const juce::File& f)
    {
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        juce::MemoryInputStream in (mb, false);
        const int n = (int) (mb.getSize() / 8);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c) b.setSample (c, i, in.readFloat());
        return b;
    }

    //==========================================================================
    // LFO helpers / measurements
    using vf::params::lfoId;

    struct LfoSetup
    {
        int wave = 0; float rate = 2.0f; int target = 0; float amount = 0.0f;
        int mode = 0; bool sync = false; int div = vf::dsp::defaultLfoDivision;
        float depth = 1.0f; float delay = 0.0f; float phase = 0.0f;
    };

    void setLfo (VSTForgeAudioProcessor& p, int k, const LfoSetup& l)
    {
        setParam (p, lfoId (k, "wave"), (float) l.wave);   setParam (p, lfoId (k, "rate"), l.rate);
        setParam (p, lfoId (k, "target"), (float) l.target); setParam (p, lfoId (k, "amount"), l.amount);
        setParam (p, lfoId (k, "mode"), (float) l.mode);   setParam (p, lfoId (k, "sync"), l.sync ? 1.0f : 0.0f);
        setParam (p, lfoId (k, "div"), (float) l.div);     setParam (p, lfoId (k, "depth"), l.depth);
        setParam (p, lfoId (k, "delay"), l.delay);         setParam (p, lfoId (k, "phase"), l.phase);
    }

    int divIndex (const char* name)
    {
        for (int i = 0; i < vf::dsp::numLfoDivisions; ++i)
            if (juce::String (vf::dsp::lfoDivisions[i].name) == name) return i;
        return vf::dsp::defaultLfoDivision;
    }

    // Instantaneous frequency (Hz) per window from interpolated rising zero crossings.
    std::vector<double> pitchTrack (const juce::AudioBuffer<float>& b, int start, int end, int window)
    {
        std::vector<double> out;
        for (int w = start; w + window <= end; w += window)
        {
            double first = -1, last = -1; int count = 0;
            for (int i = w + 1; i < w + window; ++i)
            {
                const float a = b.getSample (0, i - 1), c = b.getSample (0, i);
                if (a < 0.0f && c >= 0.0f)
                {
                    const double t = (i - 1) + (double) (-a) / (double) (c - a);
                    if (first < 0) first = t;
                    last = t; ++count;
                }
            }
            if (count >= 2) out.push_back ((count - 1) * sampleRate / (last - first));
        }
        return out;
    }

    std::vector<double> windowed (const juce::AudioBuffer<float>& b, int start, int end, int window, bool bright)
    {
        std::vector<double> out;
        for (int w = start; w + window <= end; w += window)
            out.push_back (bright ? brightness (b, w, window) : analyse (b, w, window).rms);
        return out;
    }

    double rangeCents (const std::vector<double>& f)
    {
        if (f.empty()) return 0;
        auto [mn, mx] = std::minmax_element (f.begin(), f.end());
        return 1200.0 * std::log2 (*mx / *mn);
    }

    double maxMinRatio (const std::vector<double>& v)
    {
        auto [mn, mx] = std::minmax_element (v.begin(), v.end());
        return *mx / juce::jmax (*mn, 1.0e-12);
    }

    double autocorr (const std::vector<double>& v, int lag)
    {
        double mean = 0; for (double x : v) mean += x; mean /= (double) v.size();
        double num = 0, den = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            den += (v[i] - mean) * (v[i] - mean);
            if (i + (size_t) lag < v.size()) num += (v[i] - mean) * (v[i + (size_t) lag] - mean);
        }
        return num / juce::jmax (den, 1.0e-12);
    }

    double coefVar (const std::vector<double>& v)
    {
        double mean = 0, var = 0;
        for (double x : v) mean += x;
        mean /= (double) v.size();
        for (double x : v) var += (x - mean) * (x - mean);
        return std::sqrt (var / (double) v.size()) / juce::jmax (mean, 1.0e-12);
    }

    double stereoWidth (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
    {
        if (len < 0) len = b.getNumSamples() - start;
        double side = 0, mid = 0;
        for (int i = start; i < start + len; ++i)
        {
            const double l = b.getSample (0, i), r = b.getSample (1, i);
            side += (l - r) * (l - r); mid += (l + r) * (l + r);
        }
        return std::sqrt (side / juce::jmax (mid, 1.0e-20));   // side/mid RMS ratio
    }

    struct FxSetup
    {
        bool chorus = false, delay = false, reverb = false;
        float chorusRate = 0.6f, chorusDepth = 0.6f, chorusMix = 0.5f;
        float delayMs = 300.0f, feedback = 0.45f, highCut = 6000.0f, delayMix = 0.35f; bool pingPong = false, sync = false; int div = 10;
        float size = 0.6f, damping = 0.5f, width = 1.0f, preDelay = 20.0f, reverbMix = 0.3f;
    };

    void setFx (VSTForgeAudioProcessor& p, const FxSetup& f)
    {
        setParam (p, id::chorusOn, f.chorus ? 1.0f : 0.0f); setParam (p, id::chorusRate, f.chorusRate);
        setParam (p, id::chorusDepth, f.chorusDepth); setParam (p, id::chorusMix, f.chorusMix);
        setParam (p, id::delayOn, f.delay ? 1.0f : 0.0f); setParam (p, id::delayTime, f.delayMs);
        setParam (p, id::delayFeedback, f.feedback); setParam (p, id::delayHighCut, f.highCut); setParam (p, id::delayMix, f.delayMix);
        setParam (p, id::delayPingPong, f.pingPong ? 1.0f : 0.0f); setParam (p, id::delaySync, f.sync ? 1.0f : 0.0f);
        setParam (p, id::delayDiv, (float) f.div);
        setParam (p, id::reverbOn, f.reverb ? 1.0f : 0.0f); setParam (p, id::reverbSize, f.size); setParam (p, id::reverbDamping, f.damping);
        setParam (p, id::reverbWidth, f.width); setParam (p, id::reverbPreDelay, f.preDelay); setParam (p, id::reverbMix, f.reverbMix);
    }

    // Time (s) after which every 10 ms window stays below the given level.
    double timeBelow (const juce::AudioBuffer<float>& b, float levelDb, int from)
    {
        const int w = (int) (0.01 * renderRate);
        int lastLoud = from;
        for (int i = from; i + w <= b.getNumSamples(); i += w)
            if (db (analyse (b, i, w).peak) > levelDb) lastLoud = i + w;
        return lastLoud / renderRate;
    }

    //==========================================================================
    // M5: presets
    using vf::presets::PresetManager;

    struct Phrase { std::vector<Note> notes; double seconds; };

    Phrase categoryPhrase (const juce::String& cat)
    {
        Phrase ph;
        auto& n = ph.notes;
        if (cat == "Bass")
        {
            const int line[] = { 36, 36, 43, 36, 39, 41, 43, 31 };
            for (int i = 0; i < 8; ++i)
                n.push_back ({ i < 7 ? i * 0.25 : 2.0, i == 6 ? 0.45 : (i == 7 ? 0.5 : 0.2), line[i], i % 2 == 0 ? 1.0f : 0.85f });
            ph.seconds = 3.0;
        }
        else if (cat == "Lead")
        {
            n = { { 0.0, 0.28, 72, 0.9f }, { 0.3, 0.28, 74, 0.85f }, { 0.6, 0.28, 76, 0.9f }, { 0.9, 0.9, 79, 1.0f },
                  { 1.85, 0.28, 77, 0.85f }, { 2.15, 0.6, 76, 0.9f } };
            ph.seconds = 3.4;
        }
        else if (cat == "Pad")
        {
            n = chord (0.0, 2.6, { 48, 55, 60, 64, 67 }, 0.8f);
            ph.seconds = 4.5;
        }
        else if (cat == "Pluck")
        {
            const int arp[] = { 60, 64, 67, 72, 76, 72, 67, 64, 60, 64, 67, 72 };
            for (int i = 0; i < 12; ++i) n.push_back ({ i * 0.15, 0.12, arp[i], i % 4 == 0 ? 1.0f : 0.85f });
            append (n, chord (1.9, 0.2, { 48, 60, 67 }, 0.95f));
            ph.seconds = 3.4;
        }
        else if (cat == "Keys")
        {
            append (n, chord (0.0, 0.55, { 48, 55, 60, 64 }, 0.85f));
            append (n, chord (0.6, 0.55, { 53, 57, 60, 65 }, 0.85f));
            append (n, chord (1.2, 0.55, { 55, 59, 62, 67 }, 0.85f));
            append (n, chord (1.8, 1.0, { 48, 55, 60, 64 }, 0.9f));
            ph.seconds = 3.6;
        }
        else // FX
        {
            n = chord (0.0, 2.2, { 48, 60, 67 }, 0.9f);
            ph.seconds = 4.5;
        }
        return ph;
    }

    struct PresetRender { juce::AudioBuffer<float> audio; float minLimiterGain = 1.0f; };

    PresetRender renderWithPreset (const std::function<bool (VSTForgeAudioProcessor&)>& load, const juce::String& category,
                                   double maxSeconds = 0.0)
    {
        auto p = makeProcessor();
        PresetRender r;
        if (! load (*p)) return r;
        auto ph = categoryPhrase (category);
        r.audio = render (*p, ph.notes, maxSeconds > 0.0 ? std::min (maxSeconds, ph.seconds) : ph.seconds,
                          [&] (double) { r.minLimiterGain = std::min (r.minLimiterGain, p->getLimiterGain()); });
        r.minLimiterGain = std::min (r.minLimiterGain, p->getLimiterGain());
        return r;
    }

    // Plain (denormalised) parameter values = what the APVTS state stores and the DSP reads.
    std::vector<float> paramValues (VSTForgeAudioProcessor& p)
    {
        std::vector<float> v;
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                v.push_back (r->convertFrom0to1 (r->getValue()));
        return v;
    }

    // State equality on plain values with a relative tolerance of 1e-6
    // (~8 float ulps). JUCE's AudioParameterFloat stores f(norm) and reports
    // f^-1 of that, so on log/skewed ranges a value set through the parameter
    // API can land a few ulps away from the original (also true for JUCE's own
    // session restore). maxRel reports the largest relative difference seen.
    bool sameState (const std::vector<float>& a, const std::vector<float>& b, double* maxRel = nullptr)
    {
        if (a.size() != b.size()) return false;
        bool same = true; double worst = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            same &= std::abs ((double) a[i] - b[i]) <= 1.0e-6 * std::max (std::abs ((double) a[i]), std::abs ((double) b[i])) + 1.0e-9;
            worst = std::max (worst, std::abs ((double) a[i] - b[i]) / std::max (1.0e-30, (double) std::abs (a[i])));
        }
        if (maxRel != nullptr) *maxRel = worst;
        return same;
    }

    juce::Array<juce::File> presetFilesIn (const juce::File& dir)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, true, "*.vfpreset");
        files.sort();
        return files;
    }

    // --normalise-presets <dir>: rewrite sparse preset files as full state.
    int normalisePresets (const juce::File& dir)
    {
        for (auto& f : presetFilesIn (dir))
        {
            auto p = makeProcessor();
            auto& pm = p->getPresetManager();
            if (! pm.loadFromFile (f)) { std::printf ("FAILED %s: %s\n", f.getFullPathName().toRawUTF8(), pm.getLastError().toRawUTF8()); return 1; }
            vf::presets::PresetInfo info; juce::ValueTree values; juce::String err;
            PresetManager::readPresetFile (f, info, values, err);
            auto xml = pm.createPresetXml (info.name, info.category, info.author);
            if (! xml->writeTo (f)) return 1;
            // Verify: loading the full file reproduces the same parameter values.
            const auto before = paramValues (*p);
            auto q = makeProcessor();
            q->getPresetManager().loadFromFile (f);
            if (! sameState (paramValues (*q), before))
            {
                std::printf ("MISMATCH %s\n", f.getFileName().toRawUTF8());
                auto& a = p->getParameters(); auto& b = q->getParameters();
                for (int k = 0; k < a.size(); ++k)
                    if (! juce::exactlyEqual (a[k]->getValue(), b[k]->getValue()))
                    {
                        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (a[k]); auto* rb = dynamic_cast<juce::RangedAudioParameter*> (b[k]);
                        std::printf ("  %s norm %.9g vs %.9g plain %.9g vs %.9g\n", ra->paramID.toRawUTF8(), a[k]->getValue(), b[k]->getValue(),
                                     ra->convertFrom0to1 (a[k]->getValue()), rb->convertFrom0to1 (b[k]->getValue()));
                    }
                return 1;
            }
            std::printf ("normalised %s\n", f.getFileName().toRawUTF8());
        }
        return 0;
    }

    // --tune-presets <dir>: render each file with its category phrase, print levels.
    int tunePresets (const juce::File& dir)
    {
        for (auto& f : presetFilesIn (dir))
        {
            vf::presets::PresetInfo info; juce::ValueTree values; juce::String err;
            if (! PresetManager::readPresetFile (f, info, values, err)) { std::printf ("BAD %s\n", f.getFileName().toRawUTF8()); continue; }
            auto r = renderWithPreset ([&] (VSTForgeAudioProcessor& p) { return p.getPresetManager().loadFromFile (f); }, info.category);
            const auto st = analyse (r.audio);
            std::printf ("TUNE\t%s\t%s\t%.2f\t%.2f\t%.2f\n", info.name.toRawUTF8(), info.category.toRawUTF8(), db (st.peak), db (st.rms),
                         -db (r.minLimiterGain));
        }
        return 0;
    }

    std::vector<Note> plucks (double start)
    {
        return { { start, 0.12, 60, 1.0f }, { start + 0.0, 0.12, 67, 0.9f } };
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Never touch the real user preset folder from the tests.
    const auto presetTemp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vstforge-test-presets");
    presetTemp.deleteRecursively();
   #if JUCE_WINDOWS
    _putenv_s ("SYNSYN1000_PRESET_DIR", presetTemp.getFullPathName().toRawUTF8());
   #else
    setenv ("SYNSYN1000_PRESET_DIR", presetTemp.getFullPathName().toRawUTF8(), 1);
   #endif

    if (argc > 1 && juce::String (argv[1]) == "--dump-reference")
        return dumpReferences();
    if (argc > 2 && juce::String (argv[1]) == "--normalise-presets")
        return normalisePresets (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc > 2 && juce::String (argv[1]) == "--tune-presets")
        return tunePresets (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                      : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
    outDir.createDirectory();

    const auto names = vf::dsp::waveformNames();

    std::printf ("%-24s %9s %9s %8s %9s  %s\n", "render", "peak dBFS", "RMS dBFS", "NaN/Inf", "denormal", "result");

    // 1. Per-waveform, oscillator 1 only, default (open) filter: same as M1.
    for (int w = 0; w < names.size(); ++w)
    {
        auto p = makeProcessor();
        setParam (*p, id::waveform, (float) w);
        auto buf = render (*p, phrase (0.05), 2.2);
        check (("osc1 " + names[w]).toRawUTF8(), buf, ("osc_" + names[w].toLowerCase() + ".wav").toRawUTF8());
    }

    // 2. Three-oscillator detuned chord.
    {
        auto p = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*p, oscId (i, "on"), 1.0f); setParam (*p, oscId (i, "wave"), 1.0f); setParam (*p, oscId (i, "level"), 0.8f); }
        setParam (*p, oscId (1, "fine"), 7.0f);
        setParam (*p, oscId (2, "coarse"), -12.0f);
        setParam (*p, oscId (2, "fine"), -7.0f);
        setParam (*p, id::filterCutoff, 4000.0f);
        setParam (*p, id::filterRes, 0.25f);
        auto buf = render (*p, chord (0.05, 2.0, { 48, 55, 60, 64, 67 }), 2.6);
        check ("3-osc detuned chord", buf, "m2_3osc_chord.wav");
    }

    // 3. Filter sweep: LP, resonance 0.75, cutoff 80 Hz -> 12 kHz over 4 s.
    {
        auto p = makeProcessor();
        setParam (*p, oscId (1, "on"), 1.0f);
        setParam (*p, oscId (1, "wave"), 2.0f);
        setParam (*p, oscId (1, "coarse"), -12.0f);
        setParam (*p, id::filterRes, 0.75f);
        auto buf = render (*p, chord (0.0, 4.2, { 45, 52, 57, 60 }), 4.6, [&] (double t)
        {
            setParam (*p, id::filterCutoff, (float) (80.0 * std::pow (150.0, juce::jlimit (0.0, 1.0, t / 4.0))));
        });
        check ("filter sweep LP", buf, "m2_filter_sweep.wav");
        const double early = brightness (buf, 12000, 24000), late = brightness (buf, 168000, 24000);
        result ("filter sweep opens (brightness rises)", late > early * 4.0,
                juce::String (early, 4) + " -> " + juce::String (late, 4));
    }

    // 4. High-resonance stress: res 100 %, 3 squares at full level, +6 dB,
    //    8-note chord, random cutoff jumps every block, cycling filter types,
    //    max +/- envelope amount, fast filter envelope.
    {
        auto p = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*p, oscId (i, "on"), 1.0f); setParam (*p, oscId (i, "wave"), 2.0f); setParam (*p, oscId (i, "level"), 1.0f); }
        setParam (*p, oscId (1, "coarse"), 12.0f);
        setParam (*p, oscId (2, "coarse"), -24.0f);
        setParam (*p, id::filterRes, 1.0f);
        setParam (*p, id::gain, 6.0f);
        setParam (*p, id::fenvAttack, 0.001f);
        setParam (*p, id::fenvDecay, 0.05f);
        setParam (*p, id::fenvSustain, 0.5f);
        juce::Random rng (42);
        auto notes = chord (0.0, 3.0, { 24, 36, 43, 48, 55, 60, 79, 96 }, 1.0f);
        append (notes, chord (1.5, 1.0, { 30, 42, 66, 102 }, 1.0f));
        auto buf = render (*p, notes, 3.5, [&] (double t)
        {
            setParam (*p, id::filterCutoff, 20.0f * std::pow (1000.0f, rng.nextFloat()));
            setParam (*p, id::filterType, (float) ((int) (t / 0.5) % 3));
            setParam (*p, id::filterEnvAmt, ((int) (t / 0.25) % 2) ? 1.0f : -1.0f);
            setParam (*p, id::filterKeytrack, rng.nextFloat());
        });
        check ("high-res stress", buf, "m2_resonance_stress.wav");
    }

    // 5. Loudness safety: +6 dB, 16 voices, 3 saws at full level.
    {
        auto p = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*p, oscId (i, "on"), 1.0f); setParam (*p, oscId (i, "level"), 1.0f); }
        setParam (*p, id::gain, 6.0f);
        auto buf = render (*p, chord (0.0, 1.5, { 36, 40, 43, 47, 48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83 }, 1.0f), 2.0);
        check ("+6 dB 16-voice limiter", buf, nullptr);
    }

    // 6. Waveform switch while a note sustains: no click.
    {
        auto p = makeProcessor();
        setParam (*p, id::waveform, 0.0f);
        const double switchAt = 0.5;
        auto buf = render (*p, chord (0.0, 1.0, { 45 }, 1.0f), 1.0, [&] (double t)
        {
            setParam (*p, id::waveform, t >= switchAt ? 3.0f : 0.0f); // sine -> triangle
        });
        auto maxDiff = [&] (int a, int b)
        {
            float m = 0;
            for (int i = a + 1; i < b; ++i) m = std::max (m, std::abs (buf.getSample (0, i) - buf.getSample (0, i - 1)));
            return m;
        };
        const int sw = (int) (switchAt * sampleRate);
        const float steady = std::max (maxDiff (sw - 9600, sw - 960), maxDiff (sw + 960, sw + 9600));
        const float atSwitch = maxDiff (sw - 960, sw + 960);
        result ("waveform switch click-free (sine->tri)", atSwitch <= steady * 1.5f,
                "max step " + juce::String (atSwitch, 5) + " vs steady " + juce::String (steady, 5));
    }

    // 7. Release tail decays to exact silence.
    {
        auto p = makeProcessor();
        setParam (*p, oscId (1, "on"), 1.0f);
        setParam (*p, id::filterRes, 0.9f);
        setParam (*p, id::filterCutoff, 800.0f);
        auto buf = render (*p, phrase (0.0), 4.0);
        const auto tail = analyse (buf, buf.getNumSamples() - 24000, 24000);
        result ("release tail silent", tail.peak <= 0.0f, "peak " + juce::String (tail.peak));
    }

    // 8. State: round trip, M1 backward compatibility, editor size.
    {
        auto p = makeProcessor();
        setParam (*p, id::attack, 1.25f);
        setParam (*p, id::filterCutoff, 1234.0f);
        setParam (*p, oscId (2, "coarse"), -7.0f);
        p->setSavedEditorWidth (1234);
        juce::MemoryBlock mb; p->getStateInformation (mb);

        auto q = makeProcessor();
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool rt = std::abs (getParam (*q, id::attack) - 1.25f) < 1e-3f
                     && std::abs (getParam (*q, id::filterCutoff) - 1234.0f) < 1.0f
                     && std::abs (getParam (*q, oscId (2, "coarse")) + 7.0f) < 1e-3f;
        result ("state save/restore", rt, "attack " + juce::String (getParam (*q, id::attack), 3)
                + ", cutoff " + juce::String (getParam (*q, id::filterCutoff), 1));
        result ("editor size saved in state", q->getSavedEditorWidth() == 1234, "width " + juce::String (q->getSavedEditorWidth()));

        // A Milestone-1 session (only M1 parameters present).
        juce::XmlElement m1 ("VSTForgeSynth");
        auto add = [&] (const char* pid, double v) { auto* e = m1.createNewChildElement ("PARAM"); e->setAttribute ("id", pid); e->setAttribute ("value", v); };
        add ("osc1_wave", 3); add ("amp_attack", 0.5); add ("amp_decay", 0.2); add ("amp_sustain", 0.6);
        add ("amp_release", 0.7); add ("master_gain", -3.0);
        juce::MemoryBlock m1Data;
        juce::AudioProcessor::copyXmlToBinary (m1, m1Data);

        auto r = makeProcessor();
        setParam (*r, oscId (1, "on"), 1.0f);          // must be reset to default by the load
        setParam (*r, id::filterCutoff, 300.0f);       // must be reset to default by the load
        r->setStateInformation (m1Data.getData(), (int) m1Data.getSize());
        const bool compat = std::lround (getParam (*r, id::waveform)) == 3
                         && std::abs (getParam (*r, id::release) - 0.7f) < 1e-3f
                         && std::abs (getParam (*r, id::gain) + 3.0f) < 1e-3f
                         && getParam (*r, oscId (0, "on")) > 0.5f
                         && getParam (*r, oscId (1, "on")) < 0.5f
                         && getParam (*r, id::filterCutoff) > 19999.0f;
        result ("M1 session loads (missing params -> defaults)", compat,
                "osc1 wave " + juce::String (getParam (*r, id::waveform)) + ", osc2 on " + juce::String (getParam (*r, oscId (1, "on")))
                + ", cutoff " + juce::String (getParam (*r, id::filterCutoff), 0));

        // M1 sound is preserved: an M1 session renders like the M1 per-waveform render.
        auto buf = render (*r, phrase (0.05), 2.2);
        const auto st = analyse (buf);
        result ("M1 session renders (filter open, no clipping)", st.peak < 1.0f && db (st.rms) > -40.0f,
                "peak " + juce::String (db (st.peak), 2) + " dBFS");
    }

    // 9. Demo (~11 s): detuned 3-osc pad with LP sweep, filter-envelope plucks, band-pass sweep.
    {
        auto p = makeProcessor();
        auto setOsc = [&] (int i, bool on, int wave, float level, float coarse, float fine)
        {
            setParam (*p, oscId (i, "on"), on ? 1.0f : 0.0f); setParam (*p, oscId (i, "wave"), (float) wave);
            setParam (*p, oscId (i, "level"), level); setParam (*p, oscId (i, "coarse"), coarse); setParam (*p, oscId (i, "fine"), fine);
        };
        std::vector<Note> notes;
        append (notes, chord (0.05, 4.4, { 48, 55, 60, 63, 67 }, 0.85f));                 // pad, 0-5 s
        const int pluck[] = { 36, 48, 43, 51, 36, 55, 46, 50, 36, 48, 43, 58 };           // plucks, 5-8 s
        for (int i = 0; i < 12; ++i) notes.push_back ({ 5.0 + i * 0.25, 0.18, pluck[i], i % 4 == 0 ? 1.0f : 0.8f });
        append (notes, chord (8.2, 2.6, { 53, 60, 65, 69, 72 }, 0.85f));                  // BP sweep, 8-11.5 s

        setParam (*p, id::gain, -10.0f); // leave headroom so the limiter rarely engages
        float minLimiterGain = 1.0f;
        auto buf = render (*p, notes, 11.6, [&] (double t)
        {
            minLimiterGain = std::min (minLimiterGain, p->getLimiterGain());
            if (t < 5.0)
            {
                setOsc (0, true, 1, 0.8f, 0, 0); setOsc (1, true, 1, 0.8f, 0, 9); setOsc (2, true, 1, 0.7f, -12, -6);
                setParam (*p, id::filterType, 0.0f); setParam (*p, id::filterRes, 0.65f); setParam (*p, id::filterEnvAmt, 0.0f);
                setParam (*p, id::attack, 0.4f); setParam (*p, id::release, 0.5f); setParam (*p, id::sustain, 0.9f);
                const double u = t < 2.4 ? t / 2.4 : juce::jmax (0.0, 1.0 - (t - 2.4) / 2.4);
                setParam (*p, id::filterCutoff, (float) (120.0 * std::pow (80.0, u)));
            }
            else if (t < 8.0)
            {
                setOsc (0, true, 2, 0.8f, 0, 0); setOsc (1, true, 1, 0.7f, 12, 5); setOsc (2, false, 0, 0.7f, 0, 0);
                setParam (*p, id::filterCutoff, 180.0f); setParam (*p, id::filterRes, 0.7f);
                setParam (*p, id::filterEnvAmt, 0.75f); setParam (*p, id::filterKeytrack, 0.5f);
                setParam (*p, id::fenvAttack, 0.001f); setParam (*p, id::fenvDecay, 0.18f); setParam (*p, id::fenvSustain, 0.0f);
                setParam (*p, id::attack, 0.002f); setParam (*p, id::sustain, 0.6f); setParam (*p, id::release, 0.15f);
            }
            else
            {
                setOsc (0, true, 1, 0.8f, 0, 0); setOsc (1, true, 1, 0.8f, 0, -11); setOsc (2, true, 3, 0.8f, 12, 4);
                setParam (*p, id::filterType, 2.0f); setParam (*p, id::filterRes, 0.8f);
                setParam (*p, id::filterEnvAmt, 0.0f); setParam (*p, id::filterKeytrack, 0.0f);
                setParam (*p, id::attack, 0.15f); setParam (*p, id::sustain, 0.9f); setParam (*p, id::release, 0.8f);
                setParam (*p, id::filterCutoff, (float) (300.0 * std::pow (20.0, juce::jlimit (0.0, 1.0, (t - 8.2) / 2.6))));
            }
        });
        check ("m2_demo.wav (11.6 s)", buf, "m2_demo.wav");
        std::printf ("  demo: max limiter gain reduction %.2f dB\n", -db (minLimiterGain));
    }


    // ======================= M3: LFOs =======================================
    // 11. Old sessions render identically to v0.2.0 (sample-exact).
    for (auto& sc : compatScenarios())
    {
        const auto ref = readRaw (referenceDir().getChildFile (juce::String (sc.name) + ".f32"));
        const auto now = sc.render();
        float maxDiff = ref.getNumSamples() == now.getNumSamples() ? 0.0f : 1.0f;
        for (int c = 0; c < 2 && maxDiff < 1.0f; ++c)
            for (int i = 0; i < now.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (ref.getSample (c, i) - now.getSample (c, i)));
        result (("identical to reference: " + juce::String (sc.name)).toRawUTF8(), maxDiff <= 0.0f,
                "max |diff| " + juce::String (maxDiff, 9) + ", " + juce::String (now.getNumSamples()) + " samples");
    }

    std::printf ("%-24s %9s %9s %8s %9s  %s\n", "render", "peak dBFS", "RMS dBFS", "NaN/Inf", "denormal", "result");

    // 12. Vibrato: LFO 1 sine 5 Hz -> pitch, amount 5 % (= +/-0.6 st).
    {
        auto run = [&] (float amount)
        {
            auto p = makeProcessor();
            setParam (*p, id::waveform, 0.0f);
            setLfo (*p, 0, { 0, 5.0f, 3, amount });
            return render (*p, chord (0.0, 2.4, { 69 }, 1.0f), 2.5);
        };
        auto buf = run (0.05f);
        check ("vibrato (sine->pitch)", buf, "m3_vibrato.wav");
        const auto mod = rangeCents (pitchTrack (buf, 24000, 110000, 960));
        const auto dry = rangeCents (pitchTrack (run (0.0f), 24000, 110000, 960));
        result ("vibrato depth (expect ~120 cents p-p)", mod > 80.0 && mod < 160.0 && dry < 3.0,
                "pitch range " + juce::String (mod, 1) + " cents, without LFO " + juce::String (dry, 2));
    }

    // 13. Filter wobble: synced 1/8 square -> cutoff at 120 BPM (4 Hz).
    {
        auto p = makeProcessor();
        setParam (*p, oscId (1, "on"), 1.0f); setParam (*p, oscId (1, "coarse"), -12.0f);
        setParam (*p, id::filterCutoff, 350.0f); setParam (*p, id::filterRes, 0.6f);
        setLfo (*p, 0, { 4, 2.0f, 1, 0.6f, 0, true, divIndex ("1/8") });
        auto buf = render (*p, chord (0.0, 3.0, { 36, 43, 48 }), 3.2);
        check ("wobble (1/8 sq->cutoff)", buf, "m3_wobble_square.wav");
        const auto br = windowed (buf, 0, 144000, 3000, true);   // 62.5 ms windows: 4 per LFO cycle
        const double r = maxMinRatio (br), ac4 = autocorr (br, 4), ac2 = autocorr (br, 2);
        result ("wobble period = 1/8 @ 120 BPM (0.25 s)", r > 2.0 && ac4 > 0.5 && ac2 < 0.0,
                "brightness max/min " + juce::String (r, 2) + ", autocorr lag 0.25 s " + juce::String (ac4, 2)
                + ", lag 0.125 s " + juce::String (ac2, 2));

        auto q = makeProcessor();
        setParam (*q, oscId (1, "on"), 1.0f); setParam (*q, oscId (1, "coarse"), -12.0f);
        setParam (*q, id::filterCutoff, 250.0f); setParam (*q, id::filterRes, 0.7f);
        setLfo (*q, 1, { 3, 2.0f, 1, 0.7f, 1, true, divIndex ("1/8 T") });
        auto saw = render (*q, chord (0.0, 3.0, { 36, 43 }), 3.2);
        check ("wobble (1/8T saw->cutoff)", saw, "m3_wobble_saw.wav");
        const auto br2 = windowed (saw, 0, 144000, 1000, true); // 1/8T = 1/6 s = 8 windows
        result ("saw wobble period = 1/8T (0.167 s)", autocorr (br2, 8) > 0.5 && autocorr (br2, 4) < 0.0,
                "autocorr lag 1/6 s " + juce::String (autocorr (br2, 8), 2) + ", half " + juce::String (autocorr (br2, 4), 2));
    }

    // 14. Tremolo: LFO 1 sine 6 Hz -> amp, amount 100 %; plus delay/fade-in.
    {
        auto p = makeProcessor();
        setParam (*p, id::waveform, 1.0f);
        setLfo (*p, 0, { 0, 6.0f, 10, 1.0f });
        auto buf = render (*p, chord (0.0, 2.0, { 57, 64 }), 2.2);
        check ("tremolo (sine->amp)", buf, "m3_tremolo.wav");
        const auto env = windowed (buf, 24000, 96000, 480, false);
        result ("tremolo depth", maxMinRatio (env) > 8.0, "RMS max/min " + juce::String (maxMinRatio (env), 1));

        auto q = makeProcessor();
        setParam (*q, id::waveform, 1.0f);
        setParam (*q, id::sustain, 1.0f);   // flat amp envelope: only the LFO moves the level
        setLfo (*q, 0, { 0, 6.0f, 10, 1.0f, 0, false, 6, 1.0f, 1.5f });
        auto d = render (*q, chord (0.0, 3.0, { 57, 64 }), 3.0);
        const double early = maxMinRatio (windowed (d, 4800, 19200, 480, false));
        const double late = maxMinRatio (windowed (d, 96000, 136000, 480, false));
        result ("LFO delay/fade-in (1.5 s)", early < 1.6 && late > 8.0,
                "depth ratio first 0.3 s " + juce::String (early, 2) + ", after 2 s " + juce::String (late, 1));
    }

    // 15. Sample & hold -> cutoff, 8 Hz.
    {
        auto p = makeProcessor();
        setParam (*p, oscId (1, "on"), 1.0f); setParam (*p, oscId (1, "fine"), 8.0f);
        setParam (*p, id::filterCutoff, 900.0f); setParam (*p, id::filterRes, 0.5f);
        setLfo (*p, 0, { 5, 8.0f, 1, 0.8f });
        auto buf = render (*p, chord (0.0, 3.0, { 48, 55, 60 }), 3.2);
        check ("S&H 8 Hz -> cutoff", buf, "m3_sample_hold.wav");
        const auto br = windowed (buf, 3000, 144000, 6000, true); // one window per S&H step
        std::vector<double> sorted (br.begin(), br.end()); std::sort (sorted.begin(), sorted.end());
        int levels = 1;
        for (size_t i = 1; i < sorted.size(); ++i) if (sorted[i] > sorted[i - 1] * 1.08) ++levels;
        result ("S&H steps vary", coefVar (br) > 0.15 && levels >= 5,
                "brightness CV " + juce::String (coefVar (br), 2) + ", distinct levels " + juce::String (levels));
    }

    // 16. Mod wheel scales LFO 1 depth (vibrato only when the wheel is up).
    {
        auto run = [&] (int wheel)
        {
            auto p = makeProcessor();
            setParam (*p, id::waveform, 0.0f);
            setParam (*p, id::lfo1ModWheel, 1.0f);
            setLfo (*p, 0, { 0, 5.0f, 3, 0.05f });
            p->setUiModWheel (wheel);
            return rangeCents (pitchTrack (render (*p, chord (0.0, 2.0, { 69 }, 1.0f), 2.0), 24000, 90000, 960));
        };
        const double down = run (0), up = run (127);
        result ("mod wheel -> LFO 1 depth", down < 3.0 && up > 80.0,
                "wheel 0: " + juce::String (down, 2) + " cents, wheel 127: " + juce::String (up, 1) + " cents");
    }

    // 17. Stress: both LFOs at full depth/amount with resonance 100 %.
    {
        auto p = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*p, oscId (i, "on"), 1.0f); setParam (*p, oscId (i, "wave"), 2.0f); setParam (*p, oscId (i, "level"), 1.0f); }
        setParam (*p, oscId (2, "coarse"), -24.0f);
        setParam (*p, id::filterRes, 1.0f); setParam (*p, id::filterEnvAmt, 1.0f); setParam (*p, id::gain, 6.0f);
        setLfo (*p, 0, { 5, 40.0f, 1, 1.0f });           // S&H 40 Hz -> cutoff, +/-4 oct
        setLfo (*p, 1, { 4, 40.0f, 2, -1.0f, 1 });       // square 40 Hz -> resonance (free)
        auto buf = render (*p, chord (0.0, 3.0, { 24, 36, 48, 55, 60, 67, 84, 96 }, 1.0f), 3.4, [&] (double t)
        {
            setParam (*p, id::filterType, (float) ((int) (t / 0.6) % 3));
            setParam (*p, id::filterCutoff, t < 1.7 ? 20.0f : 20000.0f);
        });
        check ("stress: S&H cut + sq reso", buf, "m3_stress_filter.wav");

        auto q = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*q, oscId (i, "on"), 1.0f); setParam (*q, oscId (i, "level"), 1.0f); }
        setParam (*q, id::filterRes, 1.0f); setParam (*q, id::filterCutoff, 2000.0f); setParam (*q, id::gain, 6.0f);
        setLfo (*q, 0, { 5, 40.0f, 3, 1.0f });           // S&H 40 Hz -> pitch +/-12 st
        setLfo (*q, 1, { 4, 40.0f, 10, 1.0f, 1 });       // square 40 Hz -> amp
        auto b2 = render (*q, chord (0.0, 3.0, { 36, 48, 60, 72, 96, 108 }, 1.0f), 3.4);
        check ("stress: S&H pitch + sq amp", b2, "m3_stress_pitch_amp.wav");

        auto r = makeProcessor();
        setParam (*r, id::filterRes, 1.0f);
        for (int k = 0; k < 2; ++k) setLfo (*r, k, { k == 0 ? 2 : 1, 40.0f, k == 0 ? 4 : 8, 1.0f });
        setParam (*r, oscId (1, "on"), 1.0f);
        auto b3 = render (*r, chord (0.0, 2.0, { 40, 52, 64 }, 1.0f), 2.4);
        check ("stress: osc pitch + level", b3, nullptr);
    }

    // 18. Demo (~11.5 s): vibrato pad + slow filter LFO, synced wobble bass, S&H + tremolo.
    {
        auto p = makeProcessor();
        auto setOsc = [&] (int i, bool on, int wave, float level, float coarse, float fine)
        {
            setParam (*p, oscId (i, "on"), on ? 1.0f : 0.0f); setParam (*p, oscId (i, "wave"), (float) wave);
            setParam (*p, oscId (i, "level"), level); setParam (*p, oscId (i, "coarse"), coarse); setParam (*p, oscId (i, "fine"), fine);
        };
        setParam (*p, id::gain, -10.0f);
        std::vector<Note> notes;
        append (notes, chord (0.05, 3.2, { 50, 57, 62, 66, 69 }, 0.85f));
        for (int i = 0; i < 4; ++i) append (notes, chord (3.6 + i * 1.0, 0.95, { i % 2 ? 31 : 36, i % 2 ? 43 : 48 }, 0.95f));
        append (notes, chord (7.7, 3.2, { 57, 60, 64, 71, 76 }, 0.85f));
        float minLimiterGain = 1.0f;

        auto buf = render (*p, notes, 11.6, [&] (double t)
        {
            minLimiterGain = std::min (minLimiterGain, p->getLimiterGain());
            if (t < 3.5)
            {
                setOsc (0, true, 1, 0.8f, 0, 0); setOsc (1, true, 1, 0.8f, 0, 8); setOsc (2, true, 3, 0.6f, 12, -4);
                setParam (*p, id::filterCutoff, 900.0f); setParam (*p, id::filterRes, 0.45f);
                setParam (*p, id::attack, 0.3f); setParam (*p, id::sustain, 0.9f); setParam (*p, id::release, 0.4f);
                setLfo (*p, 0, { 0, 5.5f, 3, 0.03f, 0, false, 6, 1.0f, 0.8f });     // delayed vibrato
                setLfo (*p, 1, { 1, 0.35f, 1, 0.55f, 1 });                            // slow free triangle -> cutoff
            }
            else if (t < 7.6)
            {
                setOsc (0, true, 1, 0.9f, 0, 0); setOsc (1, true, 2, 0.8f, -12, 0); setOsc (2, true, 1, 0.5f, 0, 12);
                setParam (*p, id::filterCutoff, 260.0f); setParam (*p, id::filterRes, 0.72f);
                setParam (*p, id::attack, 0.005f); setParam (*p, id::sustain, 0.9f); setParam (*p, id::release, 0.1f);
                setLfo (*p, 0, { 0, 5.5f, 0, 0.0f });
                setLfo (*p, 1, { 0, 2.0f, 1, 0.75f, 0, true, divIndex (t < 5.6 ? "1/8" : "1/16 T") }); // synced wobble
            }
            else
            {
                setOsc (0, true, 1, 0.8f, 0, 0); setOsc (1, true, 2, 0.6f, 0, -6); setOsc (2, false, 0, 0.5f, 0, 0);
                setParam (*p, id::filterCutoff, 1100.0f); setParam (*p, id::filterRes, 0.8f);
                setParam (*p, id::attack, 0.05f); setParam (*p, id::release, 0.6f);
                setLfo (*p, 0, { 0, 6.0f, 10, 0.7f, 1 });                             // tremolo
                setLfo (*p, 1, { 5, 8.0f, 1, 0.7f, 1 });                              // S&H -> cutoff
            }
        });
        check ("m3_demo.wav (11.6 s)", buf, "m3_demo.wav");
        std::printf ("  demo: max limiter gain reduction %.2f dB\n", -db (minLimiterGain));
    }


    // ======================= M4: effects ====================================
    std::printf ("%-24s %9s %9s %8s %9s  %s\n", "render", "peak dBFS", "RMS dBFS", "NaN/Inf", "denormal", "result");
    auto fxSynth = [] (VSTForgeAudioProcessor& p)
    {
        setParam (p, oscId (1, "on"), 1.0f); setParam (p, oscId (1, "fine"), 6.0f);
        setParam (p, id::filterCutoff, 3500.0f); setParam (p, id::filterRes, 0.2f);
    };

    // 20. Chorus only.
    {
        auto p = makeProcessor(); fxSynth (*p);
        FxSetup f; f.chorus = true; setFx (*p, f);
        auto buf = render (*p, chord (0.0, 2.0, { 48, 55, 60, 64 }), 2.6);
        check ("chorus", buf, "m4_chorus.wav");
        result ("chorus stereo width > 0", stereoWidth (buf) > 0.1, "side/mid " + juce::String (stereoWidth (buf), 3));
    }

    // 21. Delay only (stereo, then ping-pong), echo timing.
    {
        auto run = [&] (bool pingPong, bool on)
        {
            auto p = makeProcessor(); fxSynth (*p);
            FxSetup f; f.delay = on; f.pingPong = pingPong; setFx (*p, f);
            return render (*p, plucks (0.05), 2.5);
        };
        auto buf = run (false, true);
        check ("delay (stereo)", buf, "m4_delay.wav");
        const double echo = analyse (buf, 33600, 4800).rms, dry = analyse (run (false, false), 33600, 4800).rms;
        result ("delay echo at +300 ms", db (echo) > db (dry) + 30.0f,
                "RMS at 0.70-0.80 s: " + juce::String (db (echo), 1) + " dB vs dry " + juce::String (db (dry), 1) + " dB");
        auto pp = run (true, true);
        check ("delay (ping-pong)", pp, "m4_delay_pingpong.wav");
        result ("ping-pong stereo width > 0", stereoWidth (pp, 24000, 72000) > 0.5,
                "side/mid " + juce::String (stereoWidth (pp, 24000, 72000), 3));
    }

    // 22. Reverb only + tail behaviour.
    {
        auto p = makeProcessor(); fxSynth (*p);
        FxSetup f; f.reverb = true; f.size = 0.7f; f.reverbMix = 0.35f; setFx (*p, f);
        auto buf = render (*p, plucks (0.05), 12.0);
        check ("reverb", buf, "m4_reverb.wav");
        const auto tail = analyse (buf, (int) (0.8 * renderRate), (int) (0.5 * renderRate));   // amp release ends ~0.5 s
        const double below = timeBelow (buf, -90.0f, (int) (0.5 * renderRate));
        const auto last = analyse (buf, buf.getNumSamples() - 48000, 48000);
        result ("reverb tail continues after note-off", db (tail.rms) > -60.0f, "RMS 0.8-1.3 s " + juce::String (db (tail.rms), 1) + " dBFS");
        result ("reverb tail < -90 dBFS, no denormals", below < 8.0 && last.denormals == 0 && tail.denormals == 0,
                "below -90 dBFS from " + juce::String (below, 2) + " s, last-second peak " + juce::String (db (last.peak), 1) + " dBFS");
        result ("reverb stereo width > 0", stereoWidth (buf, 24000, 96000) > 0.1, "side/mid " + juce::String (stereoWidth (buf, 24000, 96000), 3));

        // reset() clears all effect buffers.
        p->reset();
        auto after = render (*p, {}, 0.5);
        result ("reset() clears effect state", analyse (after).peak <= 0.0f, "peak after reset " + juce::String (analyse (after).peak));
    }

    // 23. All three together.
    {
        auto p = makeProcessor(); fxSynth (*p);
        FxSetup f; f.chorus = f.delay = f.reverb = true; f.pingPong = true; setFx (*p, f);
        auto notes = chord (0.0, 1.5, { 48, 55, 60, 64 });
        append (notes, phrase (1.6));
        auto buf = render (*p, notes, 5.0);
        check ("chorus+delay+reverb", buf, "m4_all_fx.wav");
        result ("all-fx stereo width > 0", stereoWidth (buf) > 0.2, "side/mid " + juce::String (stereoWidth (buf), 3));
    }

    // 24. Delay stress: 95 % feedback, ping-pong, no damping, short + swept time, hot input.
    {
        auto p = makeProcessor();
        for (int i = 0; i < 3; ++i) { setParam (*p, oscId (i, "on"), 1.0f); setParam (*p, oscId (i, "wave"), 2.0f); setParam (*p, oscId (i, "level"), 1.0f); }
        setParam (*p, id::gain, 6.0f); setParam (*p, id::filterRes, 0.9f); setParam (*p, id::filterCutoff, 2000.0f);
        FxSetup f; f.delay = f.reverb = true; f.feedback = 0.95f; f.pingPong = true; f.highCut = 20000.0f; f.delayMix = 1.0f;
        f.delayMs = 10.0f; f.size = 1.0f; f.reverbMix = 1.0f; setFx (*p, f);
        std::vector<float> limGain;   // limiter gain per block, to estimate the pre-limiter level
        auto buf = render (*p, chord (0.0, 2.0, { 36, 48, 55, 60, 67, 72, 79, 84 }, 1.0f), 30.0, [&] (double t)
        {
            limGain.push_back (p->getLimiterGain());
            setParam (*p, id::delayTime, t < 3.0 ? (float) (10.0 + 600.0 * t / 3.0) : 610.0f);
        });
        auto preLimiterDb = [&] (double t0)
        {
            const int b0 = (int) (t0 * renderRate) / blockSize;
            double g = 0; for (int b = b0; b < b0 + 187; ++b) g += limGain[(size_t) juce::jmin ((int) limGain.size() - 1, b + 1)];
            g /= 187.0;
            return db (analyse (buf, b0 * blockSize, 187 * blockSize).rms / juce::jmax (g, 1.0e-9));
        };
        check ("delay stress fb 95 %", buf, "m4_delay_stress.wav");
        const float pre4 = preLimiterDb (4.0), pre29 = preLimiterDb (28.9);
        result ("feedback decays (no runaway)", pre29 < pre4 - 10.0f,
                "pre-limiter RMS at 4 s " + juce::String (pre4, 1) + " dB -> 29 s " + juce::String (pre29, 1) + " dB");
    }

    // 25. Smooth bypass: switching reverb/delay/chorus off mid-note does not click.
    {
        auto p = makeProcessor();
        setParam (*p, id::waveform, 0.0f);
        FxSetup f; f.chorus = f.delay = f.reverb = true; f.reverbMix = 0.5f; setFx (*p, f);
        const double offAt = 1.0;
        auto buf = render (*p, chord (0.0, 2.0, { 57 }, 1.0f), 2.0, [&] (double t)
        {
            const float on = t < offAt ? 1.0f : 0.0f;
            setParam (*p, id::chorusOn, on); setParam (*p, id::delayOn, on); setParam (*p, id::reverbOn, on);
        });
        float steady = 0, atSwitch = 0;
        const int sw = (int) (offAt * renderRate);
        for (int i = sw - 24000; i < sw + 24000; ++i)
        {
            const float d = std::abs (buf.getSample (0, i) - buf.getSample (0, i - 1));
            if (std::abs (i - sw) < 2400) atSwitch = std::max (atSwitch, d); else steady = std::max (steady, d);
        }
        result ("effects bypass ramp click-free", atSwitch <= steady * 1.5f,
                "max step " + juce::String (atSwitch, 5) + " vs steady " + juce::String (steady, 5));
    }

    // 26. Mono output bus: effects fold back to mono.
    {
        auto p = std::make_unique<VSTForgeAudioProcessor>();
        p->setPlayConfigDetails (0, 1, renderRate, blockSize);
        p->prepareToPlay (renderRate, blockSize);
        FxSetup f; f.chorus = f.delay = f.reverb = true; setFx (*p, f);
        juce::AudioBuffer<float> mono (1, blockSize), out (2, (int) renderRate * 2);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += blockSize)
        {
            midi.clear();
            if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            if (pos == 9600) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            p->processBlock (mono, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, mono, 0, 0, blockSize);
        }
        check ("mono bus, all fx", out, nullptr);
    }

    // 27. Sample-rate changes: same processor re-prepared at 44.1k / 96k / 48k.
    {
        auto p = std::make_unique<VSTForgeAudioProcessor>();
        fxSynth (*p);
        FxSetup f; f.chorus = f.delay = f.reverb = true; f.feedback = 0.0f; f.delayMs = 250.0f; f.delayMix = 0.5f; setFx (*p, f);
        for (double sr : { 44100.0, 96000.0, 48000.0 })
        {
            renderRate = sr;
            p->setPlayConfigDetails (0, 2, sr, blockSize);
            p->prepareToPlay (sr, blockSize);
            p->reset();
            auto buf = render (*p, plucks (0.0), 3.0);
            const juce::String name = "all fx @ " + juce::String (sr / 1000.0, 1) + " kHz";
            check (name.toRawUTF8(), buf, nullptr);
            // Echo timing: delay-only wet-minus-dry difference starts at 250 ms.
            auto delayOnly = [&] (bool on)
            {
                auto q = makeProcessor();
                FxSetup g; g.delay = on; g.feedback = 0.0f; g.delayMs = 250.0f; g.delayMix = 0.5f; setFx (*q, g);
                return render (*q, plucks (0.0), 0.6);
            };
            auto wet = delayOnly (true), dryOnly = delayOnly (false);
            int onset = -1;
            for (int i = 0; i < wet.getNumSamples() && onset < 0; ++i)
                if (std::abs (wet.getSample (0, i) - dryOnly.getSample (0, i)) > 1.0e-5f) onset = i;
            const double onsetMs = onset * 1000.0 / sr;
            result (("delay time correct @ " + juce::String (sr / 1000.0, 1) + " kHz").toRawUTF8(), std::abs (onsetMs - 250.0) < 1.0,
                    "echo onset at " + juce::String (onsetMs, 2) + " ms");
        }
        renderRate = sampleRate;
    }

    // 28. Demo (~12 s): chorus pad, synced ping-pong plucks, reverb lead with everything on.
    {
        auto p = makeProcessor();
        setParam (*p, id::gain, -11.0f);
        std::vector<Note> notes;
        append (notes, chord (0.05, 3.4, { 50, 57, 62, 65, 69 }, 0.8f));
        const int seq[] = { 62, 65, 69, 72, 74, 72, 69, 65 };
        for (int i = 0; i < 8; ++i) notes.push_back ({ 4.0 + i * 0.25, 0.1, seq[i], 0.95f });
        append (notes, chord (7.0, 0.3, { 38, 50 }, 1.0f));
        const int lead[] = { 74, 77, 81, 79, 77, 74 };
        for (int i = 0; i < 6; ++i) notes.push_back ({ 7.4 + i * 0.4, i == 5 ? 1.0 : 0.33, lead[i], 0.9f });
        float minLimiterGain = 1.0f;
        auto buf = render (*p, notes, 12.0, [&] (double t)
        {
            minLimiterGain = std::min (minLimiterGain, p->getLimiterGain());
            FxSetup f;
            if (t < 3.8)
            {
                setParam (*p, oscId (1, "on"), 1.0f); setParam (*p, oscId (1, "fine"), 7.0f);
                setParam (*p, id::attack, 0.4f); setParam (*p, id::release, 0.6f); setParam (*p, id::filterCutoff, 2500.0f);
                f.chorus = true; f.chorusDepth = 0.7f; f.reverb = true; f.reverbMix = 0.2f;
            }
            else if (t < 7.0)
            {
                setParam (*p, oscId (1, "on"), 0.0f); setParam (*p, id::waveform, 2.0f);
                setParam (*p, id::attack, 0.002f); setParam (*p, id::release, 0.12f); setParam (*p, id::sustain, 0.3f);
                setParam (*p, id::filterCutoff, 700.0f); setParam (*p, id::filterEnvAmt, 0.6f); setParam (*p, id::fenvDecay, 0.15f);
                setParam (*p, id::filterRes, 0.5f);
                f.delay = true; f.sync = true; f.div = 10; f.pingPong = true; f.feedback = 0.55f; f.delayMix = 0.4f;
                f.reverb = true; f.reverbMix = 0.15f;
            }
            else
            {
                setParam (*p, id::waveform, 1.0f); setParam (*p, oscId (1, "on"), 1.0f); setParam (*p, oscId (1, "fine"), -8.0f);
                setParam (*p, id::attack, 0.02f); setParam (*p, id::sustain, 0.8f); setParam (*p, id::release, 0.8f);
                setParam (*p, id::filterCutoff, 1800.0f); setParam (*p, id::filterEnvAmt, 0.3f);
                setLfo (*p, 0, { 0, 5.5f, 3, 0.02f, 0, false, 6, 1.0f, 0.4f });
                f.chorus = true; f.delay = true; f.sync = true; f.div = 9; f.pingPong = true; f.feedback = 0.4f; f.delayMix = 0.3f;
                f.reverb = true; f.size = 0.85f; f.reverbMix = 0.35f; f.preDelay = 40.0f;
            }
            setFx (*p, f);
        });
        check ("m4_demo.wav (12 s)", buf, "m4_demo.wav");
        std::printf ("  demo: max limiter gain reduction %.2f dB, stereo width %.3f\n", -db (minLimiterGain), stereoWidth (buf));
    }

    // ======================= M5: presets ====================================
    std::printf ("\n-- M5: presets --\n");
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();

        // 30. Factory bank: embedded, 24 presets, 4 per category, programs exposed.
        std::map<juce::String, int> perCat;
        for (auto& info : pm.getPresets()) if (info.isFactory && ! info.isInit) ++perCat[info.category];
        bool catsOk = perCat.size() == 6;
        for (auto& c : { "Bass", "Lead", "Pad", "Pluck", "Keys", "FX" }) catsOk &= perCat[c] == 4;
        result ("factory bank embedded (24, 4 per category)", pm.getNumFactoryPresets() == 24 && catsOk,
                juce::String (pm.getNumFactoryPresets()) + " presets, " + juce::String ((int) perCat.size()) + " categories");
        result ("host programs = Init + factory", p->getNumPrograms() == 25 && p->getProgramName (0) == "Init"
                    && p->getProgramName (1).isNotEmpty() && p->getProgramName (25).isEmpty(),
                juce::String (p->getNumPrograms()) + " programs, #1 \"" + p->getProgramName (1) + "\", #24 \"" + p->getProgramName (24) + "\"");

        // 31. Program switching via the host API: state matches a direct load, current program reported.
        bool progOk = true; juce::String bad;
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            auto ref = makeProcessor();
            ref->getPresetManager().loadPreset (i);
            if (p->getCurrentProgram() != i || ! sameState (paramValues (*p), paramValues (*ref)) || pm.isModified()
                || pm.getCurrentName() != p->getProgramName (i))
            { progOk = false; bad << i << " "; }
        }
        p->setCurrentProgram (0);
        std::vector<float> defaults;
        for (auto* prm : p->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) defaults.push_back (r->convertFrom0to1 (r->getDefaultValue()));
        const bool initDefaults = sameState (paramValues (*p), defaults);
        result ("setCurrentProgram 0..24 (state, name, index)", progOk, progOk ? juce::String ("all 25 match a direct load") : "bad: " + bad);
        result ("Init program = all defaults", initDefaults, "");
        p->setCurrentProgram (-1); p->setCurrentProgram (99);   // out of range: ignored
        result ("out-of-range program ignored", p->getCurrentProgram() == 0, "current " + juce::String (p->getCurrentProgram()));
    }

    // 32. Save -> load round trip (temp user folder), human-readable file, modified flag.
    const auto userRoot = presetTemp.getChildFile ("user");
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();
        pm.setUserRoot (userRoot);
        pm.loadFactory (5);
        setParam (*p, id::filterCutoff, 1234.5f); setParam (*p, vf::params::lfoId (1, "target"), 7.0f); setParam (*p, id::reverbMix, 0.42f);
        setParam (*p, oscId (2, "coarse"), -7.0f); setParam (*p, id::delayDiv, 13.0f);
        const bool modifiedAfterEdit = pm.isModified();
        const auto saved = paramValues (*p);
        const auto r = pm.saveAs ("Round Trip", "Lead", "Tester", false);
        const auto file = userRoot.getChildFile ("Lead/Round Trip.vfpreset");
        const auto text = file.loadFileAsString();
        result ("save as -> <root>/Lead/Round Trip.vfpreset", r == PresetManager::Result::ok && file.existsAsFile()
                    && text.contains ("name=\"Round Trip\"") && text.contains ("category=\"Lead\"") && text.contains ("author=\"Tester\"")
                    && text.contains ("version=\"" JUCE_STRINGIFY (VSTFORGE_VERSION_STRING) "\"") && text.contains ("<PARAM id=\"filter_cutoff\""),
                juce::String (file.getSize()) + " bytes, " + juce::String (text.length() - text.removeCharacters ("\n").length()) + " lines");
        result ("modified flag (edit -> on, save -> off)", modifiedAfterEdit && ! pm.isModified() && pm.isCurrentUser(),
                "after edit " + juce::String (modifiedAfterEdit ? "on" : "off") + ", after save " + (pm.isModified() ? "on" : "off"));
        pm.loadInit();
        const bool changed = ! sameState (paramValues (*p), saved);
        const int idx = pm.getCurrentIndex();
        int userIdx = -1;
        for (size_t i = 0; i < pm.getPresets().size(); ++i) if (pm.getPresets()[i].name == "Round Trip") userIdx = (int) i;
        const bool loaded = pm.loadPreset (userIdx);
        double rel = 0.0;
        const bool same = sameState (paramValues (*p), saved, &rel);
        int exact = 0;
        { const auto now = paramValues (*p); for (size_t i = 0; i < now.size(); ++i) exact += juce::exactlyEqual (now[i], saved[i]) ? 1 : 0; }
        result ("round trip: load -> identical state", changed && loaded && same && idx == 0,
                juce::String (exact) + "/" + juce::String ((int) saved.size()) + " bit-exact, max rel diff " + juce::String (rel, 9));
        // Saving the loaded preset again yields a byte-identical file (fixed point).
        const auto text1 = file.loadFileAsString();
        pm.saveCurrent();
        result ("round trip: re-save is byte-identical", text1 == file.loadFileAsString(), juce::String (file.getSize()) + " bytes");

        // A second processor (fresh instance) sees the same preset with identical values.
        auto q = makeProcessor();
        q->getPresetManager().setUserRoot (userRoot);
        result ("round trip: fresh instance loads file identically", q->getPresetManager().loadFromFile (file) && sameState (paramValues (*q), saved), "");

        // 33. Plugin state stores the current preset name and restores it.
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto r2 = makeProcessor(); r2->getPresetManager().setUserRoot (userRoot);
        r2->setStateInformation (mb.getData(), (int) mb.getSize());
        auto& pm2 = r2->getPresetManager();
        result ("session restores preset name (user)", pm2.getCurrentName() == "Round Trip" && pm2.getCurrentCategory() == "Lead"
                    && pm2.isCurrentUser() && ! pm2.isModified(), pm2.getCurrentName() + " / " + pm2.getCurrentCategory());
        p->setCurrentProgram (7);
        setParam (*p, id::filterRes, 0.9f);   // modified
        p->getStateInformation (mb);
        auto r3 = makeProcessor(); r3->setStateInformation (mb.getData(), (int) mb.getSize());
        result ("session restores factory program + modified flag", r3->getCurrentProgram() == 7 && r3->getPresetManager().isModified()
                    && r3->getPresetManager().getCurrentName() == p->getProgramName (7),
                "program " + juce::String (r3->getCurrentProgram()) + " \"" + r3->getPresetManager().getCurrentName() + "\", modified "
                    + (r3->getPresetManager().isModified() ? "yes" : "no"));
        // An M4 session (no preset info) shows "Init" + modified (differs from defaults).
        auto r4 = loadSession (referenceDir().getChildFile ("m4_session.state"));
        result ("M4 session: name Init, modified", r4->getPresetManager().getCurrentName() == "Init" && r4->getPresetManager().isModified(), "");

        // 34. File operations: overwrite protection, rename, delete, read-only factory.
        auto& m = pm;
        const auto again = m.saveAs ("Round Trip", "Lead", "Tester", false);
        const auto over  = m.saveAs ("Round Trip", "Lead", "Tester", true);
        result ("save as existing name: needs overwrite", again == PresetManager::Result::exists && over == PresetManager::Result::ok, "");
        const auto badName = m.saveAs ("  ", "Pad", "x", false);
        const auto badName2 = m.saveAs ("a/b", "Pad", "x", false);
        result ("invalid names rejected", badName == PresetManager::Result::invalidName && badName2 == PresetManager::Result::invalidName, "");
        const int cur = m.getCurrentIndex();
        const auto ren = m.renamePreset (cur, "Renamed Lead");
        const auto renamed = userRoot.getChildFile ("Lead/Renamed Lead.vfpreset");
        result ("rename user preset (file + name)", ren == PresetManager::Result::ok && renamed.existsAsFile() && ! file.exists()
                    && renamed.loadFileAsString().contains ("name=\"Renamed Lead\"") && m.getCurrentName() == "Renamed Lead",
                renamed.getFileName());
        const auto renFactory = m.renamePreset (3, "Nope");
        const auto delFactory = m.deletePreset (3);
        result ("factory presets read-only (rename/delete/save)", renFactory == PresetManager::Result::readOnly
                    && delFactory == PresetManager::Result::readOnly && (m.loadFactory (2), m.saveCurrent() == PresetManager::Result::readOnly), "");
        m.saveAs ("Pad Two", "Pad", "Tester", false);
        const int before = (int) m.getPresets().size();
        int delIdx = -1;
        for (size_t i = 0; i < m.getPresets().size(); ++i) if (m.getPresets()[i].name == "Renamed Lead") delIdx = (int) i;
        const auto del = m.deletePreset (delIdx);
        result ("delete user preset", del == PresetManager::Result::ok && ! renamed.exists() && (int) m.getPresets().size() == before - 1,
                juce::String (before) + " -> " + juce::String ((int) m.getPresets().size()) + " entries");
        result ("category filter (Pad: 4 factory + 1 user, User: 1)", m.getIndicesInCategory ("Pad").size() == 5
                    && m.getIndicesInCategory ("User").size() == 1 && m.getIndicesInCategory ("All").size() == 26, "");
        // Prev/next wrap within a category.
        m.loadFactory (0);
        const auto firstName = m.getCurrentName();
        bool stepOk = true;
        for (int i = 0; i < 4; ++i) stepOk &= m.step (+1, "Bass");
        result ("prev/next wrap in category", stepOk && m.getCurrentName() == firstName && (m.step (-1, "Bass"), m.getCurrentCategory() == "Bass"), "");
    }

    // 35. Corrupt / missing files: no crash, state untouched, reported.
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();
        const auto dir = presetTemp.getChildFile ("corrupt");
        dir.getChildFile ("Pad").createDirectory();
        dir.getChildFile ("Lead").createDirectory();
        dir.getChildFile ("Pad/Garbage.vfpreset").replaceWithText ("this is \x01 not xml <<<");
        dir.getChildFile ("Pad/Truncated.vfpreset").replaceWithText ("<?xml version=\"1.0\"?><VSTForgePreset name=\"T\" category=\"Pad\"><VSTForgeSynth><PARAM id=");
        dir.getChildFile ("Pad/Empty.vfpreset").replaceWithText ("");
        dir.getChildFile ("Pad/WrongRoot.vfpreset").replaceWithText ("<VSTForgeSynth><PARAM id=\"master_gain\" value=\"0\"/></VSTForgeSynth>");
        dir.getChildFile ("Pad/Future.vfpreset").replaceWithText ("<VSTForgePreset format=\"99\" name=\"F\" category=\"Pad\"><VSTForgeSynth/></VSTForgePreset>");
        dir.getChildFile ("Lead/Weird.vfpreset").replaceWithText (
            "<VSTForgePreset name=\"Weird Values\" category=\"NoSuchCategory\"><VSTForgeSynth>"
            "<PARAM id=\"filter_cutoff\" value=\"nan\"/><PARAM id=\"amp_attack\" value=\"1e30\"/><PARAM id=\"osc1_wave\" value=\"-5\"/>"
            "<PARAM id=\"delay_time\" value=\"-100\"/><PARAM id=\"unknown_param\" value=\"3\"/><PARAM id=\"master_gain\" value=\"-12\"/>"
            "<PARAM value=\"1\"/></VSTForgeSynth></VSTForgePreset>");
        pm.setUserRoot (dir);
        p->setCurrentProgram (3);
        const auto stateBefore = paramValues (*p);
        result ("corrupt files skipped on scan (5 unreadable)", pm.getNumUnreadableFiles() == 5 && pm.getIndicesInCategory ("User").size() == 1,
                juce::String (pm.getNumUnreadableFiles()) + " unreadable, " + juce::String ((int) pm.getIndicesInCategory ("User").size()) + " usable");
        bool allFail = true;
        for (auto n : { "Garbage", "Truncated", "Empty", "WrongRoot", "Future" })
            allFail &= ! pm.loadFromFile (dir.getChildFile ("Pad").getChildFile (juce::String (n) + ".vfpreset"));
        const bool missing = ! pm.loadFromFile (dir.getChildFile ("Pad/DoesNotExist.vfpreset"));
        result ("corrupt/missing file load fails, state untouched", allFail && missing && paramValues (*p) == stateBefore
                    && p->getCurrentProgram() == 3, "last error: " + pm.getLastError());
        // Weird values: clamped / defaults, unknown IDs ignored, category -> User.
        int weird = -1;
        for (size_t i = 0; i < pm.getPresets().size(); ++i) if (pm.getPresets()[i].name == "Weird Values") weird = (int) i;
        const bool wl = weird >= 0 && pm.loadPreset (weird);
        const bool clamped = wl && getParam (*p, id::filterCutoff) > 19999.0f && std::abs (getParam (*p, id::attack) - 5.0f) < 1e-3f
                          && getParam (*p, id::waveform) < 0.5f && std::abs (getParam (*p, id::delayTime) - 1.0f) < 1e-3f
                          && std::abs (getParam (*p, id::gain) + 12.0f) < 1e-3f && pm.getCurrentCategory() == "User";
        result ("bad values clamped / defaulted, unknown IDs ignored", clamped,
                "cutoff " + juce::String (getParam (*p, id::filterCutoff), 0) + " (NaN->default), attack " + juce::String (getParam (*p, id::attack), 2)
                    + " (clamped), delay " + juce::String (getParam (*p, id::delayTime), 1) + " ms");
        // File removed after the scan: load fails cleanly and the list is refreshed.
        dir.getChildFile ("Lead/Weird.vfpreset").deleteFile();
        const auto st2 = paramValues (*p);
        const bool goneFails = ! pm.loadPreset (weird) && paramValues (*p) == st2;
        pm.rescanUserPresets();
        result ("deleted-behind-our-back file: clean failure", goneFails && pm.getIndicesInCategory ("User").empty(), pm.getLastError());
        pm.setUserRoot (presetTemp.getChildFile ("does/not/exist"));
        result ("missing user folder: empty user list", pm.getIndicesInCategory ("User").empty() && pm.getNumFactoryPresets() == 24, "");
        juce::MemoryBlock junk; junk.append ("garbage", 7);
        p->setStateInformation (junk.getData(), (int) junk.getSize());
        result ("garbage plug-in state ignored", paramValues (*p) == st2, "");
    }

    // 36. Glitch-free preset switching while notes sound.
    {
        auto p = makeProcessor();
        p->setCurrentProgram (1 + 8);   // a pad
        const auto padName = p->getProgramName (9);
        auto notes = chord (0.0, 3.0, { 48, 55, 60, 64 }, 0.9f);  // held through the switch (note-offs after the switch)
        const double tSwitch = 1.0;
        auto buf = render (*p, notes, 2.0, [&] (double t)
        {
            if (std::abs (t - tSwitch) < 0.5 * blockSize / renderRate) p->setCurrentProgram (1 + 12);   // a pluck
        });
        const int s0 = (int) (tSwitch * renderRate);
        float maxStepSwitch = 0, maxStepSteady = 0, peakBefore = 0;
        for (int i = s0 - 4800; i < s0; ++i)
            for (int c = 0; c < 2; ++c)
            {
                maxStepSteady = std::max (maxStepSteady, std::abs (buf.getSample (c, i) - buf.getSample (c, i - 1)));
                peakBefore = std::max (peakBefore, std::abs (buf.getSample (c, i)));
            }
        for (int i = s0; i < s0 + 1200; ++i)
            for (int c = 0; c < 2; ++c)
                maxStepSwitch = std::max (maxStepSwitch, std::abs (buf.getSample (c, i) - buf.getSample (c, i - 1)));
        // The switch lands on the next block boundary (<= 1 block late); the fade lasts 10 ms.
        const auto after = analyse (buf, s0 + (int) (0.02 * renderRate), (int) (0.9 * renderRate));
        result ("preset switch fades (no click)", maxStepSwitch <= maxStepSteady * 1.05f && p->getPresetFadeCount() == 1,
                "max step at switch " + juce::String (maxStepSwitch, 4) + " vs steady " + juce::String (maxStepSteady, 4)
                    + " (" + padName + " -> " + p->getProgramName (13) + ")");
        result ("old voices + tails silenced (no stuck notes)", db (after.peak) < -120.0f && p->getNumActiveVoices() == 0,
                "peak after fade " + juce::String (db (after.peak), 1) + " dBFS, active voices " + juce::String (p->getNumActiveVoices()));

        // Note-on arriving during the fade is deferred, not lost; its note-off is honoured.
        auto q = makeProcessor();
        q->setCurrentProgram (9);
        std::vector<Note> n2 = chord (0.0, 1.5, { 48, 55, 60 }, 0.9f);
        n2.push_back ({ 1.003, 0.4, 72, 1.0f });
        float peakNew = 0;
        auto b2 = render (*q, n2, 3.0, [&] (double t)
        {
            if (std::abs (t - 1.0) < 0.5 * blockSize / renderRate) q->setCurrentProgram (13);
        });
        peakNew = analyse (b2, (int) (1.02 * renderRate), (int) (0.3 * renderRate)).peak;
        result ("note-on during fade is played after it", db (peakNew) > -30.0f && q->getNumActiveVoices() == 0,
                "new note peak " + juce::String (db (peakNew), 1) + " dBFS, voices at end " + juce::String (q->getNumActiveVoices()));

        // Switching while silent: instant, no fade, notes at the same time play immediately.
        auto r = makeProcessor();
        r->setCurrentProgram (13);
        auto b3 = render (*r, chord (0.0, 0.2, { 60 }), 0.1);
        result ("switch while silent: no fade, immediate", r->getPresetFadeCount() == 0 && analyse (b3, 0, 256).peak > 0.0f, "");
    }

    // 37. Every factory preset rendered with a phrase suited to its category.
    std::printf ("\n%-18s %-6s %9s %9s %9s %8s %9s %10s  %s\n", "preset", "cat", "peak dBFS", "RMS dBFS", "limiter", "NaN/Inf", "denormal", "dense GR", "result");
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();
        int inTarget = 0, limited = 0;
        for (int i = 0; i < pm.getNumFactoryPresets(); ++i)
        {
            const auto info = pm.getPresets()[(size_t) (1 + i)];
            auto r = renderWithPreset ([i] (VSTForgeAudioProcessor& q) { q.setCurrentProgram (1 + i); return true; }, info.category);
            const auto st = analyse (r.audio);
            const float gr = -db (r.minLimiterGain);
            const bool pass = st.nonFinite == 0 && st.denormals == 0 && st.peak < 1.0f && db (st.rms) > -45.0f;
            const bool target = db (st.peak) >= -6.0f && db (st.peak) <= -3.0f;
            // Informational: a dense 6-note chord at full velocity (worst case) -> limiter gain reduction.
            float denseGr = 0.0f;
            {
                auto q = makeProcessor();
                q->setCurrentProgram (1 + i);
                float minG = 1.0f;
                auto d = render (*q, chord (0.0, 2.0, { 36, 48, 55, 60, 64, 67 }, 1.0f), 3.0, [&] (double) { minG = std::min (minG, q->getLimiterGain()); });
                const auto ds = analyse (d);
                ok &= ds.nonFinite == 0 && ds.denormals == 0 && ds.peak < 1.0f;
                denseGr = -db (minG);
            }
            inTarget += target ? 1 : 0;
            limited += gr > 0.01f ? 1 : 0;
            ok &= pass;
            std::printf ("%-18s %-6s %9.2f %9.2f %8.2f%s %8lld %9lld %9.2f  %s%s\n", info.name.toRawUTF8(), info.category.toRawUTF8(),
                         db (st.peak), db (st.rms), gr, gr > 0.01f ? "!" : " ", (long long) st.nonFinite, (long long) st.denormals,
                         denseGr, pass ? "PASS" : "FAIL", target ? "" : "  (outside -6..-3)");
        }
        result ("factory presets: peaks in -6..-3 dBFS", inTarget == pm.getNumFactoryPresets(), juce::String (inTarget) + "/24 in target");
        result ("factory presets: limiter never engaged", limited == 0, juce::String (limited) + " limited");
    }

    // 38. Preset tour (~38 s): 12 presets, 3.2 s each.
    {
        const char* tour[] = { "Warm Strings", "Forge Saw Lead", "Acid Squelch", "Bright Pluck", "Electric Tines", "Glass Pad",
                               "Wobble Bass", "Echo Drops", "Drawbar Organ", "Hollow Square", "Sci-Fi Bleeps", "Rising Sweep" };
        const double seg = 3.2;
        const int segLen = (int) (seg * renderRate);
        juce::AudioBuffer<float> all (2, segLen * 12);
        all.clear();
        auto probe = makeProcessor();
        auto& pm = probe->getPresetManager();
        bool found = true;
        for (int k = 0; k < 12; ++k)
        {
            int prog = -1;
            for (int i = 0; i < pm.getNumFactoryPresets(); ++i) if (pm.getPresets()[(size_t) (1 + i)].name == tour[k]) prog = 1 + i;
            found &= prog > 0;
            if (prog < 0) continue;
            auto r = renderWithPreset ([prog] (VSTForgeAudioProcessor& q) { q.setCurrentProgram (prog); return true; },
                                       pm.getPresets()[(size_t) prog].category, seg);
            const int fade = (int) (0.25 * renderRate);
            for (int c = 0; c < 2; ++c)
            {
                all.copyFrom (c, k * segLen, r.audio, c, 0, std::min (segLen, r.audio.getNumSamples()));
                all.applyGainRamp (c, k * segLen + segLen - fade, fade, 1.0f, 0.0f);
            }
        }
        check ("m5_preset_tour.wav (38 s)", all, "m5_preset_tour.wav");
        result ("tour presets found", found, "12 presets x 3.2 s");
    }

    // 39. UI flows through the preset UI: Save As dialog, Delete needs confirmation.
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();
        pm.setUserRoot (presetTemp.getChildFile ("ui"));
        p->setCurrentProgram (17);
        std::unique_ptr<juce::AudioProcessorEditor> edBase (p->createEditor());
        auto* ed = dynamic_cast<VSTForgeAudioProcessorEditor*> (edBase.get());
        auto& ui = ed->getPresetUi();
        ui.saveClicked();   // factory -> opens Save As
        auto* dlg = ui.getDialog();
        const bool opened = dlg != nullptr && dlg->isVisible();
        if (dlg != nullptr) { dlg->getNameEditor().setText ("UI Saved", false); dlg->clickOk(); }
        const auto f = presetTemp.getChildFile ("ui").getChildFile (pm.getCurrentCategory()).getChildFile ("UI Saved.vfpreset");
        result ("UI: Save on factory opens Save As; saves copy", opened && f.existsAsFile() && pm.isCurrentUser() && pm.getCurrentName() == "UI Saved",
                f.getParentDirectory().getFileName() + "/" + f.getFileName());
        ui.deleteClicked();
        auto* confirm = ui.getDialog();
        const bool stillThere = f.existsAsFile();     // nothing deleted before confirming
        const bool confirmShown = confirm != nullptr && confirm->isVisible();
        if (confirm != nullptr) confirm->clickOk();
        result ("UI: Delete asks for confirmation, then deletes", stillThere && confirmShown && ! f.exists() && ! pm.isCurrentUser(), "");
        ed->showPresetBrowser (true);
        result ("UI: browser lists all presets", ui.getBrowser() != nullptr && ui.getBrowser()->getNumVisibleCells() == 25,
                juce::String (ui.getBrowser() != nullptr ? ui.getBrowser()->getNumVisibleCells() : 0) + " cells");
        ui.setFilter ("Pad");
        result ("UI: category filter", ui.getBrowser()->getNumVisibleCells() == 4, juce::String (ui.getBrowser()->getNumVisibleCells()) + " Pad cells");
    }

    // 29. Editor snapshot + resize.
    {
        auto p = makeProcessor();
        p->getPresetManager().setUserRoot (presetTemp.getChildFile ("shots"));
        int prog = 1;
        for (int i = 0; i < p->getNumPrograms(); ++i) if (p->getProgramName (i) == "Pulse Motion") prog = i;
        p->setCurrentProgram (prog);
        // A user preset so the browser shows one too.
        p->getPresetManager().saveAs ("My Pulse Pad", "Pad", "Tester", false);
        p->setCurrentProgram (prog);
        setParam (*p, id::filterCutoff, 1100.0f);   // -> EDITED lamp lit
        render (*p, {}, 0.3);   // advance LFO phases for the display
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        for (int n : { 60, 64, 67 }) p->getKeyboardState().noteOn (1, n, 1.0f);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto save = [&] (const juce::Image& im, const char* name)
        {
            auto f = outDir.getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream fos (f);
            return fos.openedOk() && png.writeImageToStream (im, fos);
        };
        result ("m5_editor.png", save (img, "m5_editor.png"), juce::String (img.getWidth()) + "x" + juce::String (img.getHeight()));

        const int w0 = ed->getWidth(), h0 = ed->getHeight();
        ed->setSize (w0 * 3 / 2, h0 * 3 / 2);
        auto big = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        result ("editor resize (scales, size stored)", save (big, "m5_editor_resized.png")
                    && ed->getWidth() == w0 * 3 / 2 && p->getSavedEditorWidth() == w0 * 3 / 2,
                juce::String (w0) + "x" + juce::String (h0) + " -> " + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));
        ed->setSize (w0, h0);
        if (auto* fe = dynamic_cast<VSTForgeAudioProcessorEditor*> (ed.get()))
        {
            fe->showPresetBrowser (true);
            auto shot = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            result ("m5_browser.png (browser open)", save (shot, "m5_browser.png"), juce::String (shot.getWidth()) + "x" + juce::String (shot.getHeight()));
            fe->showPresetBrowser (false);
            fe->getPresetUi().saveAsClicked();
            auto dlgShot = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            result ("m5_saveas.png (Save As dialog)", save (dlgShot, "m5_saveas.png"), "");
        }
        ed->setSize (w0 * 3 / 2, h0 * 3 / 2);
        ed.reset();

        // Re-open: size restored.
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (p->createEditor());
        result ("editor reopens at saved size", ed2->getWidth() == w0 * 3 / 2, "width " + juce::String (ed2->getWidth()));
    }

    // 40. MIDI input status + LED (M6).
    {
        auto p = makeProcessor();
        const bool fresh = ! p->isMidiInputConnected() && p->getMidiActivityCount() == 0;
        result ("MIDI: fresh instance not connected", fresh, "");

        // On-screen keyboard only: not external MIDI.
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        p->getKeyboardState().noteOn (1, 60, 0.8f);
        p->processBlock (block, midi);
        midi.clear();   // a host hands over a fresh buffer each block
        p->getKeyboardState().noteOff (1, 60, 0.0f);
        p->processBlock (block, midi);
        result ("MIDI: on-screen keyboard not counted", ! p->hasReceivedExternalMidi() && p->getMidiActivityCount() == 0, "");

        // Clock only: confirms the MIDI route, but no flash.
        midi.clear();
        midi.addEvent (juce::MidiMessage::midiClock(), 0);
        p->processBlock (block, midi);
        result ("MIDI: clock marks connected, no flash", p->isMidiInputConnected() && p->getMidiActivityCount() == 0, "");

        // Host notes: one activity tick per channel message.
        render (*p, phrase (0.0), 2.0);
        const auto cnt = p->getMidiActivityCount();
        result ("MIDI: host notes counted", cnt == 16, juce::String ((int) cnt) + " events");

        // Standalone: device status decides.
        p->setStandaloneMidiInputs (0, {});
        const bool offWhenNoDevice = ! p->isMidiInputConnected();
        p->setStandaloneMidiInputs (1, "Minilab3 MIDI");
        result ("MIDI: standalone uses open-device count", offWhenNoDevice && p->isMidiInputConnected()
                    && p->getStandaloneMidiNames() == "Minilab3 MIDI", "");

        // LED states.
        std::atomic<bool> conn { false };
        std::atomic<uint32_t> act { 0 };
        vf::gui::MidiLed led ([&] { return conn.load(); }, [&] { return act.load(); });
        led.poll();
        const bool s0 = led.getState() == vf::gui::MidiLed::State::off;
        conn = true; led.poll();
        const bool s1 = led.getState() == vf::gui::MidiLed::State::on;
        act = 1; led.poll();
        const bool s2 = led.getState() == vf::gui::MidiLed::State::flash;
        juce::Thread::sleep (vf::gui::MidiLed::flashMs + 40);
        led.poll();
        const bool s3 = led.getState() == vf::gui::MidiLed::State::on;
        result ("MIDI LED: off -> on -> flash -> on", s0 && s1 && s2 && s3, "");

        // Editor snapshot with the LED lit.
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* fe = dynamic_cast<VSTForgeAudioProcessorEditor*> (ed.get());
        if (fe != nullptr) fe->getMidiLed().poll();
        const bool ledOn = fe != nullptr && fe->getMidiLed().getState() != vf::gui::MidiLed::State::off;
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = outDir.getChildFile ("m6_editor.png");
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat png;
        result ("m6_editor.png (MIDI LED lit)", ledOn && fos.openedOk() && png.writeImageToStream (img, fos),
                juce::String (img.getWidth()) + "x" + juce::String (img.getHeight()));
    }

    // 41. Rebrand (0.6.1): legacy preset files, legacy session state, user-folder migration.
    {
        auto p = makeProcessor();
        auto& pm = p->getPresetManager();
        const auto dir = presetTemp.getChildFile ("legacy");
        dir.getChildFile ("Lead").createDirectory();
        const auto oldFile = dir.getChildFile ("Lead/Old Format.vfpreset");
        oldFile.replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<VSTForgePreset format=\"1\" name=\"Old Format\" category=\"Lead\" author=\"VST Forge\" version=\"0.5.0\">\n"
                                 "  <VSTForgeSynth>\n"
                                 "    <PARAM id=\"filter_cutoff\" value=\"777\"/>\n"
                                 "    <PARAM id=\"master_gain\" value=\"-9\"/>\n"
                                 "  </VSTForgeSynth>\n"
                                 "</VSTForgePreset>\n");
        pm.setUserRoot (dir);
        const bool listed = ! pm.getIndicesInCategory ("User").empty();
        const bool loaded = pm.loadFromFile (oldFile);
        const bool values = loaded && std::abs (getParam (*p, id::filterCutoff) - 777.0f) < 0.5f && std::abs (getParam (*p, id::gain) + 9.0f) < 1e-3f;
        result ("legacy <VSTForgePreset> file loads", listed && loaded && values && pm.getCurrentName() == "Old Format",
                "cutoff " + juce::String (getParam (*p, id::filterCutoff), 1) + ", gain " + juce::String (getParam (*p, id::gain), 1));

        const auto r = pm.saveAs ("New Format", "Lead", "Tester", false);
        const auto text = dir.getChildFile ("Lead/New Format.vfpreset").loadFileAsString();
        result ("new saves use <SynSyn1000Preset>/<SynSyn1000>", r == PresetManager::Result::ok && text.contains ("<SynSyn1000Preset ")
                    && text.contains ("<SynSyn1000>") && ! text.contains ("VSTForge"), "");

        // Factory presets are stored in the new format and all load.
        int factoryOk = 0;
        for (int i = 0; i < pm.getNumFactoryPresets(); ++i) factoryOk += pm.loadFactory (i) ? 1 : 0;
        result ("factory presets load (new tags)", factoryOk == 24, juce::String (factoryOk) + "/24");

        // Session state: a pre-0.6.1 blob (<VSTForgeSynth> root) restores; new saves write <SynSyn1000>.
        auto q = makeProcessor();
        setParam (*q, id::filterCutoff, 2345.0f);
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        const bool newTag = xml != nullptr && xml->hasTagName ("SynSyn1000");
        if (xml != nullptr) xml->setTagName ("VSTForgeSynth");
        juce::MemoryBlock legacy; if (xml != nullptr) juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
        auto r2 = makeProcessor();
        r2->setStateInformation (legacy.getData(), (int) legacy.getSize());
        juce::MemoryBlock resaved; r2->getStateInformation (resaved);
        auto xml2 = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        result ("legacy <VSTForgeSynth> state restores, re-saves as <SynSyn1000>",
                newTag && std::abs (getParam (*r2, id::filterCutoff) - 2345.0f) < 0.5f && xml2 != nullptr && xml2->hasTagName ("SynSyn1000"), "");

        // User-folder migration: copy (never move) old -> new, only when new does not exist.
        const auto oldRoot = presetTemp.getChildFile ("mig/VSTForgeSynth/Presets");
        const auto newRoot = presetTemp.getChildFile ("mig/SynSyn1000/Presets");
        oldRoot.getChildFile ("Pad").createDirectory();
        oldRoot.getChildFile ("Pad/Mine.vfpreset").replaceWithText (oldFile.loadFileAsString());
        const bool copied = PresetManager::migrateLegacyUserPresets (oldRoot, newRoot);
        const bool both = newRoot.getChildFile ("Pad/Mine.vfpreset").existsAsFile() && oldRoot.getChildFile ("Pad/Mine.vfpreset").existsAsFile();
        newRoot.getChildFile ("Pad/Mine.vfpreset").replaceWithText ("edited");
        const bool notAgain = ! PresetManager::migrateLegacyUserPresets (oldRoot, newRoot)
                              && newRoot.getChildFile ("Pad/Mine.vfpreset").loadFileAsString() == "edited";
        result ("user preset folder migration (copy once, old kept)", copied && both && notAgain, "");
        result ("default user folder is .../SynSyn1000/Presets", PresetManager::legacyUserRoot().getFullPathName().contains ("VSTForgeSynth")
                    && PresetManager::defaultUserRoot() == presetTemp, "(override active in tests)");
        result ("product name", p->getName() == "SynSyn 1000", p->getName());
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* fe = dynamic_cast<VSTForgeAudioProcessorEditor*> (ed.get());
            if (fe != nullptr) fe->showAbout (true);
            const bool aboutOk = fe != nullptr && fe->isAboutVisible() && fe->aboutHasLogo();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto f = outDir.getChildFile ("m6_1_about.png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat png;
            result ("About box (logo, trademark line) -> m6_1_about.png", aboutOk && fos.openedOk() && png.writeImageToStream (img, fos)
                        && juce::String (vf::legal::aboutText()).contains ("VST is a registered trademark of Steinberg Media Technologies GmbH."),
                    juce::String (img.getWidth()) + "x" + juce::String (img.getHeight()));
        }
        result ("About text carries the AGPLv3 notice + source placeholder", juce::String (vf::legal::aboutText()).contains ("GNU Affero General Public License")
                    && juce::String (vf::legal::aboutText()).contains ("PLACEHOLDER"), "");
    }

    std::printf ("%s\n", ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return ok ? 0 : 1;
}
