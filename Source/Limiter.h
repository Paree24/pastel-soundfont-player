#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

// Master brick-wall peak limiter: 1.5 ms lookahead, stereo-linked, sliding-minimum gain
// computer (the gain is already down when a peak reaches the output), smooth release.
// Peaks are turned down instead of clipped; a final clamp only catches rounding.
// Shared module, ported from Pastel Chromatic Sampler's Limiter.h: prepare() allocates,
// process() never does. Report latency() to the host (it holds while bypassed too).
class PeakLimiter
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        la = juce::jmax(1, (int) std::lround(0.0015 * sr));
        size = la + 1;
        dl.assign((size_t) size, 0.0f);
        dr.assign((size_t) size, 0.0f);
        tg.assign((size_t) size, 1.0f);
        dqIdx.assign((size_t) size + 1, 0);
        dqVal.assign((size_t) size + 1, 1.0f);
        attCoef = 1.0f - std::exp(-1.0f / (0.3f * (float) la));
        relCoef = 1.0f - std::exp(-1.0f / (float) (0.08 * sr));
        reset();
    }

    void reset()
    {
        std::fill(dl.begin(), dl.end(), 0.0f);
        std::fill(dr.begin(), dr.end(), 0.0f);
        std::fill(tg.begin(), tg.end(), 1.0f);
        pos = 0;
        n = 0;
        head = tail = 0;
        g = 1.0f;
        lastReduction = 0.0f;
    }

    int latency() const { return size; } // the real delay: a sample written now is read back size samples later
    float reductionDb() const { return lastReduction; }

    // enabled = false keeps the same latency (delay only) so toggling never shifts timing
    void process(float* L, float* R, int num, float ceiling, bool enabled)
    {
        float maxRed = 0.0f;
        for (int i = 0; i < num; ++i)
        {
            const float xl = L[i], xr = R[i];
            const float peak = juce::jmax(std::abs(xl), std::abs(xr));
            const float target = (enabled && peak > ceiling) ? ceiling / peak : 1.0f;

            // sliding minimum of the target gain over the lookahead window (monotonic deque)
            const long long idx = n++;
            while (tail > head && dqVal[(size_t) ((tail - 1) % (size + 1))] >= target) --tail;
            dqVal[(size_t) (tail % (size + 1))] = target;
            dqIdx[(size_t) (tail % (size + 1))] = idx;
            ++tail;
            while (dqIdx[(size_t) (head % (size + 1))] <= idx - size) ++head;
            const float gmin = dqVal[(size_t) (head % (size + 1))];

            if (!enabled) g = 1.0f; // bypassed: delay only, no gain
            else
            {
                g += (gmin - g) * (gmin < g ? attCoef : relCoef);
                if (gmin >= 1.0f && g > 0.99999f) g = 1.0f; // fully released: exactly unity
            }

            // delayed signal out
            const float ol = dl[(size_t) pos], or_ = dr[(size_t) pos];
            dl[(size_t) pos] = xl;
            dr[(size_t) pos] = xr;
            pos = (pos + 1) % size;
            float yl = ol * g, yr = or_ * g;
            if (enabled)
            {
                yl = juce::jlimit(-ceiling, ceiling, yl);
                yr = juce::jlimit(-ceiling, ceiling, yr);
            }
            L[i] = yl;
            R[i] = yr;
            maxRed = juce::jmax(maxRed, -20.0f * std::log10(juce::jmax(1.0e-6f, g)));
        }
        lastReduction = maxRed;
    }

private:
    double sr = 44100.0;
    int la = 64, size = 65, pos = 0;
    long long n = 0, head = 0, tail = 0;
    std::vector<float> dl, dr, tg, dqVal;
    std::vector<long long> dqIdx;
    float g = 1.0f, attCoef = 0.1f, relCoef = 0.001f, lastReduction = 0.0f;
};
