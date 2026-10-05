#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <map>
#include <set>
#include "VoiceFilter.h"
#include "ThirdParty/tsf.h"

// ADSR envelope (sfizz strategy): linear attack, exponential
// decay/release reaching -80 dB in nominal time (exp(-9)), release
// floor at 1e-4. Choke uses the 6 ms off-time for off_mode=fast.
struct EnvADSR
{
    void setSampleRate(double sr) { sampleRate = sr; }
    void setParams(float a, float d, float s, float r)
    {
        att = juce::jmax(a, 0.0f); dec = juce::jmax(d, 0.006f);
        sus = s; rel = juce::jmax(r, 0.006f);
    }
    void noteOn()
    {
        if (state == State::Idle) level = 0.0f;
        state = State::Attack;
    }
    void noteOff() { if (state != State::Idle) { state = State::Release; fast = false; } }
    void choke(float secs = 0.006f) // fast choke for off_by groups (off_mode=fast/time)
    {
        if (state != State::Idle) { state = State::Release; fast = true; fastSecs = secs; }
    }
    void reset()   { state = State::Idle; level = 0.0f; fast = false; }
    bool isActive() const { return state != State::Idle; }
    bool isReleasing() const { return state == State::Release; }
    float ampLevel() const { return level; } // current output, for steal decisions
    inline float next()
    {
        switch (state)
        {
            case State::Idle: return 0.0f;
            case State::Attack: {
                if (att <= 0.0f) { level = 1.0f; state = State::Decay; return level; }
                level += 1.0f / (att * (float) sampleRate);
                if (level >= 1.0f) { level = 1.0f; state = State::Decay; }
                return level;
            }
            case State::Decay: {
                float kd = std::exp(-9.0f / (dec * (float) sampleRate));
                level = sus + (level - sus) * kd;
                if (std::fabs(level - sus) < 0.002f) { level = sus; state = State::Sustain; }
                return level;
            }
            case State::Sustain: level = sus; return sus;
            case State::Release: {
                float relSecs = fast ? fastSecs : rel;
                float kr = std::exp(-9.0f / (relSecs * (float) sampleRate));
                level *= kr;
                if (level < 0.0001f) { level = 0.0f; state = State::Idle; fast = false; return 0.0f; }
                return level;
            }
        }
        return 0.0f;
    }
private:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    State state = State::Idle;
    double sampleRate = 44100.0;
    float att = 0.01f, dec = 0.2f, sus = 0.8f, rel = 0.35f, level = 0.0f;
    bool fast = false;
    float fastSecs = 0.006f;
};

// ============================================================
// SamplerEngine: SF2 via TinySoundFont, SFZ via built-in
// subset parser + polyphonic sample voices.
// Threading: loadFile/program changes on message thread,
// render/noteOn/noteOff on audio thread, guarded by lock.
// ============================================================
class SamplerEngine
{
public:
    SamplerEngine();
    ~SamplerEngine();

    bool loadFile(const juce::String& path, juce::String& errorOut);
    void unload();

    bool isLoaded() const { return loaded; }
    bool isSf2() const { return loadedSf2; }
    // keyswitch / mapping queries (for keyboard highlight)
    int getLastKeyswitch() const;
    static bool isAudioFile(const juce::File& f);
    std::vector<std::pair<int,int>> getMappedRanges() const;
    std::vector<std::pair<int,int>> getSwitchRanges() const;
    juce::String currentPath() const { return currentFile; }
    juce::String currentName() const;
    juce::String programName() const;

    // SF2 programs
    int getPresetCount() const;
    juce::StringArray getPresetNames() const;
    void setSf2Preset(int bank, int preset);
    int getSf2Bank() const { return sf2Bank; }
    int getSf2Preset() const { return sf2Preset; }

    void setSampleRate(double sr);
    void setEnvelope(float a, float d, float s, float r);
    // UI ADSR engaged (default OFF): ON = UI ADSR voices every note;
    // OFF = the soundfont plays as authored (region ampeg_* where present,
    // else a neutral gate). Lock-protected.
    void setAdsrEnabled(bool on);
    bool isAdsrEnabled() const;
    // Live MIDI CC value (sfizz MidiState): loccN/hiccN gates evaluate per
    // trigger against this. Lock-protected, audio thread safe.
    void setCC(int cc, int value);
    int getCC(int cc) const;

    // extra folders to search when samples aren't next to the .sfz
    // (your library shortcuts). Lock-protected, message thread.
    void setSearchPaths(const juce::StringArray& paths);
    int activeVoiceCount() const; // lock-protected
    // test introspection: full path of the sample a live voice plays, else ""
    juce::String getVoiceSamplePath(int voiceIdx) const;
    juce::String getSwitchLabel(int note) const; // sw_label or ""
    // articulation selector requirement of a live voice's region (-2 idle)
    int getVoiceSwReq(int voiceIdx) const;
    // Background-load support: polled during long sample loads so the caller
    // can report progress and abort. Null = synchronous load (default).
    void setLoadMonitor(std::atomic<bool>* cancel, std::atomic<float>* progress);
    // Move a background-loaded result into this (live) engine. Fast: swaps
    // vectors/pointers under this lock; caller must ensure `other` is idle.
    void swapWith(SamplerEngine& other);

    void noteOn(int midiNote, float velocity01);
    void noteOff(int midiNote);
    void allNotesOff();
    void panic();

    // Adds sampler output into dest (not cleared). dest must hold numSamples.
    void renderAdding(float* destL, float* destR, int numSamples);

private:
    struct SfzRegion
    {
        int sampleIndex = -1;   // into sampleBuffers
        double sampleRate = 44100.0;
        int rootKey = 60, loKey = 0, hiKey = 127;
        int loVel = 0, hiVel = 127;
        int tuneCents = 0;      // tune + transpose*100
        float volumeDb = 0.0f;
        float pan = 0.5f;       // 0 left .. 1 right
        float ampVelTrack = 1.0f; // 0..1
        int loopMode = 0;       // 0 off, 1 continuous/sustain, 2 one_shot
        juce::int64 loopStart = 0, loopEnd = 0;
        juce::String swLabel;   // sw_label articulation name (scope only)
        int keyswitch = -1;     // exact keyswitch key, -1 = none
        int swLo = -1, swHi = -1; // keyswitch range, -1 = none (info only)
        int swLastReq = -1;     // articulation id from sw_last in scope: requires
                                // lastKeyswitch == this (ARIA semantics)
        int swDown = -1;        // sw_down selector key (treated like keyswitch)
        int trigger = 0;        // 0 attack (first/legato), 1 release
        juce::int64 offset = 0; // start offset in samples
        juce::int64 endSmp = -1;// end offset in samples, -1 = file end
        int direction = 0;      // 0 forward, 1 reverse
        int keytrack = 1;       // pitch_keytrack, 0 = fixed pitch
        int group = 0, offBy = 0;
        int offMode = 0;        // 0 normal (release stage) 1 fast 2 timed
        float offTime = 0.006f; // for off_mode=time (sfizz Default::offTime)
        int seqLen = 0, seqPos = 1; // round robin (len 0 = off; pos defaults to 1)
        int seqCounter = 0;     // sfizz strategy: per-region position, advanced
                                // on every key-matching trigger
        // loccN/hiccN layer gates (sfizz strategy): EVERY gate must pass,
        // evaluated per trigger against live CC state (set_cc = initial).
        // Multiple CCs per region allowed (e.g. Sus_Long + Sus_P5 stacking).
        struct CCGate { int cc = 0, lo = 0, hi = 127; };
        std::vector<CCGate> ccGates;
        int loRand = 0, hiRand = 127;
        float delaySec = 0.0f;
        float ampRandomDb = 0.0f;  // amp_random: ±dB humanization per hit
        juce::int64 offsetRandom = 0; // offset_random: +0..N samples start jitter
        // Per-CC gain depths (gain_ccN/volume_onccN): LAST opcode wins per CC
        // (sfizz: same target+CC overwrites its connection). Summing repeats
        // stacked +30 dB per restated line — Metal GTX restates gain_cc30=50
        // in 8 <global> blocks, which voice-stacked into +195 dB noise.
        std::map<int,float> ccGainDepth;
        bool hasAmpEg = false;  // any ampeg_* in scope: use region EG, not the UI ADSR
        float ampA = 0.005f, ampD = 0.0f, ampS = 1.0f, ampR = 0.005f;
        // per-region filter (note-on evaluation; CC depths follow live CCs).
        // Absent entirely => bypass (no behavior change for simple banks).
        bool hasFilter = false;
        float filtCut = 19000.0f; // Hz base
        int filtType = 0;         // 0 LP 1 HP 2 BP
        float filtRes = 0.0f;     // 0..1 (from resonance dB / 40)
        float filKeytrack = 0.0f; // cents per key above MIDI 60
        float filVeltrack = 0.0f; // cents added at velocity 1
        std::map<int,float> filtCcDepth; // cutoff_ccN depths: last wins per CC (sfizz)
    };

    struct Voice
    {
        bool active = false;
        int region = -1;
        int note = -1;
        float vel = 0.0f;
        double pos = 0.0;
        double ratio = 1.0;
        int dir = 1;            // +1 forward, -1 reverse
        int startSamp = 0;      // playback bounds (inclusive/exclusive)
        int stopSamp = 0;
        int group = 0;
        int trigType = 0;     // trigger event that started the voice (0 attack, 1 release)
        float gainL = 1.0f, gainR = 1.0f;
        EnvADSR env;
        juce::uint32 startedAt = 0;
        ZvpStage vcf[2];      // per-voice region filter state (L/R)
        bool filtOn = false;
        float cutHz = 19000.0f;
        int filtType = 0;
        float filtRes = 0.0f;
    };

    bool parseSfz(const juce::String& text, const juce::File& sfzDir); // false = cancelled
    bool loadSampleFile(const juce::File& f, int& indexOut, double& rateOut);
    void unloadInternal(); // lock must be held
    // sample resolution (lock must be held)
    juce::File resolveSample(const juce::File& sfzDir, const juce::String& defaultPath,
                             const juce::String& raw, juce::String* triedOut = nullptr);
    void ensureLibraryIndex(); // lock must be held
    // region matching + voice start (lock must be held)
    int findRegionIdx(int note, int vel, int wantTrigger, int startAfter, int randRoll) const;
    bool startVoice(int regionIdx, int note, float vel, bool fromNoteOn);
    // Fire a note: EVERY matching region voices (layers), like the format
    // specifies — first-match-only drops articulation composites. Delay
    // regions queue individually; unstartable ones (offset past EOF, empty
    // sample) fall through to later matches. Round-robin is sfizz strategy:
    // each region owns its cycle, evaluated check-then-advance on every
    // key+trigger-matching hit (even regions filtered out later), so sparse
    // cycles can never deadlock and the first hit plays position 1.
    bool fireNote(int note, float vel01, int wantTrigger, bool allowDelay, bool advanceSeq = true);
    mutable std::vector<char> seqGate; // per-region pass flags for this trigger
    static juce::String loadSfzFileText(const juce::File& f);
    static void expandSfzIncludes(juce::String& text, const juce::File& dir,
                                  int depth, juce::StringArray& visited);
    static float midiToFreq(int m);

    juce::CriticalSection lock;
    juce::AudioFormatManager formats;
    std::atomic<bool>* cancelFlag = nullptr; // load monitor (not swapped)
    std::atomic<float>* progressOut = nullptr;

    bool loaded = false;
    bool loadedSf2 = false;
    juce::String currentFile;
    juce::String lastLoadDetail; // why the last sfz load found nothing

    // SFZ data
    std::vector<SfzRegion> regions;
    std::vector<juce::AudioBuffer<float>> sampleBuffers;
    std::vector<juce::String> samplePaths; // parallel to sampleBuffers (test introspection)
    static constexpr int maxVoices = 64;
    Voice voices[maxVoices];
    juce::uint32 voiceCounter = 0;
    float envA = 0.001f, envD = 4.0f, envS = 1.0f, envR = 0.01f;
    bool adsrOn = false; // UI ADSR engaged (default off: soundfont as authored)
    double hostRate = 44100.0;

    // SF2 global EG (tsf owns its voices, so ADSR shapes the sum)
    EnvADSR sf2MasterEg;
    bool sf2EgGate = false;

    // SF2 data
    tsf* soundfont = nullptr;
    int sf2Bank = 0, sf2Preset = 0;
    std::vector<float> tsfTemp; // interleaved stereo scratch

    // basename -> full path cache for relocated-sample search (lock guarded)
    juce::StringArray searchPaths;
    // global library index (search-path roots, fingerprint-cached across loads)
    std::map<juce::String, juce::String> libIndex;
    juce::String libIndexKey;
    int libIndexedFiles = 0, libIndexedRoots = 0;
    // per-load index of the sfz dir subtree (built ONCE per load, see parseSfz;
    // never walked per-region: that was the Metal-GTX hang)
    std::map<juce::String, juce::String> localIndex;
    std::vector<std::pair<juce::String, juce::String>> localPaths; // (lowerFull, full)
    bool searchCapped = false; // library index hit a budget/timeout (see error text)

    // keyswitch state (sfz); -1 = none pressed yet. sfizz strategy: switched
    // layers stay silent until selected (sw_default = initial selection).
    int lastKeyswitch = -1;
    std::set<int> swLastKeys; // every sw_last key across the patch (all are switches)
    std::map<int, juce::String> swLabels; // switch key -> sw_label articulation name
    bool isSwitchKey(int note) const;
    bool switchOk(const SfzRegion& r) const;
    bool ccOk(const SfzRegion& r) const; // every loccN/hiccN gate passes live CC
    int ccState[128] = {}; // live CC values (sfizz: init 0, set_cc overrides)
    mutable juce::Random rng;
    int lastVel[128] = { 0 };
    struct DelayedNote { int regionIdx; int note; float vel; int trigger; int samplesLeft; };
    std::vector<DelayedNote> delayed;
};
