#include "Parameters.h"

using APVTS = juce::AudioProcessorValueTreeState;

static std::unique_ptr<juce::RangedAudioParameter> floatParam(const char* id, const char* name,
    float lo, float hi, float def, const char* unit = "")
{
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(id, 1), name,
        juce::NormalisableRange<float>(lo, hi), def, unit);
}

static std::unique_ptr<juce::RangedAudioParameter> choiceParam(const char* id, const char* name,
    std::initializer_list<const char*> choices, int def)
{
    juce::StringArray arr;
    for (auto c : choices) arr.add(c);
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(id, 1), name, arr, def);
}

static std::unique_ptr<juce::RangedAudioParameter> boolParam(const char* id, const char* name, bool def)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), name, def);
}

APVTS::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    // program
    p.push_back(floatParam(PP::PRESET, "Preset", 0, 127, 0));
    p.push_back(floatParam(PP::BANK, "Bank", 0, 127, 0));
    // ADSR (off by default: the soundfont plays as authored). Defaults are
    // neutral: fastest attack, full decay/sustain, tiny release.
    p.push_back(boolParam(PP::ADSRON, "ADSR On", false));
    p.push_back(floatParam(PP::ATK, "Attack", 0.001f, 4.0f, 0.001f, "s"));
    p.push_back(floatParam(PP::DEC, "Decay", 0.005f, 4.0f, 4.0f, "s"));
    p.push_back(floatParam(PP::SUS, "Sustain", 0.0f, 1.0f, 1.0f));
    p.push_back(floatParam(PP::REL, "Release", 0.01f, 8.0f, 0.01f, "s"));
    // filter (off by default: dry soundfont signal)
    p.push_back(boolParam(PP::FTON, "Filter On", false));
    p.push_back(choiceParam(PP::FTYPE, "Filter Type", { "Low Pass", "High Pass", "Band Pass" }, 0));
    p.push_back(floatParam(PP::FCUT, "Cutoff", 40.0f, 18000.0f, 18000.0f, "Hz"));
    p.push_back(floatParam(PP::FRES, "Resonance", 0.0f, 1.0f, 0.15f));
    p.push_back(floatParam(PP::FDRIVE, "Drive", 0.0f, 1.0f, 0.0f));
    p.push_back(choiceParam(PP::FSLOPE, "Slope", { "6 dB", "12 dB", "24 dB", "48 dB" }, 1));
    // lfo (off by default)
    p.push_back(boolParam(PP::LFOON, "LFO On", false));
    p.push_back(floatParam(PP::LFORATE, "LFO Rate", 0.05f, 20.0f, 2.0f, "Hz"));
    p.push_back(floatParam(PP::LFODEPTH, "LFO Depth", 0.0f, 1.0f, 0.0f));
    p.push_back(choiceParam(PP::LFOWAVE, "LFO Wave", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0));
    // chorus
    p.push_back(boolParam(PP::CHON, "Chorus On", false));
    p.push_back(choiceParam(PP::CHMODE, "Chorus Mode", { "Manual", "Vintage I", "Vintage II", "Wide" }, 0));
    p.push_back(floatParam(PP::CHRATE, "Chorus Rate", 0.05f, 10.0f, 1.2f, "Hz"));
    p.push_back(floatParam(PP::CHDEPTH, "Chorus Depth", 0.0f, 1.0f, 0.35f));
    p.push_back(floatParam(PP::CHMIX, "Chorus Mix", 0.0f, 1.0f, 0.4f));
    // phaser
    p.push_back(boolParam(PP::PHON, "Phaser On", false));
    p.push_back(floatParam(PP::PHRATE, "Phaser Rate", 0.05f, 10.0f, 0.6f, "Hz"));
    p.push_back(floatParam(PP::PHDEPTH, "Phaser Depth", 0.0f, 1.0f, 0.5f));
    p.push_back(floatParam(PP::PHFB, "Phaser Feedback", 0.0f, 0.95f, 0.5f));
    p.push_back(floatParam(PP::PHMIX, "Phaser Mix", 0.0f, 1.0f, 0.5f));
    // flanger
    p.push_back(boolParam(PP::FLON, "Flanger On", false));
    p.push_back(floatParam(PP::FLRATE, "Flanger Rate", 0.05f, 10.0f, 0.4f, "Hz"));
    p.push_back(floatParam(PP::FLDEPTH, "Flanger Depth", 0.0f, 1.0f, 0.5f));
    p.push_back(floatParam(PP::FLFB, "Flanger Feedback", -0.95f, 0.95f, 0.55f));
    p.push_back(floatParam(PP::FLMIX, "Flanger Mix", 0.0f, 1.0f, 0.45f));
    // distortion
    p.push_back(boolParam(PP::DION, "Distortion On", false));
    p.push_back(floatParam(PP::DIDRIVE, "Dist Drive", 0.0f, 1.0f, 0.45f));
    p.push_back(floatParam(PP::DIMIX, "Dist Mix", 0.0f, 1.0f, 0.8f));
    // saturation
    p.push_back(boolParam(PP::SAON, "Saturation On", false));
    p.push_back(floatParam(PP::SAAMT, "Sat Amount", 0.0f, 1.0f, 0.3f));
    p.push_back(floatParam(PP::SATONE, "Sat Tone", 0.0f, 1.0f, 0.6f));
    p.push_back(choiceParam(PP::SAMODE, "Sat Mode", { "Tape", "Tube" }, 0));
    // reverb
    p.push_back(boolParam(PP::RVON, "Reverb On", false));
    p.push_back(floatParam(PP::RVSIZE, "Reverb Size", 0.0f, 1.0f, 0.45f));
    p.push_back(floatParam(PP::RVDAMP, "Reverb Damp", 0.0f, 1.0f, 0.5f));
    p.push_back(floatParam(PP::RVMIX, "Reverb Mix", 0.0f, 1.0f, 0.3f));
    // delay
    p.push_back(boolParam(PP::DLON, "Delay On", false));
    p.push_back(floatParam(PP::DLTIME, "Delay Time", 1.0f, 2000.0f, 375.0f, "ms"));
    p.push_back(floatParam(PP::DLFB, "Delay Feedback", 0.0f, 0.9f, 0.35f));
    p.push_back(floatParam(PP::DLMIX, "Delay Mix", 0.0f, 1.0f, 0.25f));
    p.push_back(boolParam(PP::DLSYNC, "Delay Sync", false));
    p.push_back(choiceParam(PP::DLDIV, "Delay Division",
        { "1/4", "1/8", "1/8 Dotted", "1/4 Triplet", "1/16", "1/2" }, 1));
    // master (-24 dB default: hot banks peak far above 0 dBFS voiced raw)
    p.push_back(floatParam(PP::VOLUME, "Volume", 0.0f, 1.25f, 0.063f));
    p.push_back(boolParam(PP::LIMIT, "Limiter", true));
    p.push_back(boolParam(PP::MONO, "Mono", false));
    // arp
    p.push_back(boolParam(PP::ARPON, "Arp On", false));
    p.push_back(choiceParam(PP::ARPMODE, "Arp Mode", { "Up", "Down", "Up-Down", "Random" }, 0));
    p.push_back(choiceParam(PP::ARPDIV, "Arp Division",
        { "1/4", "1/4T", "1/4 D", "1/8", "1/8T", "1/8 D", "1/16", "1/16T", "1/16 D", "1/32", "1/32T" }, 3));
    p.push_back(floatParam(PP::ARPOCT, "Arp Octaves", 1.0f, 4.0f, 1.0f));
    p.push_back(floatParam(PP::ARPGATE, "Arp Gate", 0.1f, 1.0f, 0.8f));
    return { p.begin(), p.end() };
}
