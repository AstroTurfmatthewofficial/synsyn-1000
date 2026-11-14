#include "PresetBrowser.h"

namespace vf::gui
{
    using vf::presets::PresetManager;

    static juce::String numberFor (const PresetManager& pm, int index)
    {
        if (index < 0) return "--";
        if (index <= pm.getNumFactoryPresets()) return juce::String (index).paddedLeft ('0', 2);
        return "U" + juce::String (index - pm.getNumFactoryPresets());
    }

    //==========================================================================
    LcdButton::LcdButton (juce::String l, float frac) : label (std::move (l)), lcdFraction (frac)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void LcdButton::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool hot = isMouseOver (true) && isEnabled();
        ForgeLookAndFeel::drawCell (g, r, hot);
        auto inner = r.reduced (3.0f);
        auto lcd = label.isNotEmpty() ? inner.removeFromLeft (inner.getWidth() * lcdFraction) : inner;

        const auto badge = getBadge ? getBadge() : juce::String();
        if (badge.isNotEmpty())
        {
            auto b = lcd.removeFromRight (juce::jmax (30.0f, lcd.getHeight() * 1.45f));
            lcd.removeFromRight (2.0f);
            g.setColour (juce::Colour (0xff111111));
            g.fillRect (b);
            g.setColour (colours::accent.withAlpha (0.9f));
            g.fillRect (b.reduced (2.0f));
            g.setColour (colours::lcdText);
            g.setFont (ForgeLookAndFeel::lcdFont (juce::jmin (b.getHeight() * 0.8f, 20.0f)));
            g.drawFittedText (badge, b.reduced (2.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        ForgeLookAndFeel::drawLcd (g, lcd, getText ? getText() : juce::String(), juce::Justification::centredLeft,
                                   isLit ? isLit() : false);
        if (label.isNotEmpty())
        {
            g.setColour (hot ? colours::accent : colours::label);
            g.setFont (ForgeLookAndFeel::uiFont (12.0f, true));
            g.drawFittedText (label, inner.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, 1);
        }
        if (! isEnabled())
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (r);
        }
    }

    void LcdButton::mouseUp (const juce::MouseEvent& e)
    {
        if (isEnabled() && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    void LcdButton::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (isEnabled() && onDoubleClick) onDoubleClick();
    }

    //==========================================================================
    ArrowButton::ArrowButton (bool pointsRight)
        : juce::Button (pointsRight ? "Next preset" : "Previous preset"), right (pointsRight) {}

    void ArrowButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        ForgeLookAndFeel::drawCell (g, r, highlighted || down, down ? colours::accent : colours::outline);
        auto a = r.reduced (r.getWidth() * 0.2f, r.getHeight() * 0.25f);
        juce::Path p;
        if (right) p.addArrow ({ a.getX(), a.getCentreY(), a.getRight(), a.getCentreY() }, 4.0f, a.getHeight(), a.getWidth() * 0.5f);
        else       p.addArrow ({ a.getRight(), a.getCentreY(), a.getX(), a.getCentreY() }, 4.0f, a.getHeight(), a.getWidth() * 0.5f);
        g.setColour (highlighted || down ? colours::accent : colours::trace);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    //==========================================================================
    MemorySection::MemorySection (PresetManager& pm, PresetUi& u) : SectionPanel ("Memory"), presets (pm), ui (u)
    {
        bank.getText = [this] { return ui.filter.toUpperCase(); };
        bank.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "All", true, ui.filter == "All");
            for (int i = 0; i < PresetManager::categories().size(); ++i)
                m.addItem (2 + i, PresetManager::categories()[i], true, ui.filter == PresetManager::categories()[i]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bank),
                             [this] (int r)
                             {
                                 if (r == 1) ui.setFilter ("All");
                                 else if (r >= 2) ui.setFilter (PresetManager::categories()[r - 2]);
                                 repaintAll();
                             });
        };
        patch.getText = [this] { return presets.getCurrentName().toUpperCase(); };
        patch.getBadge = [this] { return numberFor (presets, presets.getCurrentIndex()); };
        patch.onClick = [this] { ui.toggleBrowser(); };
        info.getText = [this]
        {
            if (auto s = ui.getStatus(); s.isNotEmpty()) return s.toUpperCase();
            const int idx = presets.getCurrentIndex();
            juce::String kind = idx == 0 ? "DEFAULT" : (presets.isCurrentFactory() ? "FACTORY" : (presets.isCurrentUser() ? "USER" : "UNSAVED"));
            return presets.getCurrentCategory().toUpperCase() + " / " + kind;
        };
        info.onClick = [this] { ui.toggleBrowser(); };

        prev.onClick = [this] { ui.step (-1); };
        next.onClick = [this] { ui.step (+1); };
        list.setClickingTogglesState (false);
        list.onClick = [this] { ui.toggleBrowser(); };
        init.onClick = [this] { ui.initClicked(); };
        save.onClick = [this] { ui.saveClicked(); };
        saveAs.onClick = [this] { ui.saveAsClicked(); };
        rename.onClick = [this] { ui.renameClicked(); };
        del.onClick = [this] { ui.deleteClicked(); };

        prev.setTooltip ("Previous preset in bank");
        next.setTooltip ("Next preset in bank");
        save.setTooltip ("Save over the current user preset (factory presets: Save As)");
        rename.setTooltip ("Rename the current user preset");
        del.setTooltip ("Delete the current user preset");

        for (auto* c : std::initializer_list<juce::Component*> { &bank, &patch, &info, &prev, &next, &list, &init, &save,
                                                                 &saveAs, &rename, &del })
            addAndMakeVisible (c);

        presets.addChangeListener (this);
        updateButtons();
        startTimerHz (8);
    }

    MemorySection::~MemorySection()
    {
        presets.removeChangeListener (this);
    }

    void MemorySection::updateButtons()
    {
        const bool user = presets.isCurrentUser();
        rename.setEnabled (user);
        del.setEnabled (user);
    }

    void MemorySection::repaintAll()
    {
        bank.repaint(); patch.repaint(); info.repaint(); repaint (lampArea);
    }

    void MemorySection::timerCallback()
    {
        const bool m = presets.isModified();
        const auto s = ui.getStatus();
        if (m != lastModified || s != lastStatus)
        {
            lastModified = m;
            lastStatus = s;
            info.repaint();
            repaint (lampArea);
        }
    }

    void MemorySection::resized()
    {
        auto r = getBodyBounds();
        const int h = r.getHeight() / 4;
        auto row1 = r.removeFromTop (h), row2 = r.removeFromTop (h), row3 = r.removeFromTop (h), row4 = r;

        const int arrowW = 34;
        next.setBounds (row1.removeFromRight (arrowW).reduced (1));
        row1.removeFromRight (1);
        prev.setBounds (row1.removeFromRight (arrowW).reduced (1));
        bank.setBounds (row1);

        list.setBounds (row2.removeFromRight (arrowW * 2 + 1).reduced (1));
        patch.setBounds (row2);

        lampArea = row3.removeFromRight (arrowW * 2 + 1).reduced (1);
        info.setBounds (row3);

        juce::Component* btns[] = { &init, &save, &saveAs, &rename, &del };
        const float weights[] = { 0.8f, 0.9f, 1.25f, 1.2f, 1.1f };
        float total = 0; for (float w : weights) total += w;
        const int w0 = row4.getWidth();
        for (int i = 0; i < 5; ++i)
        {
            const int w = i == 4 ? row4.getWidth() : juce::roundToInt ((float) w0 * weights[i] / total);
            btns[i]->setBounds (row4.removeFromLeft (w).reduced (1));
        }
    }

    void MemorySection::paint (juce::Graphics& g)
    {
        SectionPanel::paint (g);
        // EDITED lamp: lit (orange) when parameters differ from the loaded preset.
        auto r = lampArea.toFloat().reduced (1.0f);
        const bool m = presets.isModified();
        ForgeLookAndFeel::drawCell (g, r, false, m ? colours::accent : colours::outlineDim);
        auto lamp = r.reduced (3.0f);
        g.setColour (m ? colours::accent : juce::Colour (0xff1a1a1a));
        g.fillRect (lamp);
        g.setColour (m ? colours::lcdText : colours::label.withAlpha (0.35f));
        g.setFont (ForgeLookAndFeel::lcdFont (juce::jmin (lamp.getHeight() * 0.75f, 15.0f)));
        g.drawText ("EDITED", lamp, juce::Justification::centred);
    }

    //==========================================================================
    class PresetBrowserPanel::Grid final : public juce::Component
    {
    public:
        Grid (PresetManager& pm, PresetUi& u) : presets (pm), ui (u) {}

        void rebuild (int width)
        {
            cells.clear();
            indices = presets.getIndicesInCategory (ui.filter);
            for (int idx : indices)
            {
                auto* c = cells.add (new LcdButton ({}, 1.0f));
                const auto info = presets.getPresets()[(size_t) idx];
                c->getText = [info] { return info.name.toUpperCase(); };
                c->getBadge = [this, idx] { return numberFor (presets, idx); };
                c->isLit = [this, idx] { return presets.getCurrentIndex() == idx; };
                c->onClick = [this, idx] { ui.loadIndex (idx, false); };
                c->onDoubleClick = [this, idx] { ui.loadIndex (idx, true); };
                c->tooltipLabel = info.category;
                addAndMakeVisible (c);
            }
            layout (width);
        }

        void layout (int width)
        {
            const int cols = juce::jmax (1, width / 220);
            const int cw = width / cols, ch = 30;
            const int rows = ((int) cells.size() + cols - 1) / cols;
            setSize (width, juce::jmax (1, rows * ch));
            for (int i = 0; i < cells.size(); ++i)
                cells[i]->setBounds ((i % cols) * cw, (i / cols) * ch, cw, ch);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // Small category tag and a "USER" marker inside each cell.
            g.setFont (ForgeLookAndFeel::uiFont (10.5f, true));
            for (int i = 0; i < cells.size(); ++i)
            {
                const auto& info = presets.getPresets()[(size_t) indices[(size_t) i]];
                auto r = cells[i]->getBounds().reduced (6, 4);
                r.removeFromRight (juce::roundToInt (juce::jmax (30.0f, (float) cells[i]->getHeight() * 1.45f)) - 2);
                g.setColour (info.isFactory ? colours::lcdText.withAlpha (0.45f) : colours::accent.darker (0.3f));
                g.drawText ((info.isFactory ? info.category : "USER " + info.category).toUpperCase(), r.reduced (4, 0),
                            juce::Justification::centredRight);
            }
        }

        int numCells() const { return cells.size(); }

    private:
        PresetManager& presets;
        PresetUi& ui;
        juce::OwnedArray<LcdButton> cells;
        std::vector<int> indices;
    };

    PresetBrowserPanel::PresetBrowserPanel (PresetManager& pm, PresetUi& u)
        : SectionPanel ("Preset Browser"), presets (pm), ui (u), grid (std::make_unique<Grid> (pm, u))
    {
        juce::StringArray names { "All" };
        names.addArray (PresetManager::categories());
        for (auto& n : names)
        {
            auto* t = tabs.add (new LcdButton ({}, 1.0f));
            t->getText = [n] { return n.toUpperCase(); };
            t->isLit = [this, n] { return ui.filter == n; };
            t->onClick = [this, n] { ui.setFilter (n); };
            addAndMakeVisible (t);
        }
        close.onClick = [this] { ui.showBrowser (false); };
        rescan.onClick = [this] { presets.rescanUserPresets(); ui.flash ("Rescanned"); };
        openFolder.onClick = [this]
        {
            auto root = presets.getUserRoot();
            root.createDirectory();
            root.startAsProcess();
        };
        footer.setFont (ForgeLookAndFeel::uiFont (11.5f));
        footer.setColour (juce::Label::textColourId, colours::headerText.withAlpha (0.8f));
        for (auto* c : std::initializer_list<juce::Component*> { &close, &rescan, &openFolder, &footer, &viewport })
            addAndMakeVisible (c);
        viewport.setViewedComponent (grid.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        presets.addChangeListener (this);
    }

    PresetBrowserPanel::~PresetBrowserPanel()
    {
        presets.removeChangeListener (this);
    }

    int PresetBrowserPanel::getNumVisibleCells() const { return grid->numCells(); }

    void PresetBrowserPanel::refresh()
    {
        grid->rebuild (juce::jmax (100, viewport.getMaximumVisibleWidth()));
        for (auto* t : tabs) t->repaint();
        int user = 0;
        for (auto& p : presets.getPresets()) user += p.isFactory ? 0 : 1;
        juce::String text = "User folder: " + presets.getUserRoot().getFullPathName()
                          + "   |   " + juce::String (presets.getNumFactoryPresets()) + " factory (read-only), "
                          + juce::String (user) + " user";
        if (presets.getNumUnreadableFiles() > 0)
            text << "   |   " << presets.getNumUnreadableFiles() << " unreadable file(s) skipped";
        footer.setText (text, juce::dontSendNotification);
        repaint();
    }

    void PresetBrowserPanel::resized()
    {
        auto r = getBodyBounds().reduced (2);
        auto top = r.removeFromTop (30);
        close.setBounds (top.removeFromRight (80).reduced (1));
        top.removeFromRight (6);
        const int tw = top.getWidth() / tabs.size();
        for (auto* t : tabs) t->setBounds (top.removeFromLeft (tw));
        r.removeFromTop (4);
        auto bottom = r.removeFromBottom (26);
        openFolder.setBounds (bottom.removeFromRight (110).reduced (1));
        rescan.setBounds (bottom.removeFromRight (80).reduced (1));
        footer.setBounds (bottom);
        r.removeFromBottom (4);
        viewport.setBounds (r);
        refresh();
    }

    //==========================================================================
    PresetDialog::PresetDialog (Mode m, juce::String t, juce::String msg, juce::String okText)
        : mode (m), title (std::move (t)), message (std::move (msg)), ok (okText)
    {
        auto style = [] (juce::TextEditor& e)
        {
            e.setColour (juce::TextEditor::backgroundColourId, colours::lcdBack);
            e.setColour (juce::TextEditor::textColourId, colours::lcdText);
            e.setColour (juce::TextEditor::outlineColourId, colours::outline);
            e.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
            e.setColour (juce::TextEditor::highlightColourId, colours::outline.withAlpha (0.5f));
            e.setColour (juce::CaretComponent::caretColourId, colours::lcdText);
            e.setFont (ForgeLookAndFeel::lcdFont (17.0f));
            e.setIndents (6, 5);
            e.setSelectAllWhenFocused (true);
        };
        style (nameEditor);
        style (authorEditor);
        nameEditor.setInputRestrictions (PresetManager::maxNameLength);
        authorEditor.setInputRestrictions (40);
        nameEditor.onReturnKey = [this] { ok.triggerClick(); };
        authorEditor.onReturnKey = [this] { ok.triggerClick(); };
        nameEditor.onEscapeKey = authorEditor.onEscapeKey = [this] { cancel.triggerClick(); };

        ok.onClick = [this]
        {
            Result r { nameEditor.getText().trim(), category, authorEditor.getText().trim() };
            if (mode != Mode::confirm && ! PresetManager::isValidName (r.name))
            {
                setError ("Please enter a name (1-32 characters, no / \\ : * ? \" < > |).");
                return;
            }
            if (onOk) onOk (r);
        };
        cancel.onClick = [this] { if (onCancel) onCancel(); };

        if (mode == Mode::saveAs)
        {
            for (auto& c : PresetManager::categories())
            {
                auto* cell = categoryCells.add (new LcdButton ({}, 1.0f));
                cell->getText = [c] { return c.toUpperCase(); };
                cell->isLit = [this, c] { return category == c; };
                cell->onClick = [this, c] { category = c; for (auto* x : categoryCells) x->repaint(); };
                addAndMakeVisible (cell);
            }
        }
        if (mode != Mode::confirm) addAndMakeVisible (nameEditor);
        if (mode == Mode::saveAs) addAndMakeVisible (authorEditor);
        addAndMakeVisible (ok);
        addAndMakeVisible (cancel);
        setWantsKeyboardFocus (false);
        setInterceptsMouseClicks (true, true);
    }

    void PresetDialog::setFields (const juce::String& name, const juce::String& cat, const juce::String& author)
    {
        nameEditor.setText (name, false);
        authorEditor.setText (author, false);
        if (PresetManager::categories().contains (cat)) category = cat;
        for (auto* x : categoryCells) x->repaint();
    }

    void PresetDialog::focusFirstField()
    {
        if (mode != Mode::confirm && isShowing())
            nameEditor.grabKeyboardFocus();
    }

    juce::Rectangle<int> PresetDialog::boxBounds() const
    {
        const int w = 560, h = mode == Mode::saveAs ? 250 : (mode == Mode::rename ? 170 : 160);
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (w, getWidth() - 20), juce::jmin (h, getHeight() - 20));
    }

    void PresetDialog::resized()
    {
        auto box = boxBounds();
        auto r = box.withTrimmedTop (SectionPanel::headerHeight + 2).reduced (12, 10);
        auto buttons = r.removeFromBottom (30);
        cancel.setBounds (buttons.removeFromRight (100).reduced (1));
        buttons.removeFromRight (8);
        ok.setBounds (buttons.removeFromRight (110).reduced (1));
        r.removeFromBottom (24);   // error line

        auto fieldRow = [&] (juce::Component& c)
        {
            auto row = r.removeFromTop (30);
            row.removeFromLeft (80);   // label column
            c.setBounds (row);
            r.removeFromTop (8);
        };
        if (mode == Mode::confirm) return;
        fieldRow (nameEditor);
        if (mode == Mode::saveAs)
        {
            auto row = r.removeFromTop (30);
            row.removeFromLeft (80);
            const int cw = row.getWidth() / juce::jmax (1, categoryCells.size());
            for (auto* c : categoryCells) c->setBounds (row.removeFromLeft (cw));
            r.removeFromTop (8);
            fieldRow (authorEditor);
        }
    }

    void PresetDialog::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        auto box = boxBounds().toFloat();
        auto header = box.removeFromTop ((float) SectionPanel::headerHeight);
        g.setColour (colours::header);
        g.fillRect (header);
        g.setColour (colours::accent);
        g.drawRect (header, 1.0f);
        g.setColour (colours::headerText);
        g.setFont (ForgeLookAndFeel::uiFont (11.5f, true));
        g.drawText (title, header, juce::Justification::centred);
        box.removeFromTop (2.0f);
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRect (box);
        g.setColour (colours::accent.withAlpha (0.8f));
        g.drawRect (box, 1.0f);

        auto r = boxBounds().withTrimmedTop (SectionPanel::headerHeight + 2).reduced (12, 10);
        g.setFont (ForgeLookAndFeel::uiFont (12.5f, true));
        g.setColour (colours::label);
        if (mode == Mode::confirm)
        {
            g.drawFittedText (message, r.removeFromTop (70), juce::Justification::centredLeft, 4);
        }
        else
        {
            auto labelAt = [&] (const juce::String& text)
            {
                auto row = r.removeFromTop (30);
                g.drawText (text, row.removeFromLeft (76), juce::Justification::centredRight);
                r.removeFromTop (8);
            };
            labelAt ("Name");
            if (mode == Mode::saveAs) { labelAt ("Category"); labelAt ("Author"); }
        }
        if (error.isNotEmpty())
        {
            auto er = boxBounds().reduced (12, 10).removeFromBottom (54).removeFromTop (22);
            g.setColour (colours::accent);
            g.drawFittedText (error, er, juce::Justification::centredLeft, 1);
        }
    }

    //==========================================================================
    PresetUi::PresetUi (PresetManager& pm, juce::Component& h) : presets (pm), host (h) {}

    PresetUi::~PresetUi()
    {
        dialog.reset();
        retired.reset();
        browser.reset();
    }

    void PresetUi::setOverlayAreas (juce::Rectangle<int> b, juce::Rectangle<int> f)
    {
        browserArea = b;
        fullArea = f;
        if (browser != nullptr) browser->setBounds (browserArea);
        if (dialog != nullptr) dialog->setBounds (fullArea);
    }

    void PresetUi::setFilter (const juce::String& f)
    {
        filter = f;
        if (browser != nullptr) browser->refresh();
        host.repaint();
    }

    void PresetUi::showBrowser (bool show)
    {
        if (show)
        {
            presets.rescanUserPresets();   // message thread
            if (browser == nullptr)
                browser = std::make_unique<PresetBrowserPanel> (presets, *this);
            host.addAndMakeVisible (*browser);
            browser->setBounds (browserArea);
            browser->refresh();
            browser->toFront (false);
            if (dialog != nullptr) dialog->toFront (true);
        }
        else if (browser != nullptr)
        {
            browser->setVisible (false);
        }
    }

    void PresetUi::loadIndex (int index, bool closeBrowser)
    {
        if (! presets.loadPreset (index))
        {
            flash ("Load failed");
            presets.rescanUserPresets();
            if (browser != nullptr) browser->refresh();
            return;
        }
        if (closeBrowser) showBrowser (false);
    }

    void PresetUi::step (int delta)
    {
        if (! presets.step (delta, filter))
            flash ("Bank is empty");
    }

    void PresetUi::initClicked()
    {
        presets.loadInit();
    }

    void PresetUi::flash (const juce::String& m)
    {
        status = m;
        statusTime = juce::Time::getMillisecondCounter();
        host.repaint();
    }

    juce::String PresetUi::getStatus() const
    {
        if (status.isEmpty() || juce::Time::getMillisecondCounter() - statusTime > 2500) return {};
        return status;
    }

    void PresetUi::openDialog (std::unique_ptr<PresetDialog> d)
    {
        closeDialog();   // (may be called from the current dialog's callback: it is retired, not deleted)
        dialog = std::move (d);
        dialog->onCancel = [this] { closeDialog(); };
        host.addAndMakeVisible (*dialog);
        dialog->setBounds (fullArea);
        dialog->toFront (true);
        dialog->focusFirstField();
    }

    void PresetUi::closeDialog()
    {
        // Called from the dialog's own button callbacks: hide now, delete later.
        if (dialog != nullptr)
        {
            dialog->setVisible (false);
            retired = std::move (dialog);   // destroyed on the next open/close or with the editor
        }
    }

    void PresetUi::saveClicked()
    {
        if (presets.isCurrentUser())
        {
            const auto r = presets.saveCurrent();
            flash (r == PresetManager::Result::ok ? "Saved" : "Save failed");
            return;
        }
        saveAsClicked();   // factory / Init / unsaved: Save As (copy)
    }

    void PresetUi::saveAsClicked()
    {
        auto d = std::make_unique<PresetDialog> (PresetDialog::Mode::saveAs, "Save Preset As", juce::String(), "Save");
        auto cat = presets.getCurrentCategory();
        if (! PresetManager::categories().contains (cat)) cat = "User";
        d->setFields (presets.getCurrentName() == "Init" ? juce::String ("My Preset") : presets.getCurrentName(), cat,
                      presets.getLastAuthor());
        d->onOk = [this] (const PresetDialog::Result& r) { doSaveAs (r, false); };
        openDialog (std::move (d));
    }

    void PresetUi::doSaveAs (const PresetDialog::Result& r, bool overwrite)
    {
        const auto res = presets.saveAs (r.name, r.category, r.author, overwrite);
        if (res == PresetManager::Result::ok)
        {
            closeDialog();
            flash ("Saved");
            if (browser != nullptr) browser->refresh();
        }
        else if (res == PresetManager::Result::exists)
        {
            auto c = std::make_unique<PresetDialog> (PresetDialog::Mode::confirm, "Overwrite Preset",
                "A user preset named \"" + r.name + "\" already exists in " + r.category + ". Overwrite it?", "Overwrite");
            c->onOk = [this, r] (const PresetDialog::Result&) { doSaveAs (r, true); };
            openDialog (std::move (c));
        }
        else if (dialog != nullptr)
        {
            dialog->setError ("Could not save: " + presets.getLastError());
        }
    }

    void PresetUi::renameClicked()
    {
        if (! presets.isCurrentUser()) { flash ("Factory: read-only"); return; }
        const int idx = presets.getCurrentIndex();
        auto d = std::make_unique<PresetDialog> (PresetDialog::Mode::rename, "Rename Preset", juce::String(), "Rename");
        d->setFields (presets.getCurrentName(), presets.getCurrentCategory(), {});
        d->onOk = [this, idx] (const PresetDialog::Result& r)
        {
            const auto res = presets.renamePreset (idx, r.name);
            if (res == PresetManager::Result::ok) { closeDialog(); flash ("Renamed"); }
            else if (dialog != nullptr) dialog->setError (res == PresetManager::Result::exists
                                                              ? juce::String ("A preset with that name already exists.")
                                                              : "Could not rename: " + presets.getLastError());
        };
        openDialog (std::move (d));
    }

    void PresetUi::deleteClicked()
    {
        if (! presets.isCurrentUser()) { flash ("Factory: read-only"); return; }
        const int idx = presets.getCurrentIndex();
        const auto& info = presets.getPresets()[(size_t) idx];
        auto d = std::make_unique<PresetDialog> (PresetDialog::Mode::confirm, "Delete Preset",
            "Delete the user preset \"" + info.name + "\" (" + info.category + ")?\nThe file "
                + info.file.getFileName() + " will be removed from disk. This cannot be undone.", "Delete");
        d->onOk = [this, idx] (const PresetDialog::Result&)
        {
            const auto res = presets.deletePreset (idx);
            closeDialog();
            flash (res == PresetManager::Result::ok ? "Deleted" : "Delete failed");
            if (browser != nullptr) browser->refresh();
        };
        openDialog (std::move (d));
    }
}
