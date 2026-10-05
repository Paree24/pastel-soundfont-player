#include "FileLibrary.h"
#include <cstdlib>

FileLibrary::FileLibrary()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "PastelSoundfontPlayer";
    opts.filenameSuffix = ".settings";
    // test hook: isolated settings for the headless harness
    juce::String folder = "Pastel Soundfont Player";
    if (const char* e = std::getenv("PASTEL_SETTINGS_FOLDER")) if (*e != 0) folder = e;
    opts.folderName = folder;
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);
    load();
}

void FileLibrary::load()
{
    shortcuts.clear();
    if (auto* f = props.getUserSettings())
    {
        for (int i = 0; i < 64; ++i)
        {
            auto d = f->getValue("shortcut" + juce::String(i), f->getValue("libdir" + juce::String(i), ""));
            if (d.isEmpty()) break;
            if (juce::File(d).isDirectory() && !shortcuts.contains(d)) shortcuts.add(d);
        }
        currentDir = f->getValue("currentDir", "");
        lastFile = f->getValue("lastFile", "");
        if (shortcuts.isEmpty() && lastFile.isEmpty())
            migrateLegacy(); // one-time move from the old "PastelAudio" folder
        if (!juce::File(currentDir).isDirectory())
        {
            if (juce::File(lastFile).existsAsFile()) currentDir = juce::File(lastFile).getParentDirectory().getFullPathName();
            else if (shortcuts.size() > 0) currentDir = shortcuts[0];
            else currentDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName();
        }
    }
}

void FileLibrary::migrateLegacy()
{
    auto legacy = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("PastelAudio/PastelSoundfontPlayer.settings");
    if (!legacy.existsAsFile()) return;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "PastelSoundfontPlayer";
    juce::PropertiesFile pf(legacy, opts);
    for (int i = 0; i < 64; ++i)
    {
        auto d = pf.getValue("shortcut" + juce::String(i), pf.getValue("libdir" + juce::String(i), ""));
        if (d.isEmpty()) break;
        if (juce::File(d).isDirectory() && !shortcuts.contains(d)) shortcuts.add(d);
    }
    auto cd = pf.getValue("currentDir", ""), lf = pf.getValue("lastFile", "");
    if (juce::File(cd).isDirectory()) currentDir = cd;
    if (juce::File(lf).existsAsFile()) lastFile = lf;
    if (!shortcuts.isEmpty() || lastFile.isNotEmpty()) save();
}

void FileLibrary::save()
{
    if (auto* f = props.getUserSettings())
    {
        for (int i = 0; i < 64; ++i) { f->removeValue("shortcut" + juce::String(i)); f->removeValue("libdir" + juce::String(i)); }
        for (int i = 0; i < shortcuts.size(); ++i) f->setValue("shortcut" + juce::String(i), shortcuts[i]);
        f->setValue("currentDir", currentDir);
        f->setValue("lastFile", lastFile);
        f->saveIfNeeded();
    }
}

juce::StringArray FileLibrary::getShortcuts() const { return shortcuts; }

void FileLibrary::addShortcut(const juce::String& path)
{
    juce::File d(path);
    if (!d.isDirectory()) return;
    auto full = d.getFullPathName();
    if (!shortcuts.contains(full))
    {
        shortcuts.add(full);
        save();
        sendChangeMessage();
    }
}

void FileLibrary::removeShortcut(int index)
{
    if (index >= 0 && index < shortcuts.size())
    {
        shortcuts.remove(index);
        save();
        sendChangeMessage();
    }
}

void FileLibrary::setCurrentDir(const juce::String& path)
{
    if (juce::File(path).isDirectory() && path != currentDir)
    {
        currentDir = juce::File(path).getFullPathName();
        save();
        sendChangeMessage();
    }
}

bool FileLibrary::isSoundfontFile(const juce::File& f)
{
    auto e = f.getFileExtension().toLowerCase();
    return e == ".sf2" || e == ".sfz" || e == ".sf3";
}

std::vector<FileLibrary::Entry> FileLibrary::listEntries(const juce::String& filter) const
{
    std::vector<Entry> out;
    juce::File dir(currentDir);
    if (!dir.isDirectory()) return out;
    juce::Array<juce::File> children;
    dir.findChildFiles(children, juce::File::findFilesAndDirectories, false);
    juce::String flt = filter.trim();
    for (auto& c : children)
    {
        if (c.isDirectory())
        {
            if (c.getFileName().startsWith(".")) continue;
            if (flt.isNotEmpty() && !c.getFileName().containsIgnoreCase(flt)) continue;
            out.push_back({ c.getFullPathName(), c.getFileName(), true, false });
        }
        else if (isSoundfontFile(c))
        {
            if (flt.isNotEmpty() && !c.getFileName().containsIgnoreCase(flt)) continue;
            auto e = c.getFileExtension().toLowerCase();
            out.push_back({ c.getFullPathName(), c.getFileName(), false, e != ".sfz" });
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b)
    {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    return out;
}

juce::String FileLibrary::getLastFile() const { return lastFile; }

void FileLibrary::setLastFile(const juce::String& path)
{
    lastFile = path;
    save();
}

int FileLibrary::getWindowW()
{
    if (auto* f = props.getUserSettings()) return f->getIntValue("winW", 1180);
    return 1180;
}

int FileLibrary::getWindowH()
{
    if (auto* f = props.getUserSettings()) return f->getIntValue("winH", 700);
    return 700;
}

void FileLibrary::setWindowSize(int w, int h)
{
    if (auto* f = props.getUserSettings())
    {
        f->setValue("winW", w);
        f->setValue("winH", h);
        f->saveIfNeeded();
    }
}
