#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <map>
#include <set>
#include "VoiceFilter.h"
#include "ThirdParty/tsf.h"

// Exponential RC-style ADSR (ERSA character): snappy attack,
// smooth analog-like decay/release instead of linear segments.
struct EnvADSR
{
    void setSampleRate(double sr) { sampleRate = sr; }
    void setParams(float a, float d, float s, float r)
    {
        att = juce::jmax(a, 0.001f); dec = juce::jmax(d, 0.002f);
        sus = s; rel = juce::jmax(r, 0.005f);
    }
    void noteOn()
    {
        if (state == State::Idle) level = 0.0f;
        state = State::Attack;
    }
    void noteOff() { if (state != State::Idle) { state = State::Release; fast = false; } }
    void choke(float secs = 0.008f) // fast choke for off_by groups (off_mode=fast/time)
    {
        if (state != State::Idle) { state = State::Release; fast = true; fastSecs = secs; }
    }
    void reset()   { state = State::Idle; level = 0.0f; fast = false; }
    bool isActive() const { return state != State::Idle; }
    float ampLevel() const { return level; } // current output, for steal decisions
    // Time constants are true time: each stage settles within its nominal
    // seconds (exp(-5) ~= 0.7% residual), voices actually die on schedule
    // instead of smearing 2.5x longer over legato lines.
    inline float next()
    {
        switch (state)
        {
            case State::Idle: return 0.0f;
            case State::Attack: {
                float ka = 1.0f - std::exp(-5.0f / (att * (float) sampleRate));
                level += (1.0f - level) * ka;
                if ((1.0f - level) < 0.005f) state = State::Decay;
                return level;
            }
            case State::Decay: {
                float kd = 1.0f - std::exp(-5.0f / (dec * (float) sampleRate));
                level += (sus - level) * kd;
                if (std::fabs(level - sus) < 0.002f) { level = sus; state = State::Sustain; }
                return level;
            }
            case State::Sustain: level = sus; return sus;
            case State::Release: {
                float relSecs = fast ? fastSecs : rel;
                float kr = std::exp(-5.0f / (relSecs * (float) sampleRate));
                level *= kr;
                if (level < 0.001f) { level = 0.0f; state = State::Idle; fast = false; return 0.0f; }
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
    float fastSecs = 0.008f;
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
        float offTime = 0.008f; // for off_mode=time
        int seqLen = 0, seqPos = 1; // round robin (len 0 = off; pos defaults to 1)
        int loCC = -1, hiCC = -1, ccNum = -1; // loccN/hiccN layer gate (static, via set_cc)
        int loRand = 0, hiRand = 127;
        float delaySec = 0.0f;
        float ampRandomDb = 0.0f;  // amp_random: ±dB humanization per hit
        juce::int64 offsetRandom = 0; // offset_random: +0..N samples start jitter
        float ccGainDb = 0.0f;  // static gain_ccN/volume_onccN at set_cc defaults
        bool hasAmpEg = false;  // any ampeg_* in scope: use region EG, not the UI ADSR
        float ampA = 0.005f, ampD = 0.0f, ampS = 1.0f, ampR = 0.005f;
        // per-region filter (static note-on evaluation; dynamic CC/EG swept
        // cutoffs are out of scope). Absent entirely => bypass (no behavior
        // change for simple banks).
        bool hasFilter = false;
        float filtCut = 19000.0f; // Hz base
        int filtType = 0;         // 0 LP 1 HP 2 BP
        float filtRes = 0.0f;     // 0..1 (from resonance dB / 40)
        float filKeytrack = 0.0f; // cents per key above MIDI 60
        float filVeltrack = 0.0f; // cents added at velocity 1
        float filtCcCents = 0.0f; // static cutoff_ccN at set_cc defaults
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
    int findRegionIdx(int note, int vel, int wantTrigger, int startAfter = -1,
                        int randRoll = -1, bool applySeq = true) const;
    bool seqMatches(int regionIdx) const; // RR position ok at current counter
    static int64_t seqKeyFor(int group, const SfzRegion& r)
    {
        return ((int64_t) group << 32) | (uint32_t) articulationKey(r.swLastReq, r.keyswitch, r.swDown);
    }
    bool startVoice(int regionIdx, int note, float vel);
    // Fire a note: EVERY matching region voices (layers), like the format
    // specifies — first-match-only drops articulation composites. Delay
    // regions queue individually; unstartable ones (offset past EOF, empty
    // sample) fall through to later matches. Round-robin positions advance
    // once per trigger event per (group, articulation), whether or not a
    // voice starts — otherwise sparse cycles deadlock after the first hit.
    bool fireNote(int note, float vel01, int wantTrigger, bool allowDelay, bool advanceSeq = true);
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
    float envA = 0.01f, envD = 0.25f, envS = 0.8f, envR = 0.35f;
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

    // keyswitch state (sfz); -1 = none pressed yet (all articulations play)
    int lastKeyswitch = -1;
    int swLastDefault = -1; // from sw_last opcode (last seen; init value)
    std::set<int> swLastKeys; // every sw_last key across the patch (all are switches)
    std::map<int, juce::String> swLabels; // switch key -> sw_label articulation name
    bool isSwitchKey(int note) const;
    bool switchOk(const SfzRegion& r) const;
    // round-robin positions, keyed by (group, articulation): each
    // articulation cycles independently, exactly as if its file were loaded
    // alone (a shared counter desyncs every articulation after any switch)
    std::map<int64_t,int> seqCounters;
    static int64_t articulationKey(int swLastReq, int keyswitch, int swDown)
    {
        if (swLastReq >= 0) return (int64_t)(swLastReq + 1);
        if (keyswitch >= 0) return (int64_t)(1000 + keyswitch);
        if (swDown >= 0) return (int64_t)(2000 + swDown);
        return 0;
    }
    mutable juce::Random rng;
    int lastVel[128] = { 0 };
    struct DelayedNote { int regionIdx; int note; float vel; int trigger; int samplesLeft; };
    std::vector<DelayedNote> delayed;
};
