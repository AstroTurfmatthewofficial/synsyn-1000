#include "PresetManager.h"
#include "VSTForgeFactoryPresets.h"

namespace vf::presets
{
    namespace
    {
        int categoryIndex (const juce::String& c)
        {
            const int i = PresetManager::categories().indexOf (c, true);
            return i < 0 ? PresetManager::categories().size() : i;
        }

        bool lessByCategoryThenName (const PresetInfo& a, const PresetInfo& b)
        {
            const int ca = categoryIndex (a.category), cb = categoryIndex (b.category);
            if (ca != cb) return ca < cb;
            return a.name.compareNatural (b.name) < 0;
        }

        // Normalised value whose plain value reproduces `plain` exactly when
        // possible (log/skewed ranges are not exactly invertible in float, so
        // search a few ulps around the direct conversion). This makes
        // save -> load -> save a fixed point.
        float bestNormalised (const juce::RangedAudioParameter& p, float plain)
        {
            // What the DSP / APVTS state ends up holding after setValue (c):
            // AudioParameterFloat stores f(c) and reports f^-1 of that, which
            // the APVTS denormalises again.
            auto resulting = [&p] (float c) { return p.convertFrom0to1 (p.convertTo0to1 (p.convertFrom0to1 (c))); };
            const float n0 = juce::jlimit (0.0f, 1.0f, p.convertTo0to1 (plain));
            float best = n0, bestErr = std::abs (resulting (n0) - plain);
            float up = n0, down = n0;
            for (int k = 0; k < 64 && bestErr > 0.0f; ++k)
            {
                up = std::nextafter (up, 2.0f);
                down = std::nextafter (down, -1.0f);
                for (float c : { down, up })
                {
                    if (c < 0.0f || c > 1.0f) continue;
                    const float e = std::abs (resulting (c) - plain);
                    if (e < bestErr) { best = c; bestErr = e; }
                }
            }
            return best;
        }

        juce::String normaliseCategory (const juce::String& c)
        {
            const int i = PresetManager::categories().indexOf (c.trim(), true);
            return i < 0 ? juce::String ("User") : PresetManager::categories()[i];
        }
    }

    const juce::StringArray& PresetManager::categories()
    {
        static const juce::StringArray c { "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "User" };
        return c;
    }

    static juce::File userDataBase()
    {
       #if JUCE_LINUX || JUCE_BSD
        // XDG data dir: $XDG_DATA_HOME or ~/.local/share
        auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
        return xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg)
                   ? juce::File (xdg)
                   : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
    }

    static juce::File presetDirOverride()
    {
        for (auto* name : { "SYNSYN1000_PRESET_DIR", "VSTFORGE_PRESET_DIR" })
            if (auto env = juce::SystemStats::getEnvironmentVariable (name, {}); env.isNotEmpty())
                return juce::File (env);
        return {};
    }

    juce::File PresetManager::defaultUserRoot()
    {
        if (auto o = presetDirOverride(); o != juce::File())
            return o;
        return userDataBase().getChildFile ("SynSyn1000/Presets");
    }

    juce::File PresetManager::legacyUserRoot()
    {
        return userDataBase().getChildFile ("VSTForgeSynth/Presets");
    }

    bool PresetManager::migrateLegacyUserPresets (const juce::File& legacy, const juce::File& target)
    {
        if (! legacy.isDirectory() || target.exists())
            return false;
        // copyDirectoryTo copies recursively and leaves the source untouched.
        return target.getParentDirectory().createDirectory().wasOk() && legacy.copyDirectoryTo (target);
    }

    //==========================================================================
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, std::function<void()> before)
        : state (s), beforeApply (std::move (before)), userRoot (defaultUserRoot())
    {
        // 0.6.1 rebrand: bring the old VST Forge Synth user presets across once (copy only).
        if (presetDirOverride() == juce::File())
            migrateLegacyUserPresets (legacyUserRoot(), userRoot);

        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                params.add (rp);

        // Factory bank: parsed once from the embedded data, kept in memory.
        for (int i = 0; i < VSTForgeFactoryPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            const char* data = VSTForgeFactoryPresets::getNamedResource (VSTForgeFactoryPresets::namedResourceList[i], size);
            if (data == nullptr || size <= 0) continue;
            auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
            Factory f;
            juce::String err;
            if (xml != nullptr && parsePreset (*xml, f.info, f.values, err))
            {
                f.info.isFactory = true;
                factory.push_back (std::move (f));
            }
            else
            {
                jassertfalse; // broken factory preset file
            }
        }
        std::stable_sort (factory.begin(), factory.end(),
                          [] (const Factory& a, const Factory& b) { return lessByCategoryThenName (a.info, b.info); });
        for (size_t i = 0; i < factory.size(); ++i)
            factory[i].info.factoryIndex = (int) i;

        rebuildList();
        snapshot.resize ((size_t) params.size());
        for (int i = 0; i < params.size(); ++i)
            snapshot[(size_t) i] = params[i]->getDefaultValue();
    }

    void PresetManager::rebuildList()
    {
        presets.clear();
        PresetInfo init;
        init.name = "Init"; init.category = "Init"; init.author = "DoubleDouble Instrumentations"; init.isInit = true; init.isFactory = true;
        presets.push_back (init);
        for (auto& f : factory) presets.push_back (f.info);
        for (auto& u : userPresets) presets.push_back (u);
    }

    void PresetManager::setUserRoot (const juce::File& root)
    {
        userRoot = root;
        rescanUserPresets();
    }

    void PresetManager::rescanUserPresets()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::File currentFile = isCurrentUser() ? presets[(size_t) currentIndex].file : pendingUserFile;

        userPresets.clear();
        unreadableFiles = 0;
        if (userRoot.isDirectory())
        {
            for (const auto& f : userRoot.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension))
            {
                PresetInfo info;
                juce::ValueTree values;
                juce::String err;
                if (readPresetFile (f, info, values, err))
                    userPresets.push_back (info);
                else
                    ++unreadableFiles;
            }
        }
        std::stable_sort (userPresets.begin(), userPresets.end(), lessByCategoryThenName);
        rebuildList();

        if (currentFile != juce::File())
        {
            const int idx = findUserPreset (currentFile);
            if (idx >= 0) { currentIndex = idx; pendingUserFile = juce::File(); }
            else if (pendingUserFile == juce::File()) currentIndex = -1;   // file vanished
        }
        sendChangeMessage();
    }

    int PresetManager::findUserPreset (const juce::File& f) const
    {
        for (size_t i = 0; i < presets.size(); ++i)
            if (! presets[i].isFactory && presets[i].file == f)
                return (int) i;
        return -1;
    }

    std::vector<int> PresetManager::getIndicesInCategory (const juce::String& category) const
    {
        std::vector<int> out;
        const bool all = category.isEmpty() || category.equalsIgnoreCase ("All");
        const bool user = category.equalsIgnoreCase ("User");
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const auto& p = presets[i];
            if (all || (user && ! p.isFactory) || (! user && p.category.equalsIgnoreCase (category)))
                out.push_back ((int) i);
        }
        return out;
    }

    //==========================================================================
    bool PresetManager::parsePreset (const juce::XmlElement& xml, PresetInfo& info, juce::ValueTree& values, juce::String& error)
    {
        if (! isPresetRoot (xml))
        {
            error = "not a SynSyn 1000 preset";
            return false;
        }
        const int fmt = xml.getIntAttribute ("format", 1);
        if (fmt > formatVersion)
        {
            error = "preset format " + juce::String (fmt) + " is newer than this plug-in";
            return false;
        }
        info.name = xml.getStringAttribute ("name").trim().substring (0, maxNameLength);
        if (info.name.isEmpty())
        {
            error = "preset has no name";
            return false;
        }
        info.category = normaliseCategory (xml.getStringAttribute ("category"));
        info.author   = xml.getStringAttribute ("author");
        info.version  = xml.getStringAttribute ("version");

        values = juce::ValueTree ("PARAMS");
        auto* p = xml.getChildByName (paramsTag);
        if (p == nullptr)
            p = xml.getChildByName (legacyParamsTag);
        if (p != nullptr)
        {
            for (auto* e : p->getChildWithTagNameIterator ("PARAM"))
            {
                if (! e->hasAttribute ("id") || ! e->hasAttribute ("value")) continue;
                juce::ValueTree v ("PARAM");
                v.setProperty ("id", e->getStringAttribute ("id"), nullptr);
                v.setProperty ("value", e->getDoubleAttribute ("value"), nullptr);
                values.appendChild (v, nullptr);
            }
        }
        else
        {
            error = "preset has no parameter block";
            return false;
        }
        return true;
    }

    bool PresetManager::readPresetFile (const juce::File& f, PresetInfo& info, juce::ValueTree& values, juce::String& error)
    {
        if (! f.existsAsFile())
        {
            error = "file not found: " + f.getFileName();
            return false;
        }
        if (f.getSize() > 1024 * 1024)
        {
            error = "file too large: " + f.getFileName();
            return false;
        }
        juce::XmlDocument doc (f);
        auto xml = doc.getDocumentElement();
        if (xml == nullptr)
        {
            error = "unreadable preset (" + (doc.getLastParseError().isNotEmpty() ? doc.getLastParseError() : juce::String ("empty file")) + ")";
            return false;
        }
        if (! parsePreset (*xml, info, values, error))
            return false;
        info.isFactory = false;
        info.file = f;
        return true;
    }

    std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name, const juce::String& category,
                                                                      const juce::String& author) const
    {
        auto xml = std::make_unique<juce::XmlElement> (rootTag);
        xml->setAttribute ("format", formatVersion);
        xml->setAttribute ("name", name);
        xml->setAttribute ("category", category);
        xml->setAttribute ("author", author);
        xml->setAttribute ("version", JUCE_STRINGIFY (VSTFORGE_VERSION_STRING));
        auto* block = xml->createNewChildElement (paramsTag);
        for (auto* p : params)
        {
            auto* e = block->createNewChildElement ("PARAM");
            e->setAttribute ("id", p->paramID);
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        return xml;
    }

    //==========================================================================
    void PresetManager::applyParams (const juce::ValueTree& values)
    {
        if (beforeApply) beforeApply();

        std::map<juce::String, double> map;
        for (const auto& v : values)
            map[v.getProperty ("id").toString()] = (double) v.getProperty ("value");

        for (auto* p : params)
        {
            float norm = p->getDefaultValue();
            if (auto it = map.find (p->paramID); it != map.end() && std::isfinite (it->second))
            {
                const auto& range = p->getNormalisableRange();
                const float plain = juce::jlimit (range.start, range.end, (float) it->second);
                norm = bestNormalised (*p, plain);
            }
            if (! juce::exactlyEqual (p->getValue(), norm))
                p->setValueNotifyingHost (norm);
        }
    }

    void PresetManager::takeSnapshot()
    {
        snapshot.resize ((size_t) params.size());
        for (int i = 0; i < params.size(); ++i)
            snapshot[(size_t) i] = params[i]->getValue();
        stickyModified = false;
    }

    bool PresetManager::isModified() const
    {
        if (stickyModified) return true;
        for (int i = 0; i < params.size() && i < (int) snapshot.size(); ++i)
            if (std::abs (params[i]->getValue() - snapshot[(size_t) i]) > 1.0e-6f)
                return true;
        return false;
    }

    void PresetManager::setCurrent (int index, const juce::String& name, const juce::String& category)
    {
        currentIndex = index;
        currentName = name;
        currentCategory = category;
        pendingUserFile = juce::File();
    }

    bool PresetManager::loadPreset (int index)
    {
        if (index < 0 || index >= (int) presets.size())
        {
            lastError = "no such preset";
            return false;
        }
        const auto info = presets[(size_t) index];
        if (info.isInit)
        {
            applyParams (juce::ValueTree ("PARAMS"));
        }
        else if (info.isFactory)
        {
            applyParams (factory[(size_t) info.factoryIndex].values);
        }
        else
        {
            // User preset: re-read the file (it may have been edited outside).
            PresetInfo fresh;
            juce::ValueTree values;
            if (! readPresetFile (info.file, fresh, values, lastError))
                return false;   // state untouched
            applyParams (values);
            setCurrent (index, fresh.name, fresh.category);
            takeSnapshot();
            sendChangeMessage();
            return true;
        }
        setCurrent (index, info.name, info.category);
        takeSnapshot();
        sendChangeMessage();
        return true;
    }

    bool PresetManager::loadFromFile (const juce::File& f)
    {
        PresetInfo info;
        juce::ValueTree values;
        if (! readPresetFile (f, info, values, lastError))
            return false;
        applyParams (values);
        const int idx = findUserPreset (f);
        setCurrent (idx, info.name, info.category);
        takeSnapshot();
        sendChangeMessage();
        return true;
    }

    bool PresetManager::step (int delta, const juce::String& category)
    {
        const auto list = getIndicesInCategory (category);
        if (list.empty()) return false;
        auto it = std::find (list.begin(), list.end(), currentIndex);
        int pos;
        if (it == list.end())
            pos = delta > 0 ? 0 : (int) list.size() - 1;
        else
            pos = ((int) (it - list.begin()) + delta % (int) list.size() + (int) list.size()) % (int) list.size();
        // Skip unreadable user files instead of getting stuck on them.
        for (size_t tries = 0; tries < list.size(); ++tries)
        {
            if (loadPreset (list[(size_t) pos])) return true;
            pos = (pos + (delta >= 0 ? 1 : -1) + (int) list.size()) % (int) list.size();
        }
        return false;
    }

    //==========================================================================
    bool PresetManager::isValidName (const juce::String& name)
    {
        const auto n = name.trim();
        return n.isNotEmpty() && n.length() <= maxNameLength
            && ! n.containsAnyOf ("/\\:*?\"<>|") && n != "." && n != ".."
            && juce::File::createLegalFileName (n).isNotEmpty();
    }

    juce::File PresetManager::fileFor (const juce::String& name, const juce::String& category) const
    {
        return userRoot.getChildFile (normaliseCategory (category))
                       .getChildFile (juce::File::createLegalFileName (name.trim()) + fileExtension);
    }

    PresetManager::Result PresetManager::writePresetFile (const juce::File& f, const juce::XmlElement& xml)
    {
        if (! f.getParentDirectory().createDirectory())
        {
            lastError = "cannot create folder " + f.getParentDirectory().getFullPathName();
            return Result::ioError;
        }
        juce::TemporaryFile temp (f);
        if (! xml.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        {
            lastError = "cannot write " + f.getFullPathName();
            return Result::ioError;
        }
        return Result::ok;
    }

    PresetManager::Result PresetManager::saveAs (const juce::String& rawName, const juce::String& rawCategory,
                                                 const juce::String& author, bool overwrite)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto name = rawName.trim();
        if (! isValidName (name))
        {
            lastError = "invalid name";
            return Result::invalidName;
        }
        const auto category = normaliseCategory (rawCategory);
        const auto file = fileFor (name, category);
        if (file.exists() && ! overwrite)
        {
            lastError = "a preset with this name already exists";
            return Result::exists;
        }
        const auto who = author.trim().isNotEmpty() ? author.trim() : juce::String ("User");
        auto xml = createPresetXml (name, category, who);
        if (auto r = writePresetFile (file, *xml); r != Result::ok)
            return r;
        lastAuthor = who;
        pendingUserFile = file;
        currentIndex = -1;
        rescanUserPresets();
        setCurrent (findUserPreset (file), name, category);
        takeSnapshot();
        sendChangeMessage();
        return Result::ok;
    }

    PresetManager::Result PresetManager::saveCurrent()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (! isCurrentUser())
        {
            lastError = "factory presets are read-only (use Save As)";
            return Result::readOnly;
        }
        const auto info = presets[(size_t) currentIndex];
        auto xml = createPresetXml (info.name, info.category, info.author.isNotEmpty() ? info.author : lastAuthor);
        if (auto r = writePresetFile (info.file, *xml); r != Result::ok)
            return r;
        takeSnapshot();
        sendChangeMessage();
        return Result::ok;
    }

    PresetManager::Result PresetManager::renamePreset (int index, const juce::String& rawName)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (index < 0 || index >= (int) presets.size()) { lastError = "no such preset"; return Result::notFound; }
        const auto info = presets[(size_t) index];
        if (info.isFactory) { lastError = "factory presets are read-only"; return Result::readOnly; }
        const auto name = rawName.trim();
        if (! isValidName (name)) { lastError = "invalid name"; return Result::invalidName; }

        const auto target = fileFor (name, info.category);
        if (target != info.file && target.exists()) { lastError = "a preset with this name already exists"; return Result::exists; }

        juce::XmlDocument doc (info.file);
        auto xml = doc.getDocumentElement();
        if (xml == nullptr || ! isPresetRoot (*xml)) { lastError = "cannot read " + info.file.getFileName(); return Result::ioError; }
        xml->setAttribute ("name", name);
        if (auto r = writePresetFile (target, *xml); r != Result::ok)
            return r;
        if (target != info.file && ! info.file.deleteFile())
        {
            target.deleteFile();
            lastError = "cannot remove " + info.file.getFileName();
            return Result::ioError;
        }

        const bool wasCurrent = index == currentIndex;
        if (wasCurrent) { pendingUserFile = target; currentIndex = -1; }
        rescanUserPresets();
        if (wasCurrent)
        {
            const bool modified = isModified();
            currentIndex = findUserPreset (target);
            currentName = name;
            stickyModified = modified && stickyModified;
        }
        sendChangeMessage();
        return Result::ok;
    }

    PresetManager::Result PresetManager::deletePreset (int index)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (index < 0 || index >= (int) presets.size()) { lastError = "no such preset"; return Result::notFound; }
        const auto info = presets[(size_t) index];
        if (info.isFactory) { lastError = "factory presets cannot be deleted"; return Result::readOnly; }
        if (info.file.exists() && ! info.file.deleteFile()) { lastError = "cannot delete " + info.file.getFileName(); return Result::ioError; }

        const bool wasCurrent = index == currentIndex;
        if (wasCurrent) currentIndex = -1;
        rescanUserPresets();
        if (wasCurrent)
        {
            // The sound stays; it is now unsaved.
            currentIndex = -1;
            stickyModified = true;
        }
        else if (currentIndex > index)
        {
            // indices shifted; rescan already resolved user presets by file
        }
        sendChangeMessage();
        return Result::ok;
    }

    //==========================================================================
    void PresetManager::writeToState (juce::ValueTree& s) const
    {
        juce::String kind = "none";
        if (currentIndex == 0) kind = "init";
        else if (isCurrentFactory()) kind = "factory";
        else if (isCurrentUser()) kind = "user";
        s.setProperty ("presetName", currentName, nullptr);
        s.setProperty ("presetCategory", currentCategory, nullptr);
        s.setProperty ("presetKind", kind, nullptr);
        if (isCurrentUser())
            s.setProperty ("presetFile", presets[(size_t) currentIndex].file.getFullPathName(), nullptr);
        else if (pendingUserFile != juce::File())
            s.setProperty ("presetFile", pendingUserFile.getFullPathName(), nullptr);
        s.setProperty ("presetModified", isModified(), nullptr);
    }

    void PresetManager::stripStateProperties (juce::ValueTree& s)
    {
        for (auto* p : { "presetName", "presetCategory", "presetKind", "presetFile", "presetModified" })
            s.removeProperty (p, nullptr);
    }

    void PresetManager::restoreFromState (const juce::ValueTree& s)
    {
        pendingUserFile = juce::File();
        if (! s.hasProperty ("presetName"))
        {
            // Session from M1-M4: no preset info. Show "Init"; the modified
            // flag then tells whether the sound differs from the defaults.
            setCurrent (0, "Init", "Init");
            for (int i = 0; i < params.size(); ++i)
                snapshot[(size_t) i] = params[i]->getDefaultValue();
            stickyModified = false;
            sendChangeMessage();
            return;
        }

        const auto name = s.getProperty ("presetName").toString();
        const auto category = s.getProperty ("presetCategory").toString();
        const auto kind = s.getProperty ("presetKind").toString();
        int index = -1;
        if (kind == "init")
            index = 0;
        else if (kind == "factory")
        {
            for (size_t i = 0; i < factory.size(); ++i)
                if (factory[i].info.name == name && factory[i].info.category == category)
                    index = 1 + (int) i;
        }
        else if (kind == "user")
        {
            const juce::File f (s.getProperty ("presetFile").toString());
            index = findUserPreset (f);
            if (index < 0) pendingUserFile = f;   // resolved by the next rescan (no I/O here)
        }
        currentIndex = index;
        currentName = name.isNotEmpty() ? name : juce::String ("Init");
        currentCategory = category;
        takeSnapshot();
        stickyModified = (bool) s.getProperty ("presetModified", false);
        sendChangeMessage();
    }
}
