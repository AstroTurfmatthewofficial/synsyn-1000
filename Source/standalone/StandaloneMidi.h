#pragma once

#include "PluginProcessor.h"

// Standalone MIDI auto-connect (M6). Only compiled into the plugin target
// (needs juce_audio_plugin_client's StandalonePluginHolder).
//
// JUCE's desktop Standalone wrapper opens *no* MIDI input devices by default
// (autoOpenMidiDevices is only on for Android/iOS), so a USB keyboard was
// detected by the OS but never reached the synth unless it was ticked by hand
// in Options > Audio/MIDI Settings. This helper:
//   - enables every available MIDI input at startup and whenever a new one
//     appears (juce::MidiDeviceListConnection + a 1 s poll as a fallback),
//   - closes inputs that disappear, so a re-plugged device is opened afresh,
//   - respects the settings dialog: a device the user unticks is remembered
//     by name in the Standalone's settings file ("vstforgeMidiDisabledInputs")
//     and is not auto-enabled again until the user ticks it again,
//   - publishes the number/names of open inputs to the processor (MIDI LED).
namespace vf::standalone
{
    // Returns the helper when running as the Standalone, nullptr otherwise.
    std::unique_ptr<VSTForgeAudioProcessor::Extension> createMidiAutoConnect (VSTForgeAudioProcessor&);
}
