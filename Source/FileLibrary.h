#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

// ============================================================
// FileLibrary: folder shortcuts (persisted) + a simple file
// browser over the selected folder. No background scanning:
// the browser lists the current folder on demand.
// ============================================================
class FileLibrary : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::String path;
        juce::String name;
        bool isDir = false;
        bool isSf2 = false; // vs sfz
    };

    FileLibrary();
    ~FileLibrary() override = default;

    // shortcuts
    juce::StringArray getShortcuts() const;
    void addShortcut(const juce::String& path);
    void removeShortcut(int index);

    // browser state
    juce::String getCurrentDir() const { return currentDir; }
    void setCurrentDir(const juce::String& path);
    std::vector<Entry> listEntries(const juce::String& filter = "") const;
    static bool isSoundfontFile(const juce::File& f);

    juce::String getLastFile() const;
    void setLastFile(const juce::String& path);

    int getWindowW();
    int getWindowH();
    void setWindowSize(int w, int h);

private:
    void load();
    void save();
    void migrateLegacy();

    juce::ApplicationProperties props;
    juce::StringArray shortcuts;
    juce::String currentDir;
    juce::String lastFile = "";
};
