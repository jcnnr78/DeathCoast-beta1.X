/* DeathCoast -- the shared effects: a delay, a reverb and a distortion (DIRT), each fed by every machine's send,
 * and the master limiter.
 *
 * DELAY: a tape-like delay. TIME 10 ms .. 1.5 s (FREE) or a note value at the MPC's tempo (SYNC, up to 4 s);
 * FEEDBACK up to 110 % (the tape saturation holds it); TONE dark <- flat -> thin on the repeats; DRIVE saturates
 * what goes onto the tape; MOD adds wow and flutter; DUCK dips the repeats while the machines play; STEREO or
 * PING PONG. The time glides (no clicks when TIME moves); read with linear interpolation.
 * REVERB: an 8-line feedback delay network (see the class below).
 * DIRT: drive into a shaper whose CHARACTER morphs warm saturation -> hard asymmetric clipping -> wavefolding, run
 * at twice the rate; TONE tilts the sound going in (dark .. bright); GRIT adds Erosion-style hash (a very short delay
 * wobbled by noise) and, past half way, bit and sample-rate reduction. The output follows the input's loudness, so
 * DRIVE changes the dirt, not the level.
 * Each stops working when nothing goes in and its tail has died away, so an unused effect costs nothing. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "filters.h"
#include "oversample.h"

namespace dc {

inline float delayTimeSeconds(float v) { return std::min(0.01f * std::pow(150.0f, v / 127.0f), 1.5f); }
/* SYNC: TIME picks one of these note values (in beats) */
constexpr int kDelayDivs = 14;
inline float delayDivBeats(int i)
{
    static const float b[kDelayDivs] = { 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f,
                                         1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    return b[std::clamp(i, 0, kDelayDivs - 1)];
}
inline const char *delayDivName(int i)
{
    static const char *n[kDelayDivs] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
                                         "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1/1" };
    return n[std::clamp(i, 0, kDelayDivs - 1)];
}
inline int delayDivIndex(float v) { return std::clamp((int) std::lround(v / 127.0f * (kDelayDivs - 1)), 0, kDelayDivs - 1); }
constexpr float kDelayMaxSeconds = 4.0f;
inline float delaySeconds(float v, bool sync, float bpm)
{
    if (!sync) return delayTimeSeconds(v);
    return std::min(delayDivBeats(delayDivIndex(v)) * 60.0f / std::max(20.0f, bpm), kDelayMaxSeconds);
}
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
        size = 1; while (size < (int) (sr * (kDelayMaxSeconds + 0.1))) size <<= 1;
        bufL.assign((size_t) size, 0.0f); bufR.assign((size_t) size, 0.0f);
        pos = 0; lpL = lpR = hpL = hpR = 0; timeS = -1; idle = 0; env = 0; wow = flut = 0;
    }
    /* inL/inR: the send bus; adds the wet signal into outL/outR, and (DELAY > REVERB) into revL/revR.
     * seconds: the delay time (FREE or the synced note). fb01: 0 .. 1 = FEEDBACK 0 .. 110 %. tone01: 0.5 flat, below
     * a low pass on the repeats, above a high pass. drive01: tape saturation on what goes onto the tape. mod01: wow
     * and flutter. duck01: how far the repeats dip while the send is playing. */
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 float seconds, float fb01, float tone01, bool pingPong, float level,
                 float drive01, float mod01, float duck01,
                 float *revL = nullptr, float *revR = nullptr, float toRev = 0.0f)
    {
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(inL[i]) + std::fabs(inR[i]));
        /* silent and the repeats have died away (with feedback past 100 % they never do) */
        if (peak == 0.0f && idle > size && fb01 < 0.9f) return;
        idle = peak == 0.0f ? idle + n : 0;
        const float target = std::min(seconds, kDelayMaxSeconds) * sampleRate;
        if (timeS < 0) timeS = target;
        const float fb = fb01 * 1.1f;
        float lpHz, hpHz;
        if (tone01 < 0.5f) { lpHz = 600.0f * std::pow(18000.0f / 600.0f, tone01 * 2.0f); hpHz = 90.0f; }
        else { lpHz = 18000.0f; hpHz = 90.0f * std::pow(1500.0f / 90.0f, (tone01 - 0.5f) * 2.0f); }
        const float lpc = 1.0f - std::exp(-2.0f * (float) M_PI * lpHz / sampleRate);
        const float hpc = 1.0f - std::exp(-2.0f * (float) M_PI * hpHz / sampleRate);
        const float glide = 1.0f - std::exp(-1.0f / (0.08f * sampleRate));
        const float drv = 1.0f + 5.0f * drive01, makeup = 1.0f + 1.5f * drive01;
        const float wowD = mod01 * mod01 * 0.003f * sampleRate, flutD = mod01 * mod01 * 0.0003f * sampleRate;
        const float wowInc = 2.0f * (float) M_PI * 0.6f / sampleRate, flutInc = 2.0f * (float) M_PI * 7.0f / sampleRate;
        const float att = 1.0f - std::exp(-1.0f / (0.005f * sampleRate)), rel = 1.0f - std::exp(-1.0f / (0.25f * sampleRate));
        const int mask = size - 1;
        for (int i = 0; i < n; i++) {
            timeS += (target - timeS) * glide;
            wow += wowInc; if (wow > 2.0f * (float) M_PI) wow -= 2.0f * (float) M_PI;
            flut += flutInc; if (flut > 2.0f * (float) M_PI) flut -= 2.0f * (float) M_PI;
            const float d = std::max(timeS + wowD * (1.0f + std::sin(wow)) + flutD * (1.0f + std::sin(flut)), 1.0f);
            const float rp = (float) pos - d;
            const int i0 = (int) std::floor(rp); const float fr = rp - (float) i0;
            const int a = i0 & mask, b = (i0 + 1) & mask;
            const float dl = bufL[a] + (bufL[b] - bufL[a]) * fr;
            const float dr = bufR[a] + (bufR[b] - bufR[a]) * fr;
            /* feedback path: TONE's low pass and high pass */
            lpL += lpc * (dl - lpL); lpR += lpc * (dr - lpR);
            hpL += hpc * (lpL - hpL); hpR += hpc * (lpR - hpR);
            const float fl = (lpL - hpL) * fb, fr2 = (lpR - hpR) * fb;
            /* onto the tape, through the saturation (it also holds FEEDBACK past 100 % in check) */
            float wl, wr;
            if (pingPong) { wl = (inL[i] + inR[i]) * 0.5f + fr2; wr = fl; }   /* in on the left, repeats cross */
            else { wl = inL[i] + fl; wr = inR[i] + fr2; }
            bufL[pos] = fastTanh(wl * drv) / drv;
            bufR[pos] = fastTanh(wr * drv) / drv;
            /* DUCK: the repeats dip while the send plays */
            const float e = std::fabs(inL[i]) + std::fabs(inR[i]);
            env += (e > env ? att : rel) * (e - env);
            const float g = level * makeup * (1.0f - duck01 * std::min(1.0f, env * 4.0f));
            outL[i] += dl * g; outR[i] += dr * g;
            if (toRev > 0.0f) { revL[i] += dl * makeup * toRev; revR[i] += dr * makeup * toRev; }
            pos = (pos + 1) & mask;
        }
    }

private:
    std::vector<float> bufL, bufR;
    int size = 1, pos = 0, idle = 0;
    float sampleRate = 44100.0f, timeS = -1, lpL = 0, lpR = 0, hpL = 0, hpR = 0, env = 0, wow = 0, flut = 0;
};

/* REVERB: an 8-line feedback delay network (Householder mixing), fed through a pre-delay and four short allpass
 * diffusers per side; the left input feeds the even lines, the right the odd ones, so each machine keeps its place.
 * DECAY: the time to fall 60 dB, 0.3 .. 20 s, and at 127 FREEZE (no loss, no input: the tail holds). SIZE scales the
 * lines (room .. hall). PRE-DELAY 0 .. 250 ms. TONE: a low pass inside the loop below the middle (dark), a high
 * pass on the way out above it (thin). MOD: each line's length wobbles slowly (less metal, then a moving cloud). */
inline float reverbDecaySeconds(float v) { return 0.3f * std::pow(20.0f / 0.3f, std::min(v, 126.0f) / 126.0f); }
inline bool reverbFreeze(float v) { return v >= 126.5f; }
inline float reverbPreDelaySeconds(float v) { const float x = v / 127.0f; return 0.25f * x * x; }

class Reverb {
public:
    void setSampleRate(double sr)
    {
        sampleRate = (float) sr;
        const float k = sampleRate / 44100.0f;
        static const int kDiff[2][4] = { { 142, 107, 379, 277 }, { 151, 113, 389, 263 } };
        for (int c = 0; c < 2; c++)
            for (int j = 0; j < 4; j++) diff[c][j].init((int) (kDiff[c][j] * k));
        static const float kBase[kLines] = { 1433, 1601, 1867, 2053, 2251, 2399, 2617, 2797 };
        lineSize = 1; while (lineSize < (int) (2797 * kSizeMax * k + 64)) lineSize <<= 1;
        for (int i = 0; i < kLines; i++) {
            base[i] = kBase[i] * k;
            line[i].assign((size_t) lineSize, 0.0f); lp[i] = 0; lfo[i] = 0.3f * (float) i;
        }
        preSize = 1; while (preSize < (int) (sampleRate * 0.26f) + 4) preSize <<= 1;
        preL.assign((size_t) preSize, 0.0f); preR.assign((size_t) preSize, 0.0f);
        pos = prePos = 0; sizeS = -1; preS = -1; hpInL = hpInR = 0; idle = 0;
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n,
                 float decayV, float size01, float preV, float tone01, float mod01, float level)
    {
        const bool freeze = reverbFreeze(decayV);
        const float T60 = reverbDecaySeconds(decayV);
        float peak = 0;
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(inL[i]) + std::fabs(inR[i]));
        const int tail = (int) ((T60 * 1.2f + 0.3f) * sampleRate);
        if (!freeze && peak == 0.0f && idle > tail) return;
        idle = (peak == 0.0f && !freeze) ? idle + n : 0;

        const float sizeT = kSizeMin + (kSizeMax - kSizeMin) * size01;
        const float preT = reverbPreDelaySeconds(preV) * sampleRate;
        if (sizeS < 0) sizeS = sizeT;
        if (preS < 0) preS = preT;
        const float slew = 1.0f - std::exp(-1.0f / (0.15f * sampleRate));
        /* TONE: dark half = loop low pass 1.5 k .. 8 k, bright half = loop 8 k .. 16 k and an output high pass 20 .. 400 */
        float dampHz, hpHz;
        if (tone01 < 0.5f) { dampHz = 1500.0f * std::pow(8000.0f / 1500.0f, tone01 * 2.0f); hpHz = 20.0f; }
        else { dampHz = 8000.0f * std::pow(2.0f, (tone01 - 0.5f) * 2.0f); hpHz = 20.0f * std::pow(20.0f, (tone01 - 0.5f) * 2.0f); }
        const float dampC = freeze ? 1.0f : 1.0f - std::exp(-2.0f * (float) M_PI * dampHz / sampleRate);
        const float hpC = 1.0f - std::exp(-2.0f * (float) M_PI * hpHz / sampleRate);
        /* feedback gain per line for this block, from its mean length */
        float g[kLines];
        for (int i = 0; i < kLines; i++)
            g[i] = freeze ? 1.0f : std::pow(10.0f, -3.0f * base[i] * sizeS / (T60 * 1.25f * sampleRate));   /* 1.25: the loop's low pass and the interpolation also take energy */
        /* MOD: a slow sine per line (0.07 .. 0.9 Hz), up to 16 samples, eased across the block */
        /* FREEZE: the wobble fades out and the lines are read on whole samples, so the tail neither loses nor gains */
        const float depthT = freeze ? 0.0f : (0.6f + 15.0f * mod01 * mod01) * sampleRate / 44100.0f;
        const float d0 = depthS;
        depthS += (depthT - depthS) * (1.0f - std::exp(-(float) n / (0.3f * sampleRate)));
        if (freeze && depthS < 0.02f) depthS = 0.0f;
        const bool whole = freeze && depthS == 0.0f;
        float m0[kLines], m1[kLines];
        for (int i = 0; i < kLines; i++) {
            m0[i] = std::sin(lfo[i]) * d0;
            lfo[i] += 2.0f * (float) M_PI * (0.07f + 0.12f * (float) i) * (float) n / sampleRate;
            if (lfo[i] > 2.0f * (float) M_PI) lfo[i] -= 2.0f * (float) M_PI;
            m1[i] = std::sin(lfo[i]) * depthS;
        }
        const float inGain = freeze ? 0.0f : 0.35f;
        const int lm = lineSize - 1, pm = preSize - 1;
        for (int s = 0; s < n; s++) {
            sizeS += (sizeT - sizeS) * slew; preS += (preT - preS) * slew;
            /* pre-delay */
            preL[(size_t) prePos] = inL[s]; preR[(size_t) prePos] = inR[s];
            float xl, xr;
            {
                const float rp = (float) prePos - preS; const int i0 = (int) std::floor(rp); const float fr = rp - (float) i0;
                const int a = i0 & pm, b = (i0 + 1) & pm;
                xl = preL[(size_t) a] + (preL[(size_t) b] - preL[(size_t) a]) * fr;
                xr = preR[(size_t) a] + (preR[(size_t) b] - preR[(size_t) a]) * fr;
            }
            prePos = (prePos + 1) & pm;
            for (int j = 0; j < 4; j++) { xl = diff[0][j].tick(xl); xr = diff[1][j].tick(xr); }
            xl *= inGain; xr *= inGain;
            /* read the lines */
            const float t = (float) s / (float) n;
            float v[kLines], sum = 0.0f;
            for (int i = 0; i < kLines; i++) {
                const float d = base[i] * sizeS + m0[i] + (m1[i] - m0[i]) * t + 2.0f;
                float rp = (float) pos - d;
                if (whole) rp = std::floor(rp + 0.5f);
                const int i0 = (int) std::floor(rp); const float fr = rp - (float) i0;
                const float *L = line[i].data();
                const float a = L[i0 & lm], b = L[(i0 + 1) & lm];
                lp[i] += dampC * (a + (b - a) * fr - lp[i]);
                v[i] = lp[i]; sum += v[i];
            }
            sum *= 2.0f / (float) kLines;                       /* Householder: v - 2/N sum(v) */
            for (int i = 0; i < kLines; i++)
                line[i][(size_t) pos] = (v[i] - sum) * g[i] + ((i & 1) ? xr : xl) + kTiny;
            pos = (pos + 1) & lm;
            float yl = v[0] - v[2] + v[4] - v[6], yr = v[1] - v[3] + v[5] - v[7];
            hpInL += hpC * (yl - hpInL); hpInR += hpC * (yr - hpInR);   /* output high pass (TONE, bright half) */
            yl -= hpInL; yr -= hpInR;
            outL[s] += yl * level * 2.4f; outR[s] += yr * level * 2.4f;
        }
    }

private:
    static constexpr int kLines = 8;
    static constexpr float kSizeMin = 0.25f, kSizeMax = 1.5f, kTiny = 1e-20f;
    struct All {
        std::vector<float> b; int p = 0;
        void init(int len) { b.assign((size_t) std::max(len, 1), 0.0f); p = 0; }
        float tick(float x)
        {
            const float o = b[(size_t) p];
            const float w = x + o * 0.65f;
            b[(size_t) p] = w;
            if (++p >= (int) b.size()) p = 0;
            return o - 0.65f * w;
        }
    };
    All diff[2][4];
    std::vector<float> line[kLines], preL, preR;
    float base[kLines] = {}, lp[kLines] = {}, lfo[kLines] = {};
    int lineSize = 1, preSize = 1, pos = 0, prePos = 0, idle = 0;
    float sampleRate = 44100.0f, sizeS = -1, preS = -1, depthS = 0, hpInL = 0, hpInR = 0;
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

} // namespace dc
