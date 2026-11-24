/* NorthWest -- the shared effects: one delay and one reverb, each fed by every machine's send.
 *
 * DELAY: 10 ms .. 1.5 s (TIME), FEEDBACK up to 0.95, TONE = a low pass (600 Hz .. 18 kHz) and a fixed 90 Hz high
 * pass inside the feedback, STEREO or PING PONG. The time glides (no clicks when TIME moves); read with linear
 * interpolation.
 * REVERB: a small Schroeder/Freeverb network, 4 damped combs + 2 allpasses per side (different lengths per side for
 * width). SIZE sets the combs' feedback, DAMP their low pass.
 * Each stops working when nothing goes in and its tail has died away, so an unused effect costs nothing. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nw {

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
        const float target = std::min(0.01f * std::pow(150.0f, time01), 1.5f) * sampleRate;
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

} // namespace nw
