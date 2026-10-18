#include "PresetBrowser.h"
#include "PluginProcessor.h"

PresetBrowser::PresetBrowser(PastelProcessor& p, PastelLookAndFeel& l)
    : proc(p), lnf(l)
{
    search.setTextToShowWhenEmpty("Search presets...", PastelColors::textDim);
    search.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(search);

    for (int b = 0; b < 3; ++b)
    {
        bankBtns[b].setClickingTogglesState(false);
        bankBtns[b].onClick = [this, b] { activeBank = b; refreshBankButtons(); rebuildRows(); };
        addAndMakeVisible(bankBtns[b]);
    }
    refreshBankButtons();

    list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setRowHeight(26);
    addAndMakeVisible(list);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    nameBox.setTextToShowWhenEmpty("Preset name...", PastelColors::textDim);
    nameBox.onTextChange = [this] { setOverwriteArmed(false); };
    nameBox.onReturnKey = [this] { clickSave(); };
    addAndMakeVisible(nameBox);
    saveBtn.setTooltip("Save the loaded sound and every setting as a user preset");
    saveBtn.onClick = [this] { clickSave(); };
    delBtn.setTooltip("Delete the selected user preset");
    delBtn.onClick = [this]
    {
        int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size() || !rows[(size_t) r].isUser) return;
        if (PlayerPresets::deleteUserPreset(rows[(size_t) r].file))
            rebuildRows();
    };
    for (auto* b : { &closeBtn, &saveBtn, &delBtn })
        addAndMakeVisible(b);
    rebuildRows();
}

// Save (inline, no pop-up): an existing user preset needs a second click
// ("OVERWRITE?"); editing the name disarms it.
void PresetBrowser::clickSave()
{
    if (nameBox.getText().trim().isEmpty()) { nameBox.grabKeyboardFocus(); return; }
    auto name = PlayerPresets::sanitiseName(nameBox.getText());
    bool exists = PlayerPresets::getUserPresetDir().getChildFile(name + ".xml").existsAsFile();
    if (exists && !overwriteArmed) { setOverwriteArmed(true); return; }
    setOverwriteArmed(false);
    if (!PlayerPresets::writeUserPreset(name, proc.capturePreset())) return;
    proc.setPresetName(name);
    activeBank = 2;
    search.setText("", false);
    nameBox.setText(name, false);
    refreshBankButtons();
    rebuildRows();
}

void PresetBrowser::setOverwriteArmed(bool on)
{
    overwriteArmed = on;
    saveBtn.setButtonText(on ? "OVERWRITE?" : "SAVE");
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        nameBox.setText(proc.getPresetName() == "Init" ? juce::String() : proc.getPresetName(), false);
        setOverwriteArmed(false);
        rebuildRows();
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::refreshBankButtons()
{
    for (int b = 0; b < 3; ++b)
        bankBtns[b].setColour(juce::TextButton::textColourOffId,
                              b == activeBank ? PastelColors::tealBright : PastelColors::textDim);
}

void PresetBrowser::rebuildRows()
{
    rows.clear();
    juce::String q = search.getText().trim().toLowerCase();
    if (activeBank != 2 && (q.isEmpty() || juce::String("init").contains(q)))
        rows.push_back({ false, "Init", {} });
    if (activeBank != 1)
        for (auto& u : PlayerPresets::scanUserPresets())
            if (q.isEmpty() || u.name.toLowerCase().contains(q))
                rows.push_back({ true, u.name, u.file });
    list.deselectAllRows();
    list.updateContent();
    list.repaint();
    delBtn.setEnabled(false);
}

int PresetBrowser::getNumRows() { return (int) rows.size(); }

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];
    bool isCurrent = r.name == proc.getPresetName();
    if (selected)
    {
        g.setColour(PastelColors::tealDark.withAlpha(0.6f));
        g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 6.0f);
    }
    g.setFont(lnf.uiFont(10.5f));
    g.setColour(PastelColors::textDim);
    g.drawText(r.isUser ? "USER" : "FACTORY", 10, 0, 64, h, juce::Justification::centredLeft, false);
    g.setFont(lnf.titleFont(13.0f));
    g.setColour(isCurrent ? PastelColors::tealBright : PastelColors::text);
    g.drawText(r.name, 80, 0, w - 88, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::selectedRowsChanged(int)
{
    int r = list.getSelectedRow();
    delBtn.setEnabled(r >= 0 && r < (int) rows.size() && rows[(size_t) r].isUser);
    if (r < 0 || r >= (int) rows.size()) return;
    auto& row = rows[(size_t) r];
    if (row.isUser)
    {
        auto t = PlayerPresets::readUserPreset(row.file);
        if (t.isValid()) proc.applyPreset(t, row.name);
    }
    else proc.applyPreset({}, "Init");
    nameBox.setText(row.name == "Init" ? juce::String() : row.name, false);
    setOverwriteArmed(false);
    list.repaint();
}

void PresetBrowser::listBoxItemDoubleClicked(int, const juce::MouseEvent&)
{
    if (onClose) onClose();
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f)); // scrim
    g.setColour(PastelColors::panel);
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    g.setColour(PastelColors::panelEdge);
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 8.0f, 1.0f);
    g.setColour(PastelColors::text);
    g.setFont(lnf.titleFont(17.0f));
    g.drawText("PRESETS", panel.getX() + 16, panel.getY() + 10, 200, 26, juce::Justification::centredLeft, false);
    g.setColour(PastelColors::textDim);
    g.setFont(lnf.uiFont(12.0f));
    g.drawText("Sound file + all settings", panel.getX() + 108, panel.getY() + 10, 240, 26,
               juce::Justification::centredLeft, false);
}

void PresetBrowser::resized()
{
    const int pw = juce::jmin(560, getWidth() - 40), ph = juce::jmin(480, getHeight() - 80);
    panel = juce::Rectangle<int>(pw, ph).withCentre(getLocalBounds().getCentre());
    auto inner = panel.reduced(16, 12);
    auto top = inner.removeFromTop(26);
    closeBtn.setBounds(top.removeFromRight(32));
    inner.removeFromTop(10);
    search.setBounds(inner.removeFromTop(26));
    inner.removeFromTop(8);
    auto banks = inner.removeFromTop(26);
    for (int b = 0; b < 3; ++b)
    {
        bankBtns[b].setBounds(banks.removeFromLeft(90));
        banks.removeFromLeft(6);
    }
    inner.removeFromTop(8);
    auto bottom = inner.removeFromBottom(28);
    inner.removeFromBottom(10);
    list.setBounds(inner);
    delBtn.setBounds(bottom.removeFromRight(84));
    bottom.removeFromRight(6);
    saveBtn.setBounds(bottom.removeFromRight(104));
    bottom.removeFromRight(8);
    nameBox.setBounds(bottom);
}

void PresetBrowser::mouseDown(const juce::MouseEvent& e)
{
    if (!panel.contains(e.getPosition()) && onClose) onClose(); // scrim click closes
}
