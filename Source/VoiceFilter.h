#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Zavalishin SVF stage (ERSA character, shared by the global filter and
// sampler voices): Q = 0.5 + r^2 * 5, lowpass core with HP/BP derived
// from the same state. One instance per channel.
struct ZvpStage
{
    void reset() { ic1 = ic2 = 0.0f; }
    // type: 0 lowpass, 1 highpass, 2 bandpass
    inline float process(float x, float cutoffHz, float reso01, double sr, int type)
    {
        float fc = juce::jlimit(30.0f, 19000.0f, cutoffHz);
        float g = std::tan(juce::MathConstants<float>::pi * fc / (float) sr);
        g = juce::jmin(g, 8.0f);
        float r = juce::jlimit(0.0f, 0.98f, reso01);
        float Q = 0.5f + r * r * 5.0f;
        float k = 1.0f / Q;
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3; // bandpass
        float v2 = ic2 + a2 * ic1 + a3 * v3; // lowpass
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (type == 1) return x - k * v1 - v2; // highpass
        if (type == 2) return v1;              // bandpass
        return v2;                             // lowpass
    }
    float ic1 = 0.0f, ic2 = 0.0f;
};
