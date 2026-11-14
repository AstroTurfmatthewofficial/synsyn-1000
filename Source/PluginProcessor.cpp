#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

VSTForgeAudioProcessor::VSTForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, stateTag, vf::params::createLayout()),
      presets (state, [this] { presetChangeCounter.fetch_add (1, std::memory_order_acq_rel); })
{
    params.bind (state);

    for (int i = 0; i < numVoices; ++i)
        voices[(size_t) i] = static_cast<vf::dsp::SynthVoice*> (synth.addVoice (new vf::dsp::SynthVoice (i)));

    synth.addSound (new vf::dsp::SynthSound());
    synth.setNoteStealingEnabled (true);
}

VSTForgeAudioProcessor::~VSTForgeAudioProcessor()
{
    standaloneMidiHelper.reset();   // detach from the Standalone's device manager first
}

void VSTForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (auto* v : voices)
        v->prepare (sampleRate, samplesPerBlock);

    limiter.prepare (sampleRate);
    effects.prepare (sampleRate, samplesPerBlock);
    currentSampleRate = sampleRate;
    lfoGlobalPosition.fill (0.0);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.gain->load(), -60.0f) + 1.0e-6f);
    keyboardState.reset();
    deferredMidi.ensureSize (4096);
    scratchMidi.ensureSize (4096);
    deferredMidi.clear();
    fadeLeft = fadeTotal = 0;
    lastOutputPeak = 0.0f;
}

void VSTForgeAudioProcessor::releaseResources()
{
    synth.allNotesOff (0, false);
}

void VSTForgeAudioProcessor::reset()
{
    effects.reset();
    limiter.prepare (currentSampleRate);
}

//==============================================================================
int VSTForgeAudioProcessor::getNumPrograms()
{
    return 1 + presets.getNumFactoryPresets();
}

int VSTForgeAudioProcessor::getCurrentProgram()
{
    const int p = presets.getCurrentProgram();
    return p >= 0 ? p : lastProgram;
}

void VSTForgeAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    lastProgram = index;
    presets.loadPreset (index);   // Init or factory: in-memory, no file I/O
}

const juce::String VSTForgeAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    return presets.getPresets()[(size_t) index].name;
}

//==============================================================================
void VSTForgeAudioProcessor::silenceEverything() noexcept
{
    synth.allNotesOff (0, false);
    effects.reset();
}

void VSTForgeAudioProcessor::handlePresetChange (juce::MidiBuffer& midi, int) noexcept
{
    if (const auto pc = presetChangeCounter.load (std::memory_order_acquire); pc != seenPresetChange)
    {
        seenPresetChange = pc;
        bool anyActive = lastOutputPeak > 1.0e-6f;
        for (auto* v : voices) anyActive = anyActive || v->isVoiceActive();
        if (anyActive)
        {
            if (fadeLeft <= 0)
            {
                fadeTotal = fadeLeft = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.01));
                presetFadeCount.fetch_add (1);
            }
        }
        else if (fadeLeft <= 0)
        {
            silenceEverything();   // nothing audible: switch instantly
        }
    }

    if (fadeLeft > 0)
    {
        // Defer note on/off (in order) until the fade has finished.
        scratchMidi.clear();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOnOrOff())
                deferredMidi.addEvent (msg, 0);
            else
                scratchMidi.addEvent (msg, meta.samplePosition);
        }
        midi.swapWith (scratchMidi);
    }
    else if (! deferredMidi.isEmpty())
    {
        midi.addEvents (deferredMidi, 0, -1, 0);
        deferredMidi.clear();
    }
}

void VSTForgeAudioProcessor::applyPresetFade (juce::AudioBuffer<float>& buffer) noexcept
{
    if (fadeLeft <= 0) return;
    const int n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float g = fadeLeft > 0 ? (float) (fadeLeft - 1) / (float) fadeTotal : 0.0f;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.getWritePointer (c)[i] *= g;
        if (fadeLeft > 0) --fadeLeft;
    }
    if (fadeLeft == 0)
        silenceEverything();
}

bool VSTForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet().isDisabled();
}

double VSTForgeAudioProcessor::getTailLengthSeconds() const
{
    double tail = (double) params.release->load();
    if (params.delayOn->load() >= 0.5f)
    {
        const double fb = juce::jlimit (0.01, 0.95, (double) params.delayFeedback->load());
        const double t = juce::jmin (vf::dsp::StereoDelay::maxDelaySeconds, (double) params.delayTime->load() * 0.001);
        tail += t * std::log (1.0e-4) / std::log (fb);   // ~-80 dB
    }
    if (params.reverbOn->load() >= 0.5f)
        tail += 1.0 + 12.0 * (double) params.reverbSize->load();
    return tail;
}

vf::dsp::FxParams VSTForgeAudioProcessor::readFxParams() const noexcept
{
    vf::dsp::FxParams f;
    f.chorus = { params.chorusOn->load() >= 0.5f, params.chorusRate->load(), params.chorusDepth->load(), params.chorusMix->load() };

    float timeMs = params.delayTime->load();
    if (params.delaySync->load() >= 0.5f)
    {
        const int div = juce::jlimit (0, vf::dsp::numLfoDivisions - 1, (int) std::lround (params.delayDiv->load()));
        timeMs = (float) (vf::dsp::lfoDivisions[div].beats * 60000.0 / syncBpm.load());
    }
    f.delay = { params.delayOn->load() >= 0.5f, timeMs, params.delayFeedback->load(), params.delayPingPong->load() >= 0.5f,
                params.delayHighCut->load(), params.delayMix->load() };
    f.reverb = { params.reverbOn->load() >= 0.5f, params.reverbSize->load(), params.reverbDamping->load(),
                 params.reverbWidth->load(), params.reverbPreDelay->load(), params.reverbMix->load() };
    return f;
}

vf::dsp::VoiceParams VSTForgeAudioProcessor::readVoiceParams() const noexcept
{
    using namespace vf::dsp;
    VoiceParams p;
    for (size_t i = 0; i < (size_t) numOscillators; ++i)
    {
        const auto& h = params.osc[i];
        p.osc[i].waveform = (Waveform) juce::jlimit (0, (int) Waveform::numWaveforms - 1, (int) std::lround (h.wave->load()));
        p.osc[i].on = h.on->load() >= 0.5f;
        p.osc[i].level = h.level->load();
        p.osc[i].coarseSemis = std::round (h.coarse->load());
        p.osc[i].fineCents = h.fine->load();
    }
    p.ampEnv = { params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() };
    p.filter.type = (FilterType) juce::jlimit (0, 2, (int) std::lround (params.filterType->load()));
    p.filter.cutoffHz = params.filterCutoff->load();
    p.filter.resonance = params.filterRes->load();
    p.filter.keytrack = params.filterKeytrack->load();
    p.filter.envAmount = params.filterEnvAmt->load();
    p.filterEnv = { params.fenvAttack->load(), params.fenvDecay->load(), params.fenvSustain->load(), params.fenvRelease->load() };
    return p;
}

void VSTForgeAudioProcessor::updateLfos (vf::dsp::VoiceParams& p, int numSamples) noexcept
{
    using namespace vf::dsp;

    // Host tempo / position for sync (falls back to 120 BPM, free-running).
    double bpm = fallbackBpm;
    std::optional<double> ppq;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition(); q.hasValue()) ppq = *q;
        }
    syncBpm.store (bpm);

    for (size_t k = 0; k < (size_t) numLfos; ++k)
    {
        const auto& h = params.lfo[k];
        auto& lp = p.lfo[k];
        lp.shape  = (LfoShape) juce::jlimit (0, (int) LfoShape::numShapes - 1, (int) std::lround (h.wave->load()));
        lp.target = (LfoTarget) juce::jlimit (0, (int) LfoTarget::numTargets - 1, (int) std::lround (h.target->load()));
        lp.amount = h.amount->load();
        lp.depth  = h.depth->load();
        if (k == 0 && params.lfo1ModWheel->load() >= 0.5f)
            lp.depth *= modWheel;
        lp.freeRunning  = std::lround (h.mode->load()) == 1;
        lp.delaySeconds = h.delay->load();
        lp.phaseOffset  = h.phase->load() / 360.0f;
        lp.globalSeed   = 0xA5A5A5A5u + (uint32_t) k * 0x9E3779B9u;

        const bool sync = h.sync->load() >= 0.5f;
        const double beats = lfoDivisions[juce::jlimit (0, numLfoDivisions - 1, (int) std::lround (h.div->load()))].beats;
        const double hz = sync ? (bpm / 60.0) / beats : (double) h.rate->load();
        lp.increment = hz / currentSampleRate;

        // Global (free-running) position: locked to the host timeline when
        // synced and playing, otherwise a running accumulator.
        double& gp = lfoGlobalPosition[k];
        if (sync && playing && ppq.has_value())
            gp = *ppq / beats;
        lp.globalPosition = gp;
        gp += lp.increment * numSamples;
        if (gp > 1.0e6) gp -= std::floor (gp);   // keep precision; S&H cycle index wraps harmlessly

        const double shown = lp.globalPosition + lp.phaseOffset;
        lfoDisplayPhase[k].store ((float) (shown - std::floor (shown)));
    }
}

void VSTForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    // MIDI LED: count external MIDI (host / Standalone devices) before the
    // on-screen keyboard is merged in. Read-only iteration, relaxed atomics.
    if (! midi.isEmpty())
    {
        uint32_t channelMsgs = 0;
        for (const auto meta : midi)
            if (meta.numBytes > 0 && meta.data[0] >= 0x80 && meta.data[0] < 0xf0)
                ++channelMsgs;
        if (! externalMidiSeen.load (std::memory_order_relaxed))
            externalMidiSeen.store (true, std::memory_order_relaxed);
        if (channelMsgs > 0)
            midiActivityCount.fetch_add (channelMsgs, std::memory_order_relaxed);
    }

    // Merge on-screen keyboard events into the host MIDI stream.
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    if (const int pw = uiPitchWheel.load(); pw != lastUiPitchWheel)
    {
        midi.addEvent (juce::MidiMessage::pitchWheel (1, pw), 0);
        lastUiPitchWheel = pw;
    }
    if (const int mw = uiModWheel.load(); mw != lastUiModWheel)
    {
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, mw), 0);
        lastUiModWheel = mw;
    }

    // Preset change: fade out / defer notes (see header).
    handlePresetChange (midi, numSamples);

    // Track the mod wheel (CC1) for the LFO 1 depth scaler.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isControllerOfType (1))
            modWheel = (float) msg.getControllerValue() / 127.0f;
    }

    const bool frozen = fadeLeft > 0;   // fading out after a preset change: keep the old sound
    vf::dsp::VoiceParams vp;
    if (frozen)
    {
        vp = lastVoiceParams;
        for (size_t k = 0; k < (size_t) vf::dsp::numLfos; ++k)
        {
            vp.lfo[k].globalPosition = lfoGlobalPosition[k];
            lfoGlobalPosition[k] += vp.lfo[k].increment * numSamples;
        }
    }
    else
    {
        vp = readVoiceParams();
        updateLfos (vp, numSamples);
        lastVoiceParams = vp;
    }
    for (auto* v : voices)
        v->setParams (vp);

    synth.renderNextBlock (buffer, midi, 0, numSamples);

    // Master gain (smoothed; multiplicative smoother needs a non-zero floor).
    if (! frozen)
        masterGain.setTargetValue (juce::Decibels::decibelsToGain (params.gain->load(), -60.0f) + 1.0e-6f);
    if (masterGain.isSmoothing())
    {
        auto* const* ch = buffer.getArrayOfWritePointers();
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = masterGain.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                ch[c][i] *= g;
        }
    }
    else
    {
        buffer.applyGain (masterGain.getTargetValue());
    }

    // Effects: chorus -> delay -> reverb (stereo; skipped entirely when all off).
    if (! frozen)
        lastFxParams = readFxParams();
    effects.process (buffer, lastFxParams);

    // Preset change fade-out (no-op unless a preset was just loaded).
    applyPresetFade (buffer);

    // Output safety limiter (-1 dBFS ceiling, also scrubs NaN/Inf).
    limiter.process (buffer);
    limiterGain.store (limiter.getCurrentGain());

    float peak = 0.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, numSamples));
    lastOutputPeak = peak;
}

juce::AudioProcessorEditor* VSTForgeAudioProcessor::createEditor()
{
    return new VSTForgeAudioProcessorEditor (*this);
}

void VSTForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("stateVersion", 3, nullptr);   // 3: + preset* properties (M5)
    if (const int w = editorWidth.load(); w > 0)
        copy.setProperty ("editorWidth", w, nullptr);
    presets.writeToState (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void VSTForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    // Accept the current tag and the pre-0.6.1 "VSTForgeSynth" tag (same layout;
    // only the name changed in the SynSyn 1000 rebrand).
    if (xml == nullptr || ! (xml->hasTagName (state.state.getType()) || xml->hasTagName (legacyStateTag)))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (state.state.getType()))
    {
        juce::ValueTree renamed (state.state.getType());
        renamed.copyPropertiesAndChildrenFrom (tree, nullptr);
        tree = renamed;
    }

    // Backward compatibility: parameters missing from an older session (e.g.
    // M1 had no osc 2/3 or filter) are reset to their defaults instead of
    // keeping whatever value this instance currently has.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! tree.getChildWithProperty ("id", rp->paramID).isValid())
                rp->setValueNotifyingHost (rp->getDefaultValue());

    editorWidth.store ((int) tree.getProperty ("editorWidth", 0));
    tree.removeProperty ("editorWidth", nullptr);
    tree.removeProperty ("stateVersion", nullptr);
    const auto presetInfo = tree.createCopy();
    vf::presets::PresetManager::stripStateProperties (tree);
    state.replaceState (tree);

    // The APVTS skips values it considers unchanged after snapping (e.g. a bool
    // whose raw value is 0.75 already "is" 1), leaving the raw parameter value
    // un-restored. Push the restored value into any parameter that is clearly
    // off (> 1e-4 normalised; float-ulp differences are left alone).
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = tree.getChildWithProperty ("id", rp->paramID);
            if (! child.isValid() || ! child.hasProperty ("value")) continue;
            const float target = rp->convertTo0to1 ((float) child.getProperty ("value"));
            if (std::abs (rp->getValue() - target) > 1.0e-4f)
                rp->setValueNotifyingHost (target);
        }

    presets.restoreFromState (presetInfo);   // after the parameters are applied
}
