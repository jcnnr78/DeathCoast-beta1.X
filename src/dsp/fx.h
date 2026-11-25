/* NorthWest -- the shared effects: a delay, a reverb and a distortion (DIRT), each fed by every machine's send,
 * and the master limiter.
 *
 * DELAY: 10 ms .. 1.5 s (TIME), FEEDBACK up to 0.95, TONE = a low pass (600 Hz .. 18 kHz) and a fixed 90 Hz high
 * pass inside the feedback, STEREO or PING PONG. The time glides (no clicks when TIME moves); read with linear
 * interpolation.
 * REVERB: a small Schroeder/Freeverb network, 4 damped combs + 2 allpasses per side (different lengths per side for
 * width). SIZE sets the combs' feedback, DAMP their low pass.
 * DIRT: drive into a shaper whose CHARACTER morphs warm saturation -> hard asymmetric clipping -> wavefolding, run
 * at twice the rate; TONE tilts the sound going in (dark .. bright); GRIT adds Erosion-style hash (a very short delay
 * wobbled by noise) and, past half way, bit and sample-rate reduction. The output follows the input's loudness, so
 * DRIVE changes the dirt, not the level.
 * Each stops working when nothing goes in and its tail has died away, so an unused effect costs nothing. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "filters.h"
#include "oversample.h"

namespace nw {

inline float delayTimeSeconds(float v) { return std::min(0.01f * std::pow(150.0f, v / 127.0f), 1.5f); }
inline void dirtCharacterText(char *buf, int len, float v)
{
    const float x = v / 127.0f;
    if (x < 0.03f) snprintf(buf, len, "WARM");
    else if (std::fabs(x - 0.5f) < 0.03f) snprintf(buf, len, "HARD");
    else if (x > 0.97f) snprintf(buf, len, "FOLD");
    else if (x < 0.5f) snprintf(buf, len, "WARM>HARD %d%%", (int) std::lround(x * 200.0f));
    else snprintf(buf, len, "HARD>FOLD %d%%", (int) std::lround((x - 0.5f) * 200.0f));
}
inline void dirtToneText(char *buf, int len, float v)
{
    const int t = (int) std::lround((v - 64.0f) / 63.0f * 100.0f);
    if (t == 0) snprintf(buf, len, "FLAT");
    else snprintf(buf, len, t < 0 ? "DARK %d" : "BRIGHT %d", std::abs(t));
}

class Delay {
public:
    void setSampleRate(double sr)
    {
        sampleRate = (float) sr;
        size = 1; while (size < (int) (sr * 1.6)) size <<= 1;
        bufL.assign((size_t) size, 0.0f); bufR.assign((size_t) size, 0.0f);
        pos = 0; lpL = lpR = hpL = hpR = 0; timeS = -1; idle = 0;
    }
    /* inL/inR: the send bus; adds the wet signal into outL/outR */
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 float time01, float fb01, float tone01, bool pingPong, float level)
    {
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(inL[i]) + std::fabs(inR[i]));
        if (peak == 0.0f && idle > size) return;           /* silent and the buffer has run out */
        idle = peak == 0.0f ? idle + n : 0;
        const float target = delayTimeSeconds(time01 * 127.0f) * sampleRate;
        if (timeS < 0) timeS = target;
        const float fb = fb01 * 0.95f;
        const float lpc = 1.0f - std::exp(-2.0f * (float) M_PI * 600.0f * std::pow(30.0f, tone01) / sampleRate);
        const float hpc = 1.0f - std::exp(-2.0f * (float) M_PI * 90.0f / sampleRate);
        const float glide = 1.0f - std::exp(-1.0f / (0.08f * sampleRate));
        const int mask = size - 1;
        for (int i = 0; i < n; i++) {
            timeS += (target - timeS) * glide;
            const float rp = (float) pos - timeS;
            const int i0 = (int) std::floor(rp); const float fr = rp - (float) i0;
            const int a = i0 & mask, b = (i0 + 1) & mask;
            const float dl = bufL[a] + (bufL[b] - bufL[a]) * fr;
            const float dr = bufR[a] + (bufR[b] - bufR[a]) * fr;
            /* feedback path: low pass (TONE) and a 90 Hz high pass */
            lpL += lpc * (dl - lpL); lpR += lpc * (dr - lpR);
            hpL += hpc * (lpL - hpL); hpR += hpc * (lpR - hpR);
            const float fl = (lpL - hpL) * fb, fr2 = (lpR - hpR) * fb;
            if (pingPong) {        /* the input enters on the left, the repeats cross sides */
                bufL[pos] = (inL[i] + inR[i]) * 0.5f + fr2;
                bufR[pos] = fl;
            } else {
                bufL[pos] = inL[i] + fl;
                bufR[pos] = inR[i] + fr2;
            }
            outL[i] += dl * level; outR[i] += dr * level;
            pos = (pos + 1) & mask;
        }
    }

private:
    std::vector<float> bufL, bufR;
    int size = 1, pos = 0, idle = 0;
    float sampleRate = 44100.0f, timeS = -1, lpL = 0, lpR = 0, hpL = 0, hpR = 0;
};

class Reverb {
public:
    void setSampleRate(double sr)
    {
        static const int kComb[2][4] = { { 1116, 1277, 1422, 1557 }, { 1139, 1300, 1445, 1580 } };
        static const int kAll[2][2] = { { 556, 341 }, { 579, 364 } };
        const double k = sr / 44100.0;
        for (int c = 0; c < 2; c++) {
            for (int j = 0; j < 4; j++) comb[c][j].init((int) (kComb[c][j] * k));
            for (int j = 0; j < 2; j++) all[c][j].init((int) (kAll[c][j] * k));
        }
        idle = 0; tail = (int) (sr * 12.0);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 float size01, float damp01, float level)
    {
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(inL[i]) + std::fabs(inR[i]));
        if (peak == 0.0f && idle > tail) return;
        idle = peak == 0.0f ? idle + n : 0;
        const float fb = 0.70f + 0.28f * size01;
        const float damp = 0.05f + 0.6f * damp01;
        for (int i = 0; i < n; i++) {
            const float x = (inL[i] + inR[i]) * 0.15f;
            float y[2];
            for (int c = 0; c < 2; c++) {
                float s = 0;
                for (int j = 0; j < 4; j++) s += comb[c][j].tick(x, fb, damp);
                for (int j = 0; j < 2; j++) s = all[c][j].tick(s);
                y[c] = s;
            }
            outL[i] += y[0] * level; outR[i] += y[1] * level;
        }
    }

private:
    struct Comb {
        std::vector<float> b; int p = 0; float store = 0;
        void init(int len) { b.assign((size_t) std::max(len, 1), 0.0f); p = 0; store = 0; }
        float tick(float x, float fb, float damp)
        {
            const float o = b[(size_t) p];
            store = o * (1.0f - damp) + store * damp;
            b[(size_t) p] = x + store * fb;
            if (++p >= (int) b.size()) p = 0;
            return o;
        }
    };
    struct All {
        std::vector<float> b; int p = 0;
        void init(int len) { b.assign((size_t) std::max(len, 1), 0.0f); p = 0; }
        float tick(float x)
        {
            const float o = b[(size_t) p];
            b[(size_t) p] = x + o * 0.5f;
            if (++p >= (int) b.size()) p = 0;
            return o - x;
        }
    };
    Comb comb[2][4];
    All all[2][2];
    int idle = 0, tail = 0;
};

class Dirt {
public:
    void setSampleRate(double sr)
    {
        sampleRate = (float) sr;
        idle = 1 << 30; pos = 0; hold = 0; heldL = heldR = 0;
        std::memset(ero, 0, sizeof ero);
        for (int c = 0; c < 2; c++) { up[c].reset(); down[c].reset(); }
        lowL = lowR = dcL = dcR = dcxL = dcxR = 0; envIn = envOut = 1e-6f;
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 float drive01, float char01, float tone01, float grit01)
    {
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(inL[i]) + std::fabs(inR[i]));
        if (peak == 0.0f && idle > 4096) return;
        idle = peak == 0.0f ? idle + n : 0;

        const float gain = 1.0f + 39.0f * drive01 * drive01;                 /* up to +32 dB into the shaper */
        const float t = tone01 * 2.0f - 1.0f;                                /* tilt round 700 Hz */
        const float gLow = t > 0 ? 1.0f - 0.7f * t : 1.0f - 0.6f * t;
        const float gHigh = t > 0 ? 1.0f + 1.5f * t : 1.0f + 0.9f * t;
        const float lc = 1.0f - std::exp(-2.0f * (float) M_PI * 700.0f / sampleRate);
        const float ero01 = std::min(1.0f, grit01 * 2.0f);                    /* Erosion: first half of GRIT */
        const float crush = std::max(0.0f, grit01 * 2.0f - 1.0f);            /* bits and rate: second half */
        const int holdN = 1 + (int) (crush * crush * 15.0f);
        const float levels = std::pow(2.0f, 16.0f - 12.0f * crush) * 0.5f;
        const float envC = 1.0f - std::exp(-1.0f / (0.05f * sampleRate));

        float xl[kMax], xr[kMax];
        for (int i = 0; i < n; i++) {
            float l = inL[i], r = inR[i];
            lowL += lc * (l - lowL); lowR += lc * (r - lowR);
            l = lowL * gLow + (l - lowL) * gHigh;
            r = lowR * gLow + (r - lowR) * gHigh;
            if (ero01 > 0.0f) {                       /* a 0.1-1 ms delay, its length wobbled by noise, added back */
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                const float wob = (float) (rng & 0xFFFF) / 65536.0f;
                const float d = (0.1f + 0.9f * wob * ero01) * 0.001f * sampleRate;
                ero[0][pos] = l; ero[1][pos] = r;
                const int j = (pos - 1 - (int) d) & (kEro - 1);
                l += ero[0][j] * ero01 * 0.8f; r += ero[1][j] * ero01 * 0.8f;
                pos = (pos + 1) & (kEro - 1);
            }
            envIn += envC * (l * l + r * r - envIn);
            xl[i] = l * gain; xr[i] = r * gain;
        }
        /* the shaper at twice the rate */
        float ul[2 * kMax], ur[2 * kMax];
        up[0].process(xl, ul, n); up[1].process(xr, ur, n);
        for (int i = 0; i < 2 * n; i++) { ul[i] = shape(ul[i], char01); ur[i] = shape(ur[i], char01); }
        down[0].process(ul, xl, n); down[1].process(ur, xr, n);
        for (int i = 0; i < n; i++) {
            float l = xl[i], r = xr[i];
            if (crush > 0.0f) {                                              /* sample-rate and bit reduction */
                if (hold <= 0) { heldL = std::round(l * levels) / levels; heldR = std::round(r * levels) / levels; hold = holdN; }
                hold--;
                l = heldL; r = heldR;
            }
            /* DC blocker (asymmetric clipping leaves an offset) */
            const float yl = l - dcxL + 0.995f * dcL; dcxL = l; dcL = yl;
            const float yr = r - dcxR + 0.995f * dcR; dcxR = r; dcR = yr;
            envOut += envC * (yl * yl + yr * yr - envOut);
            /* follow the input's loudness */
            const float mk = std::clamp(std::sqrt((envIn + 1e-9f) / (envOut + 1e-9f)), 0.05f, 4.0f);
            outL[i] += yl * mk; outR[i] += yr * mk;
        }
    }

private:
    static float shape(float x, float c)
    {
        const float soft = fastTanh(x);
        const float hard = std::clamp(x + 0.15f, -0.7f, 1.0f);              /* asymmetric: more bite one way */
        if (c < 0.5f) { const float w = c * 2.0f; return soft * (1.0f - w) + hard * w; }
        const float w = (c - 0.5f) * 2.0f;
        const float fold = std::sin(x * 1.5707963f);                       /* folds back past +-1 */
        return hard * (1.0f - w) + fold * w;
    }
    static constexpr int kMax = 32, kEro = 128;
    float sampleRate = 44100.0f;
    float ero[2][kEro] = {};
    Upsampler2x up[2];
    Decimator2x down[2];
    float lowL = 0, lowR = 0, dcL = 0, dcR = 0, dcxL = 0, dcxR = 0, envIn = 1e-6f, envOut = 1e-6f;
    float heldL = 0, heldR = 0;
    int pos = 0, hold = 0, idle = 1 << 30;
    uint32_t rng = 0x1234567u;
};

/* the master: a peak limiter (instant attack, 120 ms release) at -1 dBFS, then a soft knee into the last dB, so
 * four machines and the effects summed past full scale squash instead of clipping hard */
class Limiter {
public:
    void setSampleRate(double sr) { rel = 1.0f - std::exp(-1.0f / (0.12f * (float) sr)); }
    void process(float *L, float *R, int n)
    {
        const float th = 0.891f;
        for (int i = 0; i < n; i++) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i]));
            const float want = pk > th ? th / pk : 1.0f;
            g = want < g ? want : g + (want - g) * rel;
            L[i] = knee(L[i] * g); R[i] = knee(R[i] * g);
        }
    }
private:
    static float knee(float x)
    {
        const float a = std::fabs(x);
        if (a <= 0.85f) return x;
        const float y = 0.85f + 0.14f * fastTanh((a - 0.85f) / 0.14f);
        return x < 0 ? -y : y;
    }
    float g = 1.0f, rel = 0.0002f;
};

} // namespace nw
