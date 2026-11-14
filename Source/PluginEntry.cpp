#include "PluginProcessor.h"
#include "standalone/StandaloneMidi.h"

// Plugin-wrapper entry point (only compiled into the plugin target).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto* p = new VSTForgeAudioProcessor();
    // Standalone: auto-enable USB/ALSA MIDI inputs (no-op for VST3/LV2).
    p->setStandaloneMidiHelper (vf::standalone::createMidiAutoConnect (*p));
    return p;
}
