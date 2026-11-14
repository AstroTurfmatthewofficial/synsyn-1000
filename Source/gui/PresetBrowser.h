#pragma once

#include "Components.h"
#include "presets/PresetManager.h"

// Preset UI (M5), SynprezFM "Memory" style:
//  - MemorySection: bank (category filter) LCD, patch LCD with number,
//    category/kind LCD with an EDITED lamp, prev/next, List, Init, Save,
//    Save As, Rename, Delete.
//  - PresetBrowserPanel: in-editor overlay with category tabs and a grid of
//    preset cells (click = load, double-click = load + close).
//  - PresetDialog: in-editor modal overlay for Save As / Rename / confirm.
// Overlays live inside the editor (no extra top-level windows), so they work
// in every host and scale with the editor.
namespace vf::gui
{
    class PresetUi;

    // A clickable LCD cell (optionally with a label on the right and an
    // orange badge LCD, e.g. the patch number).
    class LcdButton : public juce::Component
    {
    public:
        LcdButton (juce::String label = {}, float lcdFraction = 0.72f);
        std::function<juce::String()> getText;
        std::function<juce::String()> getBadge;
        std::function<bool()> isLit;
        std::function<void()> onClick, onDoubleClick;
        juce::String tooltipLabel;

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    private:
        juce::String label;
        float lcdFraction;
    };

    // Hollow block arrow button (as on the keyboard strip).
    class ArrowButton final : public juce::Button
    {
    public:
        explicit ArrowButton (bool pointsRight);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        bool right;
    };

    //==========================================================================
    class MemorySection final : public SectionPanel, private juce::ChangeListener, private juce::Timer
    {
    public:
        MemorySection (vf::presets::PresetManager&, PresetUi&);
        ~MemorySection() override;
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { updateButtons(); repaint(); }
        void timerCallback() override;
        void updateButtons();
        void repaintAll();

        vf::presets::PresetManager& presets;
        PresetUi& ui;
        LcdButton bank { "Bank", 0.62f }, patch { "Patch", 0.80f }, info {};
        ArrowButton prev { false }, next { true };
        juce::TextButton list { "List" }, init { "Init" }, save { "Save" }, saveAs { "Save As" },
                         rename { "Rename" }, del { "Delete" };
        juce::Rectangle<int> lampArea;
        bool lastModified = false;
        juce::String lastStatus;
    };

    //==========================================================================
    class PresetBrowserPanel final : public SectionPanel, private juce::ChangeListener
    {
    public:
        PresetBrowserPanel (vf::presets::PresetManager&, PresetUi&);
        ~PresetBrowserPanel() override;
        void resized() override;
        void refresh();
        int getNumVisibleCells() const;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

        class Grid;
        vf::presets::PresetManager& presets;
        PresetUi& ui;
        juce::OwnedArray<LcdButton> tabs;
        juce::TextButton close { "Close" }, rescan { "Rescan" }, openFolder { "Open Folder" };
        juce::Label footer;
        juce::Viewport viewport;
        std::unique_ptr<Grid> grid;
    };

    //==========================================================================
    class PresetDialog final : public juce::Component
    {
    public:
        enum class Mode { saveAs, rename, confirm };
        struct Result { juce::String name, category, author; };

        PresetDialog (Mode, juce::String title, juce::String message, juce::String okText);
        std::function<void (const Result&)> onOk;
        std::function<void()> onCancel;

        void setFields (const juce::String& name, const juce::String& category, const juce::String& author);
        void setError (const juce::String& e) { error = e; repaint(); }
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks on the backdrop
        void focusFirstField();

        // For tests.
        juce::TextEditor& getNameEditor() noexcept { return nameEditor; }
        void clickOk() { if (ok.onClick) ok.onClick(); }   // synchronous (triggerClick is async)

    private:
        juce::Rectangle<int> boxBounds() const;
        Mode mode;
        juce::String title, message, error;
        juce::TextEditor nameEditor, authorEditor;
        juce::OwnedArray<LcdButton> categoryCells;
        juce::String category { "User" };
        juce::TextButton ok, cancel { "Cancel" };
    };

    //==========================================================================
    // Glue between the Memory section, the overlays and the PresetManager.
    // Owned by the editor's main panel; overlays are added to `host`.
    class PresetUi
    {
    public:
        PresetUi (vf::presets::PresetManager&, juce::Component& host);
        ~PresetUi();

        juce::String filter { "All" };
        void setFilter (const juce::String&);

        void toggleBrowser()                 { showBrowser (! isBrowserVisible()); }
        void showBrowser (bool);
        bool isBrowserVisible() const        { return browser != nullptr && browser->isVisible(); }
        PresetBrowserPanel* getBrowser()     { return browser.get(); }
        PresetDialog* getDialog()            { return dialog.get(); }

        void step (int delta);
        void loadIndex (int index, bool closeBrowser);
        void saveClicked();
        void saveAsClicked();
        void renameClicked();
        void deleteClicked();
        void initClicked();

        void flash (const juce::String& message);   // short status in the Memory LCD
        juce::String getStatus() const;

        void setOverlayAreas (juce::Rectangle<int> browserArea, juce::Rectangle<int> fullArea);

    private:
        void openDialog (std::unique_ptr<PresetDialog>);
        void closeDialog();
        void doSaveAs (const PresetDialog::Result&, bool overwrite);

        vf::presets::PresetManager& presets;
        juce::Component& host;
        std::unique_ptr<PresetBrowserPanel> browser;
        std::unique_ptr<PresetDialog> dialog, retired;
        juce::Rectangle<int> browserArea, fullArea;
        juce::String status;
        juce::uint32 statusTime = 0;
    };
}
