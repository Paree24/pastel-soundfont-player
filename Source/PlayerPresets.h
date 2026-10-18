#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Plugin presets: the loaded sound file + every parameter. "Init" is the one
// factory entry (all defaults, keeps the current sound); user presets are XML
// files in Documents/Pastel SFZ Player Presets. Adapted from the ERSA / Midi
// Note Modifier preset files.
namespace PlayerPresets
{
    static const juce::Identifier kRootType { "PASTEL_PLAYER_PRESET" };

    struct UserPreset { juce::String name; juce::File file; };
    void setUserPresetDirForTest(const juce::File& dir); // harness override, else Documents
    juce::File getUserPresetDir();
    std::vector<UserPreset> scanUserPresets();
    juce::String sanitiseName(juce::String name);
    bool writeUserPreset(const juce::String& name, const juce::ValueTree& preset);
    juce::ValueTree readUserPreset(const juce::File& file); // invalid tree on failure
    bool deleteUserPreset(const juce::File& file);
}
