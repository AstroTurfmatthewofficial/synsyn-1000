#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Preset storage and browsing (M5).
//
// File format (".vfpreset", human-readable XML):
//   <SynSyn1000Preset format="1" name="..." category="Pad" author="..." version="0.6.1">
//     <SynSyn1000> <PARAM id="..." value="..."/> ... </SynSyn1000>
//   </SynSyn1000Preset>
// Pre-0.6.1 files (VST Forge Synth) used <VSTForgePreset>/<VSTForgeSynth>; they
// are still read (any combination of old/new tags). New saves use the new tags.
// Parameters missing from a preset load as their defaults; unknown IDs are
// ignored; non-finite values fall back to the default; values are clamped.
//
// The list is: [0] Init, then the factory bank (embedded via BinaryData,
// read-only, in memory), then user presets found under the user folder
// (<root>/<Category>/<Name>.vfpreset).
//
// Threading: file I/O only happens in rescanUserPresets(), save*, rename*,
// delete* and loadPreset() of a *user* preset - all called from the message
// thread (UI). Factory/Init loads (also used for host program changes) never
// touch the disk.
namespace vf::presets
{
    struct PresetInfo
    {
        juce::String name, category, author, version;
        bool isFactory = false;
        bool isInit = false;
        juce::File file;          // user presets only
        int factoryIndex = -1;    // factory presets only
    };

    class PresetManager final : public juce::ChangeBroadcaster
    {
    public:
        static constexpr const char* fileExtension = ".vfpreset";
        static constexpr const char* rootTag = "SynSyn1000Preset";
        static constexpr const char* paramsTag = "SynSyn1000";
        static constexpr const char* legacyRootTag = "VSTForgePreset";     // <= 0.6.0
        static constexpr const char* legacyParamsTag = "VSTForgeSynth";    // <= 0.6.0
        static bool isPresetRoot (const juce::XmlElement& x) { return x.hasTagName (rootTag) || x.hasTagName (legacyRootTag); }
        static constexpr int formatVersion = 1;
        static constexpr int maxNameLength = 32;

        // Categories in display order. "User" is for uncategorised user presets.
        static const juce::StringArray& categories();
        static juce::File defaultUserRoot();   // ~/.local/share/SynSyn1000/Presets (Linux)
        static juce::File legacyUserRoot();    // ~/.local/share/VSTForgeSynth/Presets (<= 0.6.0)
        // First-run migration: if `legacy` exists and `target` does not, COPY legacy's
        // contents to target (the old folder is never moved or deleted). Returns true if copied.
        static bool migrateLegacyUserPresets (const juce::File& legacy, const juce::File& target);

        // beforeApply is called right before parameters change on a preset load
        // (the processor uses it to fade out and silence sounding voices).
        PresetManager (juce::AudioProcessorValueTreeState&, std::function<void()> beforeApply);

        //---------------------------------------------------------------- list
        const std::vector<PresetInfo>& getPresets() const noexcept { return presets; }
        int getNumFactoryPresets() const noexcept                   { return (int) factory.size(); }
        // Indices into getPresets() whose category matches ("All" = everything).
        std::vector<int> getIndicesInCategory (const juce::String& category) const;
        void rescanUserPresets();                // message thread; reads the user folder
        int getNumUnreadableFiles() const noexcept { return unreadableFiles; }

        void setUserRoot (const juce::File& root);  // tests / custom location
        juce::File getUserRoot() const              { return userRoot; }

        //---------------------------------------------------------------- load
        bool loadPreset (int index);             // false -> getLastError()
        bool loadInit()                          { return loadPreset (0); }
        bool loadFactory (int factoryIndex)      { return loadPreset (1 + factoryIndex); }
        bool loadFromFile (const juce::File&);   // any .vfpreset file
        // Step through the presets of a category (wraps around). Returns false if empty.
        bool step (int delta, const juce::String& category);

        //---------------------------------------------------------------- save
        enum class Result { ok, exists, invalidName, readOnly, notFound, ioError };
        Result saveAs (const juce::String& name, const juce::String& category, const juce::String& author, bool overwrite);
        Result saveCurrent();                    // overwrite the current user preset
        Result renamePreset (int index, const juce::String& newName);
        Result deletePreset (int index);
        juce::File fileFor (const juce::String& name, const juce::String& category) const;
        static bool isValidName (const juce::String& name);

        //---------------------------------------------------------------- current
        int getCurrentIndex() const noexcept          { return currentIndex; }
        juce::String getCurrentName() const           { return currentName; }
        juce::String getCurrentCategory() const       { return currentCategory; }
        bool isCurrentFactory() const noexcept        { return currentIndex >= 0 && currentIndex <= getNumFactoryPresets(); }
        bool isCurrentUser() const noexcept           { return currentIndex > getNumFactoryPresets(); }
        bool isModified() const;                      // parameters differ from the loaded preset
        juce::String getLastError() const             { return lastError; }
        juce::String getLastAuthor() const            { return lastAuthor; }

        // Host program index (0 = Init, 1..N = factory) of the current preset, or -1.
        int getCurrentProgram() const noexcept        { return isCurrentFactory() ? currentIndex : -1; }

        //---------------------------------------------------------------- session state
        void writeToState (juce::ValueTree& state) const;   // adds preset* properties
        void restoreFromState (const juce::ValueTree& state); // no file I/O
        static void stripStateProperties (juce::ValueTree& state);

        //---------------------------------------------------------------- XML helpers
        std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name, const juce::String& category,
                                                           const juce::String& author) const;
        // Parses a preset document. Returns false (and sets error) if it is not a valid preset.
        static bool parsePreset (const juce::XmlElement&, PresetInfo& info, juce::ValueTree& params, juce::String& error);
        static bool readPresetFile (const juce::File&, PresetInfo& info, juce::ValueTree& params, juce::String& error);

    private:
        void applyParams (const juce::ValueTree& params);
        void takeSnapshot();
        void setCurrent (int index, const juce::String& name, const juce::String& category);
        Result writePresetFile (const juce::File&, const juce::XmlElement&);
        int findUserPreset (const juce::File&) const;
        void rebuildList();

        juce::AudioProcessorValueTreeState& state;
        std::function<void()> beforeApply;
        juce::Array<juce::RangedAudioParameter*> params;

        struct Factory { PresetInfo info; juce::ValueTree values; };
        std::vector<Factory> factory;
        std::vector<PresetInfo> userPresets;
        std::vector<PresetInfo> presets;     // Init + factory + user
        juce::File userRoot;
        int unreadableFiles = 0;

        int currentIndex = 0;
        juce::String currentName { "Init" }, currentCategory { "Init" };
        juce::File pendingUserFile;          // user preset restored from a session, resolved on rescan
        std::vector<float> snapshot;         // normalised values right after the last load/save
        bool stickyModified = false;         // restored session was already modified
        juce::String lastError, lastAuthor { "User" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };
}
