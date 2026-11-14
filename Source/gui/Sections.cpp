#include "Sections.h"
#include "Parameters.h"

namespace vf::gui
{
    namespace id = vf::params::id;

    static juce::RangedAudioParameter& P (juce::AudioProcessorValueTreeState& s, const juce::String& pid)
    {
        auto* p = s.getParameter (pid);
        jassert (p != nullptr);
        return *p;
    }

    //==========================================================================
    VolumeSection::VolumeSection (juce::AudioProcessorValueTreeState& s)
        : SectionPanel ("Volume"), readout (P (s, id::gain), {}), fader (s, id::gain)
    {
        addAndMakeVisible (readout);
        addAndMakeVisible (fader);
    }

    void VolumeSection::resized()
    {
        auto r = getBodyBounds();
        readout.setBounds (r.removeFromTop (rowHeight));
        r.removeFromTop (3);
        fader.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 40), r.getHeight()));
    }

    //==========================================================================
    OscillatorSection::OscillatorSection (juce::AudioProcessorValueTreeState& s, int i)
        : SectionPanel ("Oscillator " + juce::String (i + 1)),
          wave   (P (s, vf::params::oscId (i, "wave")),   "Wave"),
          level  (P (s, vf::params::oscId (i, "level")),  "Level"),
          coarse (P (s, vf::params::oscId (i, "coarse")), "Coarse"),
          fine   (P (s, vf::params::oscId (i, "fine")),   "Fine"),
          on     (P (s, vf::params::oscId (i, "on")),     "Active", 0.55f),
          display (std::vector<std::atomic<float>*> { s.getRawParameterValue (vf::params::oscId (i, "wave")),
                                                      s.getRawParameterValue (vf::params::oscId (i, "on")) })
    {
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &level, &coarse, &fine, &on, &display })
            addAndMakeVisible (c);
    }

    void OscillatorSection::resized()
    {
        auto r = getBodyBounds();
        auto left = r.removeFromLeft (r.getWidth() * 60 / 100);
        layoutRows (left, { &wave, &level, &coarse, &fine });
        r.removeFromLeft (3);
        on.setBounds (r.removeFromTop (left.getHeight() / 4));
        r.removeFromTop (3);
        display.setBounds (r);
    }

    //==========================================================================
    EnvelopeSection::EnvelopeSection (juce::AudioProcessorValueTreeState& s, const juce::String& sectionTitle,
                                      const char* a, const char* d, const char* su, const char* rel)
        : SectionPanel (sectionTitle),
          attack  (P (s, a),  "Attack"),
          decay   (P (s, d),  "Decay"),
          sustain (P (s, su), "Sustain"),
          release (P (s, rel), "Release"),
          display (std::vector<std::atomic<float>*> { s.getRawParameterValue (a), s.getRawParameterValue (d),
                                                      s.getRawParameterValue (su), s.getRawParameterValue (rel) })
    {
        for (auto* c : std::initializer_list<juce::Component*> { &attack, &decay, &sustain, &release, &display })
            addAndMakeVisible (c);
    }

    void EnvelopeSection::resized()
    {
        auto r = getBodyBounds();
        layoutRows (r.removeFromLeft (juce::jmin (190, r.getWidth() / 2)), { &attack, &decay, &sustain, &release });
        r.removeFromLeft (3);
        display.setBounds (r);
    }

    //==========================================================================
    LfoSection::LfoSection (juce::AudioProcessorValueTreeState& s, int i, std::function<float()> phaseGetter)
        : SectionPanel ("LFO " + juce::String (i + 1)),
          wave     (P (s, vf::params::lfoId (i, "wave")),   "Wave"),
          rate     (P (s, vf::params::lfoId (i, "rate")),   "Rate"),
          sync     (P (s, vf::params::lfoId (i, "sync")),   "Sync"),
          division (P (s, vf::params::lfoId (i, "div")),    "Div"),
          target   (P (s, vf::params::lfoId (i, "target")), "Dest"),
          amount   (P (s, vf::params::lfoId (i, "amount")), "Amt"),
          depth    (P (s, vf::params::lfoId (i, "depth")),  "Depth"),
          mode     (P (s, vf::params::lfoId (i, "mode")),   "Mode"),
          delay    (P (s, vf::params::lfoId (i, "delay")),  "Delay"),
          phase    (P (s, vf::params::lfoId (i, "phase")),  "Phase"),
          display (std::vector<std::atomic<float>*> { s.getRawParameterValue (vf::params::lfoId (i, "wave")),
                                                      s.getRawParameterValue (vf::params::lfoId (i, "target")),
                                                      s.getRawParameterValue (vf::params::lfoId (i, "amount")) },
                   std::move (phaseGetter))
    {
        if (i == 0)
            modWheel = std::make_unique<LcdParam> (P (s, id::lfo1ModWheel), "ModWhl");
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &rate, &sync, &division, &target, &amount,
                                                                 &depth, &mode, &delay, &phase, &display })
            addAndMakeVisible (c);
        if (modWheel != nullptr) addAndMakeVisible (*modWheel);
    }

    void LfoSection::resized()
    {
        auto r = getBodyBounds();
        const int w = r.getWidth();
        auto c1 = r.removeFromLeft (w * 27 / 100);
        auto c2 = r.removeFromLeft (w * 27 / 100);
        auto c3 = r.removeFromLeft (w * 25 / 100);
        layoutRows (c1, { &wave, &rate, &sync, &division });
        layoutRows (c2, { &target, &amount, &depth, &mode });
        const int rowH = c3.getHeight() / 4;
        delay.setBounds (c3.removeFromTop (rowH));
        phase.setBounds (c3.removeFromTop (rowH));
        if (modWheel != nullptr) modWheel->setBounds (c3.removeFromTop (rowH));
        r.removeFromLeft (3);
        display.setBounds (r);
    }

    //==========================================================================
    static std::vector<std::atomic<float>*> raw (juce::AudioProcessorValueTreeState& s, std::initializer_list<const char*> ids)
    {
        std::vector<std::atomic<float>*> v;
        for (auto* i : ids) v.push_back (s.getRawParameterValue (i));
        return v;
    }

    ChorusSection::ChorusSection (juce::AudioProcessorValueTreeState& s)
        : SectionPanel ("Chorus"),
          on (P (s, id::chorusOn), "Active"), rate (P (s, id::chorusRate), "Rate"),
          depth (P (s, id::chorusDepth), "Depth"), mix (P (s, id::chorusMix), "Mix"),
          display (FxDisplay::Kind::chorus, raw (s, { id::chorusRate, id::chorusDepth }), s.getRawParameterValue (id::chorusOn))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &rate, &depth, &mix, &display })
            addAndMakeVisible (c);
    }

    void ChorusSection::resized()
    {
        auto r = getBodyBounds();
        layoutRows (r.removeFromLeft (r.getWidth() * 58 / 100), { &on, &rate, &depth, &mix });
        r.removeFromLeft (3);
        display.setBounds (r);
    }

    DelaySection::DelaySection (juce::AudioProcessorValueTreeState& s)
        : SectionPanel ("Delay"),
          on (P (s, id::delayOn), "Active"), time (P (s, id::delayTime), "Time"),
          sync (P (s, id::delaySync), "Sync"), division (P (s, id::delayDiv), "Div"),
          feedback (P (s, id::delayFeedback), "Fdbk"), pingPong (P (s, id::delayPingPong), "PingPg"),
          highCut (P (s, id::delayHighCut), "HiCut"), mix (P (s, id::delayMix), "Mix"),
          display (FxDisplay::Kind::delay, raw (s, { id::delayTime, id::delayFeedback, id::delayPingPong }), s.getRawParameterValue (id::delayOn))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &time, &sync, &division, &feedback, &pingPong, &highCut, &mix, &display })
            addAndMakeVisible (c);
    }

    void DelaySection::resized()
    {
        auto r = getBodyBounds();
        const int w = r.getWidth();
        layoutRows (r.removeFromLeft (w * 37 / 100), { &on, &time, &sync, &division });
        layoutRows (r.removeFromLeft (w * 37 / 100), { &feedback, &pingPong, &highCut, &mix });
        r.removeFromLeft (3);
        display.setBounds (r);
    }

    ReverbSection::ReverbSection (juce::AudioProcessorValueTreeState& s)
        : SectionPanel ("Reverb"),
          on (P (s, id::reverbOn), "Active"), size (P (s, id::reverbSize), "Size"),
          damping (P (s, id::reverbDamping), "Damp"), width (P (s, id::reverbWidth), "Width"),
          preDelay (P (s, id::reverbPreDelay), "PreDly"), mix (P (s, id::reverbMix), "Mix"),
          display (FxDisplay::Kind::reverb, raw (s, { id::reverbSize, id::reverbDamping, id::reverbPreDelay }), s.getRawParameterValue (id::reverbOn))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &size, &damping, &width, &preDelay, &mix, &display })
            addAndMakeVisible (c);
    }

    void ReverbSection::resized()
    {
        auto r = getBodyBounds();
        const int w = r.getWidth();
        layoutRows (r.removeFromLeft (w * 38 / 100), { &on, &size, &damping, &width });
        auto c2 = r.removeFromLeft (w * 38 / 100);
        const int rowH = c2.getHeight() / 4;
        preDelay.setBounds (c2.removeFromTop (rowH));
        mix.setBounds (c2.removeFromTop (rowH));
        r.removeFromLeft (3);
        display.setBounds (r);
    }

    //==========================================================================
    FilterSection::FilterSection (juce::AudioProcessorValueTreeState& s)
        : SectionPanel ("Filter"),
          type      (P (s, id::filterType),     "Type"),
          cutoff    (P (s, id::filterCutoff),   "Cutoff"),
          resonance (P (s, id::filterRes),      "Reso"),
          keytrack  (P (s, id::filterKeytrack), "Key trk"),
          envAmount (P (s, id::filterEnvAmt),   "Env amt", 0.55f),
          display (std::vector<std::atomic<float>*> { s.getRawParameterValue (id::filterType),
                                                      s.getRawParameterValue (id::filterCutoff),
                                                      s.getRawParameterValue (id::filterRes) })
    {
        for (auto* c : std::initializer_list<juce::Component*> { &type, &cutoff, &resonance, &keytrack, &envAmount, &display })
            addAndMakeVisible (c);
    }

    void FilterSection::resized()
    {
        auto r = getBodyBounds();
        auto left = r.removeFromLeft (juce::jmin (190, r.getWidth() / 2));
        layoutRows (left, { &type, &cutoff, &resonance, &keytrack });
        r.removeFromLeft (3);
        envAmount.setBounds (r.removeFromTop (left.getHeight() / 4));
        r.removeFromTop (3);
        display.setBounds (r);
    }
}
