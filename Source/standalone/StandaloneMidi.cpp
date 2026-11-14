#include "StandaloneMidi.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <map>

namespace vf::standalone
{
namespace
{
    constexpr const char* disabledKey = "vstforgeMidiDisabledInputs";

    class MidiAutoConnect final : public VSTForgeAudioProcessor::Extension,
                                  private juce::Timer,
                                  private juce::ChangeListener
    {
    public:
        explicit MidiAutoConnect (VSTForgeAudioProcessor& p) : processor (p)
        {
            // The holder sets its `processor` member (and opens the audio
            // device) only after createPluginFilter() returns: attach later.
            startTimer (100);
        }

        ~MidiAutoConnect() override
        {
            stopTimer();
            listConnection = {};
            if (deviceManager != nullptr)
                deviceManager->removeChangeListener (this);
        }

    private:
        void timerCallback() override
        {
            if (deviceManager == nullptr)
            {
                if (! attach()) return;
                startTimer (1000);   // from now on: poll fallback for hot-plug
                return;
            }
            refresh (false);
        }

        bool attach()
        {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            if (holder == nullptr || holder->processor.get() != &processor)
                return false;

            deviceManager = &holder->deviceManager;
            settings = holder->settings.get();
            if (settings != nullptr)
                userDisabled.addLines (settings->getValue (disabledKey));
            userDisabled.removeEmptyStrings();

            deviceManager->addChangeListener (this);
            listConnection = juce::MidiDeviceListConnection::make ([this] { refresh (false); });
            refresh (true);
            return true;
        }

        // Enable new devices (unless the user disabled them), close vanished ones.
        void refresh (bool initial)
        {
            if (deviceManager == nullptr) return;
            const auto devices = juce::MidiInput::getAvailableDevices();

            if (initial || devices != known)
            {
                for (const auto& old : known)
                    if (! devices.contains (old) && deviceManager->isMidiInputDeviceEnabled (old.identifier))
                        deviceManager->setMidiInputDeviceEnabled (old.identifier, false);

                for (const auto& d : devices)
                    if ((initial || ! known.contains (d)) && ! userDisabled.contains (d.name))
                        deviceManager->setMidiInputDeviceEnabled (d.identifier, true);   // no-op if already open

                known = devices;
                snapshot();
            }
            publish();
        }

        // Change from the device manager (e.g. a checkbox in the settings
        // dialog): remember explicit user choices for available devices.
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            bool changed = false;
            for (const auto& d : known)
            {
                const bool en = deviceManager->isMidiInputDeviceEnabled (d.identifier);
                const auto it = lastEnabled.find (d.identifier);
                if (it == lastEnabled.end() || it->second == en) continue;
                if (en)
                {
                    changed |= userDisabled.contains (d.name);
                    userDisabled.removeString (d.name);   // user ticked it again
                }
                else
                {
                    changed |= userDisabled.addIfNotAlreadyThere (d.name);   // user unticked it
                }
            }
            if (changed && settings != nullptr)
                settings->setValue (disabledKey, userDisabled.joinIntoString ("\n"));
            snapshot();
            publish();
        }

        void snapshot()
        {
            lastEnabled.clear();
            for (const auto& d : known)
                lastEnabled[d.identifier] = deviceManager->isMidiInputDeviceEnabled (d.identifier);
        }

        void publish()
        {
            juce::StringArray open;
            for (const auto& d : known)
                if (deviceManager->isMidiInputDeviceEnabled (d.identifier))
                    open.add (d.name);
            processor.setStandaloneMidiInputs (open.size(), open.joinIntoString (", "));
        }

        VSTForgeAudioProcessor& processor;
        juce::AudioDeviceManager* deviceManager = nullptr;
        juce::PropertySet* settings = nullptr;
        juce::MidiDeviceListConnection listConnection;
        juce::Array<juce::MidiDeviceInfo> known;
        std::map<juce::String, bool> lastEnabled;
        juce::StringArray userDisabled;
    };
}

std::unique_ptr<VSTForgeAudioProcessor::Extension> createMidiAutoConnect (VSTForgeAudioProcessor& p)
{
    if (p.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return nullptr;
    return std::make_unique<MidiAutoConnect> (p);
}
}
