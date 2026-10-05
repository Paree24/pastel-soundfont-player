#include "DSPEngine.h"

// ---------------- ChorusFX ----------------
void ChorusFX::prepare(double sr)
{
    sampleRate = sr;
    int maxLen = (int)(sr * 0.05) + 16;
    bufL.assign((size_t) maxLen, 0.0f);
    bufR.assign((size_t) maxLen, 0.0f);
    len = maxLen; pos = 0;
    lfoPhase = 0.0;
    wlpL = wlpR = 0.0f;
    smBase = 12.0f; smDepth = 3.0f; smRate = 0.6f;
}

void ChorusFX::clear()
{
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    wlpL = wlpR = 0.0f;
}

float ChorusFX::readDelay(const std::vector<float>& buf, float ms)
{
    float samples = (float)(ms * sampleRate / 1000.0);
    float rp = (float)pos - samples;
    while (rp < 0) rp += (float) len;
    int i0 = (int)rp; float frac = rp - i0;
    int i1 = (i0 + 1) % len;
    return buf[(size_t)i0] * (1 - frac) + buf[(size_t)i1] * frac;
}

void ChorusFX::process(float* L, float* R, int n, float rate, float depth, float mix, int mode)
{
    if (mix <= 0.001f) return;
    float baseMs = 8.0f, depthMs = 4.0f * depth;
    bool quad = false;
    if (mode == 1)      { baseMs = 14.0f; depthMs = 3.0f; rate = 0.45f; }
    else if (mode == 2) { baseMs = 9.0f;  depthMs = 5.0f; rate = 0.85f; }
    else if (mode == 3) { baseMs = 14.0f; depthMs = 7.0f * depth; quad = true; }
    float k = 1.0f - std::exp(-1.0f / (0.030f * (float)sampleRate));
    float wg = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 7000.0f / (float)sampleRate);
    for (int i = 0; i < n; ++i)
    {
        smBase += (baseMs - smBase) * k;
        smDepth += (depthMs - smDepth) * k;
        smRate += (rate - smRate) * k;
        lfoPhase += smRate / sampleRate; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        float ph = (float)lfoPhase * 2.0f * (float)juce::MathConstants<double>::pi;
        float lfoL = std::sin(ph);
        float lfoR = quad ? std::cos(ph) : -lfoL;
        bufL[(size_t)pos] = L[i]; bufR[(size_t)pos] = R[i];
        float wetL = readDelay(bufL, smBase + smDepth * lfoL);
        float wetR = readDelay(bufR, smBase + smDepth * lfoR);
        wlpL += wg * (wetL - wlpL); wlpR += wg * (wetR - wlpR);
        L[i] = L[i] * (1.0f - mix * 0.5f) + wlpL * mix * 0.5f;
        R[i] = R[i] * (1.0f - mix * 0.5f) + wlpR * mix * 0.5f;
        pos = (pos + 1) % len;
    }
}

// ---------------- PhaserFX ----------------
void PhaserFX::clear()
{
    std::fill(stage, stage + 6, 0.0f);
    std::fill(stageL, stageL + 6, 0.0f);
    fbL = fbR = 0.0f;
}

void PhaserFX::process(float* L, float* R, int n, float rate, float depth, float fb, float mix)
{
    if (mix <= 0.001f) return;
    for (int i = 0; i < n; ++i)
    {
        phase += rate / sampleRate; if (phase >= 1.0) phase -= 1.0;
        float lfo = std::sin(2.0f * (float)juce::MathConstants<double>::pi * (float)phase);
        float g = juce::jlimit(0.05f, 0.95f, 0.5f + depth * 0.42f * lfo);
        float inL = L[i] + fbL * fb * 0.8f;
        float inR = R[i] + fbR * fb * 0.8f;
        float oL = inL, oR = inR;
        for (int s = 0; s < 6; ++s) { oL = allpass(oL, stage[s], g); oR = allpass(oR, stageL[s], g); }
        fbL = oL; fbR = oR;
        L[i] = L[i] * (1 - mix) + oL * mix;
        R[i] = R[i] * (1 - mix) + oR * mix;
    }
}

// ---------------- ModVerb ----------------
void ModVerb::prepare(double sr)
{
    sampleRate = sr;
    const float combMs[4] = { 47.0f, 53.0f, 44.0f, 58.0f };
    for (int i = 0; i < 4; ++i)
    {
        int maxLen = (int)(sr * 0.13) + 64;
        cl[i].buf.assign((size_t)maxLen, 0.0f);
        cr[i].buf.assign((size_t)maxLen, 0.0f);
        cl[i].len = maxLen; cr[i].len = maxLen;
        cl[i].base = combMs[i] * (float)sr / 1000.0f;
        cr[i].base = (combMs[i] + 1.1f) * (float)sr / 1000.0f;
        cl[i].pos = cr[i].pos = 0; cl[i].lp = cr[i].lp = 0.0f;
    }
    const float apMs[4] = { 5.0f, 1.7f, 3.4f, 2.6f };
    const float apG[4] = { 0.7f, 0.6f, 0.65f, 0.6f };
    for (int i = 0; i < 4; ++i)
    {
        int maxLen = (int)(sr * 0.01) + 16;
        al[i].buf.assign((size_t)maxLen, 0.0f);
        ar[i].buf.assign((size_t)maxLen, 0.0f);
        al[i].len = ar[i].len = maxLen;
        al[i].d = apMs[i] * (float)sr / 1000.0f;
        ar[i].d = (apMs[i] + 0.4f) * (float)sr / 1000.0f;
        al[i].pos = ar[i].pos = 0; al[i].g = ar[i].g = apG[i];
    }
    pd.buf.assign((size_t)((int)(sr * 0.06) + 16), 0.0f);
    pd.len = (int)pd.buf.size(); pd.pos = 0;
}

float ModVerb::readInterp(const std::vector<float>& buf, int len, float delaySamp, int writePos)
{
    float rp = (float)writePos - delaySamp;
    while (rp < 0.0f) rp += (float)len;
    int i0 = (int)rp % len; if (i0 < 0) i0 += len;
    int i1 = (i0 + 1) % len;
    float frac = rp - std::floor(rp);
    return buf[(size_t)i0] * (1.0f - frac) + buf[(size_t)i1] * frac;
}

float ModVerb::runComb(Comb& c, float in, float fb, float lpC, float mod, float lenScale)
{
    float d = juce::jlimit(4.0f, (float)c.len - 2.0f, c.base * lenScale + mod);
    float y = readInterp(c.buf, c.len, d, c.pos);
    c.lp += lpC * (y - c.lp);
    c.buf[(size_t)c.pos] = in + c.lp * fb;
    c.pos = (c.pos + 1) % c.len;
    return y;
}

float ModVerb::runAP(float x, AP& a)
{
    float g = a.g;
    int rp = a.pos - (int)a.d;
    while (rp < 0) rp += a.len;
    rp %= a.len;
    float bufOut = a.buf[(size_t)rp];
    float y = -g * x + bufOut;
    a.buf[(size_t)a.pos] = x + g * y;
    a.pos = (a.pos + 1) % a.len;
    return y;
}

void ModVerb::process(float* L, float* R, int n, float size, float damp, float mix)
{
    if (mix <= 0.001f) return;
    size = juce::jlimit(0.0f, 1.0f, size);
    float fb = 0.70f + size * 0.25f;
    float lpC = 0.55f * (1.0f - juce::jlimit(0.0f, 1.0f, damp)) + 0.05f;
    float lenScale = 0.75f + size * 0.6f;
    float kLen = 1.0f - std::exp(-1.0f / (0.050f * (float)sampleRate));
    float modAmp = (float)((0.0006 + size * 0.002) * sampleRate);
    const float rates[4] = { 0.11f, 0.19f, 0.27f, 0.23f };
    float wetG = (0.5f + size * 0.7f);
    float preD = size * 0.025f * (float)sampleRate;

    for (int i = 0; i < n; ++i)
    {
        curLenScale += (lenScale - curLenScale) * kLen;
        float dry = (L[i] + R[i]) * 0.5f;
        pd.buf[(size_t)pd.pos] = dry;
        float rp = (float)pd.pos - preD;
        while (rp < 0.0f) rp += (float)pd.len;
        int pi0 = (int)rp % pd.len; if (pi0 < 0) pi0 += pd.len;
        int pi1 = (pi0 + 1) % pd.len;
        float pf = rp - std::floor(rp);
        float in = pd.buf[(size_t)pi0] * (1.0f - pf) + pd.buf[(size_t)pi1] * pf;
        pd.pos = (pd.pos + 1) % pd.len;
        float accL = 0.0f, accR = 0.0f;
        for (int c = 0; c < 4; ++c)
        {
            mlfo[c] += rates[c] / sampleRate;
            if (mlfo[c] >= 1.0) mlfo[c] -= 1.0;
            float mod = std::sin(2.0f * juce::MathConstants<float>::pi * (float)mlfo[c]) * modAmp;
            accL += runComb(cl[c], in, fb, lpC, mod, curLenScale);
            accR += runComb(cr[c], in, fb, lpC, -mod, curLenScale);
        }
        accL *= 0.25f; accR *= 0.25f;
        float wL = accL, wR = accR;
        for (int a = 0; a < 4; ++a) { wL = runAP(wL, al[a]); wR = runAP(wR, ar[a]); }
        L[i] = L[i] * (1.0f - mix) + wL * wetG * mix;
        R[i] = R[i] * (1.0f - mix) + wR * wetG * mix;
    }
}

void ModVerb::clear()
{
    for (auto* c : { cl, cr }) for (int i = 0; i < 4; ++i)
    { std::fill(c[i].buf.begin(), c[i].buf.end(), 0.0f); c[i].lp = 0.0f; }
    for (auto* a : { al, ar }) for (int i = 0; i < 4; ++i)
        std::fill(a[i].buf.begin(), a[i].buf.end(), 0.0f);
    std::fill(pd.buf.begin(), pd.buf.end(), 0.0f);
}

// ---------------- SatFX ----------------
void SatFX::process(float* L, float* R, int n, float amt, float tone, int mode)
{
    if (amt <= 0.001f) return;
    amt = juce::jlimit(0.0f, 1.0f, amt);
    float d = 1.0f + amt * 9.0f;
    float fc = 2000.0f * std::pow(9.0f, juce::jlimit(0.0f, 1.0f, tone));
    float g = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float)sampleRate);
    float dcC = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 5.0f / (float)sampleRate);
    float post = 1.0f / (1.0f + amt * 1.5f);
    float even = (mode == 1) ? 0.18f : 0.06f;
    for (int i = 0; i < n; ++i)
    {
        float wetL = shape(L[i] * d, mode, even);
        float wetR = shape(R[i] * d, mode, even);
        dcL += dcC * (wetL - dcL); wetL -= dcL;
        dcR += dcC * (wetR - dcR); wetR -= dcR;
        lpL += g * (wetL - lpL); lpR += g * (wetR - lpR);
        L[i] = lpL * post;
        R[i] = lpR * post;
    }
}

// ---------------- DelayFX ----------------
void DelayFX::prepare(double sr)
{
    sampleRate = sr;
    int maxLen = (int)(sr * 2.0) + 16;
    bufL.assign((size_t) maxLen, 0.0f);
    bufR.assign((size_t) maxLen, 0.0f);
    len = maxLen; pos = 0; lpL = lpR = 0.0f;
}

void DelayFX::clear()
{
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
}

void DelayFX::process(float* L, float* R, int n, float timeMs, float fb, float mix)
{
    if (mix <= 0.001f) return;
    timeMs = juce::jlimit(20.0f, 1500.0f, timeMs);
    float samples = (float)(timeMs * sampleRate / 1000.0);
    for (int i = 0; i < n; ++i)
    {
        float rp = (float)pos - samples;
        while (rp < 0) rp += (float) len;
        int i0 = (int)rp; float frac = rp - i0; int i1 = (i0 + 1) % len;
        float dL = bufL[(size_t)i0] * (1 - frac) + bufL[(size_t)i1] * frac;
        float dR = bufR[(size_t)i0] * (1 - frac) + bufR[(size_t)i1] * frac;
        lpL += 0.25f * (dL - lpL); lpR += 0.25f * (dR - lpR);
        bufL[(size_t)pos] = L[i] + lpL * fb;
        bufR[(size_t)pos] = R[i] + lpR * fb;
        L[i] = L[i] * (1 - mix) + dL * mix;
        R[i] = R[i] * (1 - mix) + dR * mix;
        pos = (pos + 1) % len;
    }
}

// ---------------- DSPEngine ----------------
DSPEngine::DSPEngine() = default;

void DSPEngine::prepare(double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused(maxBlockSize);
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        fo6[ch].prepare({ sr, 8, 1 });
        for (int s = 0; s < 4; ++s) svf[s][ch].reset();
    }
    warmG = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 12000.0f / (float)sr);
    chorus.prepare(sr);
    phaser.prepare(sr);
    reverb.prepare(sr);
    sat.prepare(sr);
    delay.prepare(sr);

    int flMax = (int)(sr * 0.02) + 8;
    flDelayL.assign((size_t) flMax, 0.0f);
    flDelayR.assign((size_t) flMax, 0.0f);
    flWrite = 0;
    reset();
}

void DSPEngine::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        fo6[ch].reset();
        for (int s = 0; s < 4; ++s) svf[s][ch].reset();
    }
    chorus.clear(); phaser.clear(); reverb.clear(); sat.reset(); delay.clear();
    std::fill(flDelayL.begin(), flDelayL.end(), 0.0f);
    std::fill(flDelayR.begin(), flDelayR.end(), 0.0f);
    distToneStateL = distToneStateR = 0.0f;
    warmY[0] = warmY[1] = 0.0f;
    levelL = levelR = 0.0f;
    smoothCut = 18000.0f;
}

float DSPEngine::lfoSample(int wave, double phase) const
{
    double ph = phase - std::floor(phase);
    switch (wave)
    {
        case 0: return (float) std::sin(ph * juce::MathConstants<double>::twoPi);
        case 1: return (float)(4.0 * std::abs(ph - 0.5) - 1.0);
        case 2: return (float)(2.0 * ph - 1.0);
        case 3: return ph < 0.5 ? 1.0f : -1.0f;
        default: return 0.0f;
    }
}

static float softClip(float x) { return std::tanh(x); }

void DSPEngine::process(juce::AudioBuffer<float>& buffer, const DspParams& p)
{
    const int N = buffer.getNumSamples();
    if (N <= 0) return;
    const int nCh = juce::jmin(2, buffer.getNumChannels());
    float* L = buffer.getWritePointer(0);
    float* R = nCh > 1 ? buffer.getWritePointer(1) : L;

    using F6T = juce::dsp::FirstOrderTPTFilterType;
    F6T foType = F6T::lowpass;
    if (p.fType == 1) foType = F6T::highpass;
    for (int ch = 0; ch < 2; ++ch) fo6[ch].setType(foType);

    int stages = p.fSlope == 0 ? 0 : (p.fSlope == 1 ? 1 : (p.fSlope == 2 ? 2 : 4));

    float driveGain = 1.0f + p.fDrive * 12.0f;
    float driveComp = 1.0f / (1.0f + p.fDrive * 4.0f);
    double lfoInc = (double) p.lfoRate / sr;
    bool lfoActive = p.lfoDepth > 0.0005f;

    // ---------- per-sample: LFO + ERSA-style filter + drive ----------
    for (int i = 0; i < N; ++i)
    {
        float lfo = 0.0f;
        if (lfoActive)
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= 1.0;
                if (p.lfoWave == 4) shValue = random.nextFloat() * 2.0f - 1.0f;
            }
            lfo = (p.lfoWave == 4) ? shValue : lfoSample(p.lfoWave, lfoPhase);
        }
        float oct = lfo * p.lfoDepth * 4.0f;
        float targetCut = juce::jlimit(30.0f, 19000.0f, p.fCut * std::pow(2.0f, oct));
        smoothCut += (targetCut - smoothCut) * 0.02f;
        fo6[0].setCutoffFrequency(smoothCut);
        fo6[1].setCutoffFrequency(smoothCut);

        float xL = L[i], xR = R[i];
        if (p.fDrive > 0.0005f)
        {
            xL = softClip(xL * driveGain) * driveComp;
            xR = softClip(xR * driveGain) * driveComp;
        }
        if (stages == 0)
        {
            if (p.fType == 2) // BP6 falls back to one SVF stage (BP12)
            {
                xL = svf[0][0].process(xL, smoothCut, p.fRes, sr, 2);
                xR = svf[0][1].process(xR, smoothCut, p.fRes, sr, 2);
            }
            else
            {
                xL = fo6[0].processSample(0, xL);
                xR = fo6[1].processSample(1, xR);
            }
        }
        else
        {
            for (int s = 0; s < stages; ++s)
            {
                xL = svf[s][0].process(xL, smoothCut, p.fRes, sr, p.fType);
                xR = svf[s][1].process(xR, smoothCut, p.fRes, sr, p.fType);
            }
        }
        // ERSA post-filter glue: gentle saturation + resonance compensation + warmth
        {
            float drv = 1.0f + p.fRes * 1.2f;
            float yL = xL * drv, yR = xR * drv;
            xL = yL / (1.0f + 0.35f * std::fabs(yL));
            xR = yR / (1.0f + 0.35f * std::fabs(yR));
            float comp = 1.0f / (1.0f + p.fRes * 1.6f);
            xL *= comp; xR *= comp;
            warmY[0] += warmG * (xL - warmY[0]);
            warmY[1] += warmG * (xR - warmY[1]);
            xL = warmY[0]; xR = warmY[1];
        }
        L[i] = xL; R[i] = xR;
    }

    // ---------- distortion (kept: ERSA has no distortion) ----------
    if (p.diOn && p.diDrive > 0.0005f)
    {
        float g = 1.0f + p.diDrive * 49.0f;
        float tone = 0.12f;
        for (int i = 0; i < N; ++i)
        {
            float dL = softClip(L[i] * g), dR = softClip(R[i] * g);
            distToneStateL += tone * (dL - distToneStateL);
            distToneStateR += tone * (dR - distToneStateR);
            float tL = dL * 0.65f + distToneStateL * 0.35f;
            float tR = dR * 0.65f + distToneStateR * 0.35f;
            L[i] = L[i] * (1.0f - p.diMix) + tL * 0.7f * p.diMix;
            R[i] = R[i] * (1.0f - p.diMix) + tR * 0.7f * p.diMix;
        }
    }

    // ---------- saturation (ERSA SatFX) ----------
    if (p.saOn)
        sat.process(L, R, N, p.saAmt, p.saTone, p.saMode);

    // ---------- chorus (ERSA BBD) ----------
    if (p.chOn)
        chorus.process(L, R, N, p.chRate, p.chDepth, p.chMix, p.chMode);

    // ---------- phaser (ERSA 6-stage) ----------
    if (p.phOn)
        phaser.process(L, R, N, p.phRate, p.phDepth, p.phFb, p.phMix);

    // ---------- flanger (kept) ----------
    if (p.flOn)
    {
        int maxD = (int) flDelayL.size() - 2;
        double inc = (double) p.flRate / sr;
        for (int i = 0; i < N; ++i)
        {
            flPhase += inc;
            if (flPhase >= 1.0) flPhase -= 1.0;
            float lfo = std::sin((float)(flPhase * juce::MathConstants<double>::twoPi));
            float dMs = 1.0f + (lfo * 0.5f + 0.5f) * 7.0f * p.flDepth;
            float dSamp = juce::jlimit(1.0f, (float) maxD, dMs * (float) sr / 1000.0f);
            for (int ch = 0; ch < nCh; ++ch)
            {
                std::vector<float>& dl = ch == 0 ? flDelayL : flDelayR;
                float in = buffer.getSample(ch, i);
                int rp = (flWrite - (int) dSamp + (int) dl.size() * 2) % (int) dl.size();
                int rp2 = (rp + 1) % (int) dl.size();
                float frac = dSamp - (int) dSamp;
                float delayed = dl[(size_t) rp] * (1.0f - frac) + dl[(size_t) rp2] * frac;
                float out = in + delayed * p.flFb;
                dl[(size_t) flWrite] = juce::jlimit(-1.5f, 1.5f, out);
                buffer.setSample(ch, i, in * (1.0f - p.flMix) + delayed * p.flMix);
            }
            flWrite = (flWrite + 1) % (int) flDelayL.size();
        }
    }

    // ---------- delay (ERSA damped) ----------
    if (p.dlOn)
    {
        float timeMs = p.dlTimeMs;
        if (p.dlSync)
        {
            double beats = 0.5;
            switch (p.dlDiv) { case 0: beats = 1.0; break; case 1: beats = 0.5; break;
                case 2: beats = 0.75; break; case 3: beats = 2.0 / 3.0; break;
                case 4: beats = 0.25; break; case 5: beats = 2.0; break; }
            double bpm = p.bpm > 20 ? p.bpm : 120.0;
            timeMs = (float)(beats * 60000.0 / bpm);
        }
        delay.process(L, R, N, timeMs, juce::jlimit(0.0f, 0.9f, p.dlFb), p.dlMix);
    }

    // ---------- reverb (ERSA ModVerb) ----------
    if (p.rvOn)
        reverb.process(L, R, N, p.rvSize, p.rvDamp, p.rvMix);

    // ---------- mono sum + master + limiter + meters ----------
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float xL = L[i], xR = R[i];
        if (p.mono) { float mid = 0.5f * (xL + xR); xL = mid; xR = mid; }
        xL *= p.volume; xR *= p.volume;
        if (p.limiter) { xL = softClip(xL); xR = softClip(xR); }
        else { xL = juce::jlimit(-1.2f, 1.2f, xL); xR = juce::jlimit(-1.2f, 1.2f, xR); }
        L[i] = xL; R[i] = xR;
        peakL = juce::jmax(peakL, std::abs(xL));
        peakR = juce::jmax(peakR, std::abs(xR));
    }
    const float decay = 0.9995f;
    levelL = juce::jmax(peakL, levelL * decay);
    levelR = juce::jmax(peakR, levelR * decay);
    if (levelL < 1e-5f) levelL = 0.0f;
    if (levelR < 1e-5f) levelR = 0.0f;
}
