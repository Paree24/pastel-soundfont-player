#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PastelLookAndFeel.h"
#include "PlayerPresets.h"

class PastelProcessor;

// Preset browser overlay (shared design, adapted from the ERSA / Midi Note
// Modifier browser): search + All/Factory/User filter + audition list.
// Clicking a row loads it, double-click or a click on the scrim closes.
// Inline name field + Save; saving over an existing preset needs a second click.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    PresetBrowser(PastelProcessor& p, PastelLookAndFeel& lnf);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onClose;

private:
    struct Row { bool isUser = false; juce::String name; juce::File file; };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void rebuildRows();
    void refreshBankButtons();
    void clickSave();
    void setOverwriteArmed(bool on);

    PastelProcessor& proc;
    PastelLookAndFeel& lnf;
    juce::TextEditor search, nameBox;
    bool overwriteArmed = false;
    juce::ListBox list { "presets", this };
    juce::TextButton closeBtn { "X" }, saveBtn { "SAVE" }, delBtn { "DELETE" };
    juce::TextButton bankBtns[3] { juce::TextButton { "ALL" }, juce::TextButton { "FACTORY" }, juce::TextButton { "USER" } };
    std::vector<Row> rows;
    int activeBank = 0; // 0 all, 1 factory, 2 user
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
