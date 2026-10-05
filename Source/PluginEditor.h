#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"
#include "Parameters.h"
#include "PianoStrip.h"

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
                     public juce::Timer
{
public:
    explicit PastelEditor(PastelProcessor&);
    ~PastelEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

private:
    using Attach = juce::AudioProcessorValueTreeState;

    PastelProcessor& proc;
    PastelLookAndFeel lnf;

    // header
    juce::Label logoLabel, fileLabel;
    juce::TextButton loadBtn { "LOAD" }, prevBtn { "<" }, nextBtn { ">" }, panicBtn { "PANIC" };
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

    juce::OwnedArray<juce::GroupComponent> fxCards;
    juce::Label limCap { {}, "LIMITER" }, monoCap { {}, "MONO" };

    PianoStrip piano;

    float meterL = 0.0f, meterR = 0.0f;
    int lastShownSwitch = -2;

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
