#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ============================================================
// Pastel Soundfont Player : parameter IDs
// ============================================================
namespace PP
{
    // Sound / program
    static constexpr const char* PRESET   = "sfpreset"; // sf2 preset index 0..127
    static constexpr const char* BANK     = "sfbank";   // sf2 bank 0..127
    // Amp envelope (applied per-voice on SFZ, post on SF2)
    static constexpr const char* ADSRON = "adsron"; // UI ADSR active; off = soundfont EG
    static constexpr const char* ATK = "atk";
    static constexpr const char* DEC = "dec";
    static constexpr const char* SUS = "sus";
    static constexpr const char* REL = "rel";
    // Filter
    static constexpr const char* FTON  = "filton"; // master filter active; off = dry
    static constexpr const char* FTYPE  = "ftype";  // 0 LP 1 HP 2 BP
    static constexpr const char* FCUT   = "fcut";
    static constexpr const char* FRES   = "fres";
    static constexpr const char* FDRIVE = "fdrive";
    static constexpr const char* FSLOPE = "fslope"; // 0:6dB 1:12dB 2:24dB 3:48dB
    // Filter LFO
    static constexpr const char* LFOON = "lfoon"; // LFO active; off = no modulation
    static constexpr const char* LFORATE  = "lforate";
    static constexpr const char* LFODEPTH = "lfodepth";
    static constexpr const char* LFOWAVE  = "lfowave"; // 0 sine 1 tri 2 saw 3 square 4 s&h
    // Chorus
    static constexpr const char* CHON = "chon";
    static constexpr const char* CHMODE = "chmode"; // 0 manual 1 vintage I 2 vintage II 3 wide
    static constexpr const char* CHRATE = "chrate";
    static constexpr const char* CHDEPTH = "chdepth";
    static constexpr const char* CHMIX = "chmix";
    // Phaser
    static constexpr const char* PHON = "phon";
    static constexpr const char* PHRATE = "phrate";
    static constexpr const char* PHDEPTH = "phdepth";
    static constexpr const char* PHFB = "phfb";
    static constexpr const char* PHMIX = "phmix";
    // Flanger
    static constexpr const char* FLON = "flon";
    static constexpr const char* FLRATE = "flrate";
    static constexpr const char* FLDEPTH = "fldepth";
    static constexpr const char* FLFB = "flfb";
    static constexpr const char* FLMIX = "flmix";
    // Distortion
    static constexpr const char* DION = "dion";
    static constexpr const char* DIDRIVE = "didrive";
    static constexpr const char* DIMIX = "dimix";
    // Saturation
    static constexpr const char* SAON = "saon";
    static constexpr const char* SAAMT = "saamt";
    static constexpr const char* SATONE = "satone";
    static constexpr const char* SAMODE = "samode"; // 0 tape 1 tube
    // Reverb
    static constexpr const char* RVON = "rvon";
    static constexpr const char* RVSIZE = "rvsize";
    static constexpr const char* RVDAMP = "rvdamp";
    static constexpr const char* RVMIX = "rvmix";
    // Delay
    static constexpr const char* DLON = "dlon";
    static constexpr const char* DLTIME = "dltime"; // ms
    static constexpr const char* DLFB = "dlfb";
    static constexpr const char* DLMIX = "dlmix";
    static constexpr const char* DLSYNC = "dlsync"; // 0 free 1 sync
    static constexpr const char* DLDIV = "dldiv";   // synced division 0..5
    // Master
    static constexpr const char* VOLUME = "volume";
    static constexpr const char* LIMIT = "limit"; // limiter on/off
    static constexpr const char* MONO = "mono";   // sum to mono on/off
    // Arp
    static constexpr const char* ARPON = "arpon";
    static constexpr const char* ARPMODE = "arpmode"; // 0 up 1 down 2 updown 3 random
    static constexpr const char* ARPDIV = "arpdiv";   // 0:1/4 1:1/8 2:1/16 3:1/8T 4:1/16T 5:1/32
    static constexpr const char* ARPOCT = "arpoct";   // 1..4
    static constexpr const char* ARPGATE = "arpgate"; // 0.1..1
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
