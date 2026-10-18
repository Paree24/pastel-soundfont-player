#include "PlayerPresets.h"
#include <algorithm>

namespace PlayerPresets
{
static juce::File customUserDir;
static bool useCustomUserDir = false;

void setUserPresetDirForTest(const juce::File& dir) { customUserDir = dir; useCustomUserDir = true; }

juce::File getUserPresetDir()
{
    if (useCustomUserDir) return customUserDir;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Pastel SFZ Player Presets");
}

std::vector<UserPreset> scanUserPresets()
{
    std::vector<UserPreset> out;
    auto dir = getUserPresetDir();
    if (!dir.isDirectory()) return out;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.xml"))
        out.push_back({ f.getFileNameWithoutExtension(), f });
    std::sort(out.begin(), out.end(),
              [](const UserPreset& a, const UserPreset& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return out;
}

juce::String sanitiseName(juce::String name)
{
    name = name.trim().substring(0, 64);
    name = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-#");
    name = name.trim();
    return name.isEmpty() ? juce::String("Untitled") : name;
}

bool writeUserPreset(const juce::String& name, const juce::ValueTree& preset)
{
    auto dir = getUserPresetDir();
    if (!dir.isDirectory() && !dir.createDirectory()) return false;
    auto file = dir.getChildFile(sanitiseName(name) + ".xml");
    if (auto xml = preset.createXml())
        return xml->writeTo(file);
    return false;
}

juce::ValueTree readUserPreset(const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse(file))
        if (xml->hasTagName(kRootType.toString()))
            return juce::ValueTree::fromXml(*xml);
    return {};
}

bool deleteUserPreset(const juce::File& file)
{
    if (file.getParentDirectory() != getUserPresetDir()) return false; // safety: own dir only
    return file.existsAsFile() && file.deleteFile();
}
}
