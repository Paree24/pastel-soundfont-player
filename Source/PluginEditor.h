#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"
#include "Parameters.h"
#include "PianoStrip.h"
#include "PresetBrowser.h"

class PastelEditor;

// File browser model: folders first, then soundfont files
class SoundListModel : public juce::ListBoxModel
{
public:
    void setEntries(const std::vector<FileLibrary::Entry>& e);
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    FileLibrary::Entry entryAt(int row) const;
    int indexOfPath(const juce::String& path) const;
private:
    std::vector<FileLibrary::Entry> entries;
};

// Slim determinate progress bar for background sample loads.
class LoadBar : public juce::Component
{
public:
    void setProgress(float p)
    {
        p = juce::jlimit(0.0f, 1.0f, p);
        if (std::abs(p - progress) > 0.005f) { progress = p; repaint(); }
    }
    void paint(juce::Graphics& g) override;
private:
    float progress = 0.0f;
};

class PastelEditor : public juce::AudioProcessorEditor,
                     public juce::ChangeListener,
                     public juce::Timer,
                     public juce::FileDragAndDropTarget
{
public:
    explicit PastelEditor(PastelProcessor&);
    ~PastelEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    // drop an .sfz / .sf2 / audio file anywhere on the editor to load it
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    using Attach = juce::AudioProcessorValueTreeState;

    PastelProcessor& proc;
    PastelLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 }; // makes every setTooltip() text show

    // header
    juce::Label logoLabel, fileLabel;
    juce::TextButton loadBtn { "LOAD" }, prevBtn { "<" }, nextBtn { ">" }, panicBtn { "PANIC" },
                     presetsBtn { "PRESETS" };
    juce::ComboBox bankBox, presetBox;
    juce::Label bankLabel { {}, "Bank" }, presetBoxLabel { {}, "Program" };

    // library card
    juce::GroupComponent libGroup { {}, "Browser" };
    juce::TextEditor searchBox;
    juce::ComboBox dirBox;
    juce::TextButton addDirBtn { "+ ADD" }, delDirBtn { "DEL" },
                     upBtn { "UP" }, refreshBtn { "SCAN" }, loadSelBtn { "LOAD >" };
    juce::Label pathLabel;
    LoadBar loadBar;
    juce::ListBox fileList;
    SoundListModel listModel;

    struct Knob
    {
        std::unique_ptr<juce::Slider> s;
        std::unique_ptr<juce::Label> l;
        std::unique_ptr<Attach::SliderAttachment> a;
    };
    struct Switch
    {
        std::unique_ptr<juce::ToggleButton> b;
        std::unique_ptr<Attach::ButtonAttachment> a;
    };
    struct Choice
    {
        std::unique_ptr<juce::ComboBox> c;
        std::unique_ptr<juce::Label> l;
        std::unique_ptr<Attach::ComboBoxAttachment> a;
    };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<std::unique_ptr<Choice>> choices;

    juce::GroupComponent envGroup { {}, "Envelope" };
    juce::GroupComponent filterGroup { {}, "Filter" };
    juce::GroupComponent lfoGroup { {}, "LFO -> Filter" };
    juce::GroupComponent fxGroup { {}, "Effects" };
    juce::GroupComponent arpGroup { {}, "Arpeggiator" };
    juce::GroupComponent voiceGroup { {}, "Voice" };

    juce::OwnedArray<juce::GroupComponent> fxCards;
    juce::Label limCap { {}, "LIMITER" }, monoCap { {}, "MONO" }, syncCap { {}, "SYNC" },
                latchCap { {}, "LATCH" }, velCap { {}, "VEL" };
    // arp + voice controls added after the original set (index-free)
    Knob* swingKnob = nullptr;
    Knob* glideKnob = nullptr;
    Switch* latchSwitch = nullptr;
    Switch* velSwitch = nullptr;
    Choice* voiceChoice = nullptr;
    Choice* interpChoice = nullptr;

    PianoStrip piano;
    PresetBrowser presetBrowser;
    bool dropHover = false;

    float meterL = 0.0f, meterR = 0.0f;
    juce::Rectangle<int> meterArea;
    int lastShownSwitch = -2;
    std::vector<SamplerEngine::Sf2Preset> presetList; // presetBox item i+1 -> entry i
    int shownBank = -1, shownProgram = -1;

    Knob* addKnob(const juce::String& paramId, const juce::String& title);
    Switch* addSwitch(const juce::String& paramId, const juce::String& title);
    Choice* addChoice(const juce::String& paramId, const juce::String& title,
                      const juce::StringArray& items);
    juce::GroupComponent* addFxCard(const juce::String& title, juce::Colour accent);
    void refreshShortcuts();
    void refreshFileList();
    void refreshPresetBoxes();
    void refreshFileLabel();
    void refreshPathLabel();
    void loadSelectedFile();
    void selectRelative(int delta);
    void paintMeter(juce::Graphics&, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PastelEditor)
};
