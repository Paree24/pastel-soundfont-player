#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "VoiceFilter.h"

// ============================================================
// DSPEngine: global filter (LP/HP/BP, 6/12/24/48dB, drive,
// filter-LFO) + FX chain + master. Stereo in-place.
//
// Filter + FX character ported from ERSA: Zavalishin SVF core,
// exponential RC envelopes live in the sampler, BBD chorus with
// vintage modes, 6-stage phaser, modulated ModVerb, tape/tube
// saturator with DC block, damped delay.
// Params are plain floats copied per-block by the processor.
// ============================================================
struct DspParams
{
    bool  fOn = false;    // master filter engaged; off = bit-dry pass-through
    int   fType = 0;      // 0 LP 1 HP 2 BP
    float fCut = 18000.0f;
    float fRes = 0.15f;
    float fDrive = 0.0f;
    int   fSlope = 1;     // 0:6 1:12 2:24 3:48

    bool  lfoOn = false;  // filter LFO engaged
    float lfoRate = 2.0f, lfoDepth = 0.0f;
    int   lfoWave = 0;    // 0 sine 1 tri 2 saw 3 square 4 s&h

    bool  chOn = false;   int chMode = 0;
    float chRate = 1.2f, chDepth = 0.35f, chMix = 0.4f;
    bool  phOn = false;   float phRate = 0.6f, phDepth = 0.5f, phFb = 0.5f, phMix = 0.5f;
    bool  flOn = false;   float flRate = 0.4f, flDepth = 0.5f, flFb = 0.55f, flMix = 0.45f;
    bool  diOn = false;   float diDrive = 0.45f, diMix = 0.8f;
    bool  saOn = false;   float saAmt = 0.3f; int saMode = 0; float saTone = 0.6f;
    bool  rvOn = false;   float rvSize = 0.45f, rvDamp = 0.5f, rvMix = 0.3f;
    bool  dlOn = false;   float dlTimeMs = 375.0f, dlFb = 0.35f, dlMix = 0.25f;
    bool  dlSync = false; int dlDiv = 1;
    double bpm = 120.0;

    float volume = 0.5f; // -6 dB default headroom
    bool  limiter = true;
    bool  mono = false;
};

// ---- BBD-style chorus with vintage modes (ERSA character) ----
class ChorusFX
{
public:
    void prepare(double sr);
    void process(float* L, float* R, int n, float rate, float depth, float mix, int mode);
    void clear();
private:
    float readDelay(const std::vector<float>& buf, float ms);
    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int len = 1024, pos = 0;
    double lfoPhase = 0.0;
    float wlpL = 0.0f, wlpR = 0.0f;
    float smBase = 12.0f, smDepth = 3.0f, smRate = 0.6f;
};

// ---- 6-stage allpass phaser (ERSA character) ----
class PhaserFX
{
public:
    void prepare(double sr) { sampleRate = sr; phase = 0.0; clear(); }
    void clear();
    void process(float* L, float* R, int n, float rate, float depth, float fb, float mix);
private:
    inline float allpass(float x, float& s, float g) { float y = -g * x + s; s = x + g * y; return y; }
    double sampleRate = 44100.0, phase = 0.0;
    float stage[6] = {}, stageL[6] = {};
    float fbL = 0.0f, fbR = 0.0f;
};

// ---- ModVerb: modulated Schroeder reverb (ERSA character) ----
class ModVerb
{
public:
    void prepare(double sr);
    void process(float* L, float* R, int n, float size, float damp, float mix);
    void clear();
private:
    struct Comb { std::vector<float> buf; int len = 1, pos = 0; float base = 0.0f, lp = 0.0f; };
    struct AP { std::vector<float> buf; int len = 1, pos = 0; float d = 0.0f, g = 0.65f; };
    struct PD { std::vector<float> buf; int len = 1, pos = 0; };
    Comb cl[4], cr[4];
    AP al[4], ar[4];
    PD pd;
    float curLenScale = 1.0f;
    double sampleRate = 44100.0, mlfo[4] = { 0.0, 0.25, 0.5, 0.75 };
    inline float readInterp(const std::vector<float>& buf, int len, float delaySamp, int writePos);
    inline float runComb(Comb& c, float in, float fb, float lpC, float mod, float lenScale);
    inline float runAP(float x, AP& a);
};

// ---- Saturator: tape / tube with DC block + tone (ERSA character) ----
class SatFX
{
public:
    void prepare(double sr) { sampleRate = sr; reset(); }
    void reset() { dcL = dcR = lpL = lpR = 0.0f; }
    void process(float* L, float* R, int n, float amt, float tone, int mode);
private:
    inline float shape(float x, int mode, float even)
    {
        float s = (mode == 1) ? x / (1.0f + 0.5f * std::fabs(x)) : std::tanh(x);
        return s + even * s * s;
    }
    double sampleRate = 44100.0;
    float dcL = 0.0f, dcR = 0.0f, lpL = 0.0f, lpR = 0.0f;
};

// ---- Damped feedback delay (ERSA character) ----
class DelayFX
{
public:
    void prepare(double sr);
    void process(float* L, float* R, int n, float timeMs, float fb, float mix);
    void clear();
private:
    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int len = 1024, pos = 0;
    float lpL = 0.0f, lpR = 0.0f;
};

class DSPEngine
{
public:
    DSPEngine();
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void process(juce::AudioBuffer<float>& buffer, const DspParams& p);
    float getLevelL() const { return levelL; }
    float getLevelR() const { return levelR; }

private:
    float lfoSample(int wave, double phase) const;

    double sr = 44100.0;
    double lfoPhase = 0.0, flPhase = 0.0, shValue = 0.0f;
    juce::Random random;
    float smoothCut = 18000.0f;

    ZvpStage svf[4][2];
    juce::dsp::FirstOrderTPTFilter<float> fo6[2];

    // ERSA-style warmth (12 kHz 1-pole) + post saturation state
    float warmY[2] = { 0.0f, 0.0f };
    float warmG = 0.5f;

    ChorusFX chorus;
    PhaserFX phaser;
    ModVerb reverb;
    SatFX sat;
    DelayFX delay;

    std::vector<float> flDelayL, flDelayR;
    int flWrite = 0;

    float distToneStateL = 0.0f, distToneStateR = 0.0f;
    float levelL = 0.0f, levelR = 0.0f;
};
