/* NorthWest -- the machine filter (Digitone II style: a fixed base-width filter, then one of seven types).
 *
 *   BASE-WIDTH  a 12 dB high pass (BASE) and a 12 dB low pass (WIDTH) in front; each skipped at its end (off)
 *   MULTI       a 12 dB state-variable filter that morphs LP > BP > HP (SHAPE = morph)
 *   LP 4        a 24 dB ladder with drive into its feedback loop; self-oscillates at the top of RESONANCE
 *   COMB - / +  a comb tuned to CUTOFF (negative or positive feedback = RESONANCE), SHAPE damps the repeats
 *   EQ          a peaking band at CUTOFF, RESONANCE = narrowness, SHAPE = gain (64 = 0 dB)
 *   LEGACY LP / HP  the biquad low / high pass of NorthWest 1.0-1.5, SHAPE = slope (12 / 24 dB)
 *
 * The state-variable and ladder filters are zero-delay-feedback (topology-preserving) designs, whose cutoff can move
 * every sample without blowing up; the cutoff glides across each control block (32 samples) sample by sample. */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nw {

enum FilterType { FT_MULTI = 0, FT_LP4, FT_COMB_NEG, FT_COMB_POS, FT_EQ, FT_LEGACY_LP, FT_LEGACY_HP, FT_COUNT };

inline float fastTanh(float x)        /* Pade, clamped: within 2 % of tanh, monotonic */
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---------------------------------------------------------------- the SHAPE knob and the readouts */
inline const char *filterShapeName(int type)
{
    static const char *n[FT_COUNT] = { "MORPH", "DRIVE", "DAMP", "DAMP", "GAIN", "SLOPE", "SLOPE" };
    return n[std::clamp(type, 0, FT_COUNT - 1)];
}
inline float eqGainDb(float v) { return (v - 64.0f) / 63.0f * 18.0f; }
inline bool legacy24(float v) { return v >= 64.0f; }
inline void filterShapeText(char *buf, int len, int type, float v)
{
    const float x = v / 127.0f;
    switch (std::clamp(type, 0, FT_COUNT - 1)) {
    case FT_MULTI:
        if (x < 0.03f) snprintf(buf, len, "LP");
        else if (std::fabs(x - 0.5f) < 0.03f) snprintf(buf, len, "BP");
        else if (x > 0.97f) snprintf(buf, len, "HP");
        else if (x < 0.5f) snprintf(buf, len, "LP>BP %d%%", (int) std::lround(x * 200.0f));
        else snprintf(buf, len, "BP>HP %d%%", (int) std::lround((x - 0.5f) * 200.0f));
        break;
    case FT_EQ: snprintf(buf, len, "%+.1f dB", (double) eqGainDb(v)); break;
    case FT_LEGACY_LP: case FT_LEGACY_HP: snprintf(buf, len, legacy24(v) ? "24 dB" : "12 dB"); break;
    default: snprintf(buf, len, "%d %%", (int) std::lround(x * 100.0f)); break;
    }
}
/* base-width: 20 Hz .. 20 kHz; BASE 0 and WIDTH 127 are off */
inline float baseWidthHz(float v) { return 20.0f * std::pow(1000.0f, v / 127.0f); }
inline void baseWidthText(char *buf, int len, float v, bool width)
{
    if ((!width && v < 0.5f) || (width && v > 126.5f)) { snprintf(buf, len, "OFF"); return; }
    const float hz = baseWidthHz(v);
    if (hz < 1000.0f) snprintf(buf, len, "%d Hz", (int) std::lround(hz));
    else snprintf(buf, len, "%.1f kHz", (double) (hz / 1000.0f));
}

/* the filter picture: a frame of the type's strip (images/nw/flt_<type>_*.png). MULTI: CUTOFF 16 x MORPH 4 x
 * RESONANCE 3 (192); EQ: CUTOFF 16 x GAIN 8; the others: CUTOFF 32 x RESONANCE 4 */
inline int filterView(int type, float cutoff, float reso, float shape)
{
    auto q = [](float v, int steps) { return std::clamp((int) std::lround(v / 127.0f * (float) (steps - 1)), 0, steps - 1); };
    switch (std::clamp(type, 0, FT_COUNT - 1)) {
    case FT_MULTI: return (q(cutoff, 16) * 4 + q(shape, 4)) * 3 + q(reso, 3);
    case FT_EQ: return q(cutoff, 16) * 8 + q(shape, 8);
    default: return q(cutoff, 32) * 4 + q(reso, 4);
    }
}

/* ---------------------------------------------------------------- building blocks */

/* zero-delay-feedback state-variable filter (Simper), one channel */
struct Svf {
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    /* g = tan(pi fc / fs), k = 1 / Q; outputs low, band (unity peak: k * band), high */
    inline void tick(float x, float g, float k, float &lp, float &bp, float &hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

/* 4-pole ladder, zero-delay feedback solved exactly for the linear part, tanh on the loop's input */
struct Ladder {
    float s[4] = {};
    void reset() { std::memset(s, 0, sizeof s); }
    inline float tick(float x, float g, float k, float drive)
    {
        const float G = g / (1.0f + g), H = 1.0f - G;            /* one-pole TPT: y = G x + H s */
        const float S1 = H * s[0], S2 = H * s[1], S3 = H * s[2], S4 = H * s[3];
        const float sigma = G * G * G * S1 + G * G * S2 + G * S3 + S4;
        const float G4 = G * G * G * G;
        float u = (x - k * sigma) / (1.0f + k * G4);
        u = fastTanh(u * drive);
        float y = u;
        for (int i = 0; i < 4; i++) {
            const float v = (y - s[i]) * G;
            const float o = v + s[i];
            s[i] = o + v;
            y = o;
        }
        return y;
    }
};

/* RBJ biquad (transposed direct form II) */
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float tick(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set(float nb0, float nb1, float nb2, float a0, float na1, float na2)
    {
        const float i = 1.0f / a0;
        b0 = nb0 * i; b1 = nb1 * i; b2 = nb2 * i; a1 = na1 * i; a2 = na2 * i;
    }
    void lowpass(float w0, float Q)
    {
        const float c = std::cos(w0), al = std::sin(w0) / (2.0f * Q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(float w0, float Q)
    {
        const float c = std::cos(w0), al = std::sin(w0) / (2.0f * Q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak(float w0, float Q, float db)
    {
        const float A = std::pow(10.0f, db / 40.0f), c = std::cos(w0), al = std::sin(w0) / (2.0f * Q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
};

/* ---------------------------------------------------------------- the machine filter (stereo) */
class MachineFilter {
public:
    void setSampleRate(float sr) { fs = sr; }
    void reset()
    {
        for (int c = 0; c < 2; c++) {
            svf[c].reset(); lad[c].reset(); bwLo[c].reset(); bwHi[c].reset();
            for (auto &b : bq[c]) b.reset();
            std::memset(comb[c], 0, sizeof comb[c]);
            damp[c] = 0;
        }
        pos = 0; lastType = -1;
    }

    /* fn0 -> fn1: the cutoff (MIDI note units) at the block's start and end; res 0..1; shape 0..127 */
    void process(int type, float fn0, float fn1, float res, float shape, float bwloV, float bwhiV,
                 float *L, float *R, int n)
    {
        type = std::clamp(type, 0, FT_COUNT - 1);
        if (type != lastType) {                           /* a new type starts from rest */
            for (int c = 0; c < 2; c++) { svf[c].reset(); lad[c].reset(); for (auto &b : bq[c]) b.reset(); }
            lastType = type;
        }
        baseWidth(bwloV, bwhiV, L, R, n);
        const float nyq = fs * 0.49f;
        const float hz0 = std::clamp(noteHz(fn0), 8.0f, nyq), hz1 = std::clamp(noteHz(fn1), 8.0f, nyq);
        switch (type) {
        case FT_MULTI: {
            const float Q = 0.6f * std::pow(40.0f, res), k = 1.0f / Q;
            const float m = shape / 127.0f;
            /* morph weights: LP -> BP -> HP */
            const float wl = m < 0.5f ? 1.0f - 2.0f * m : 0.0f;
            const float wb = m < 0.5f ? 2.0f * m : 2.0f - 2.0f * m;
            const float wh = m < 0.5f ? 0.0f : 2.0f * m - 1.0f;
            float g = gOf(hz0); const float dg = (gOf(hz1) - g) / (float) n;
            for (int i = 0; i < n; i++, g += dg) {
                float lp, bp, hp;
                svf[0].tick(L[i], g, k, lp, bp, hp); L[i] = wl * lp + wb * k * bp + wh * hp;
                svf[1].tick(R[i], g, k, lp, bp, hp); R[i] = wl * lp + wb * k * bp + wh * hp;
            }
            break; }
        case FT_LP4: {
            const float k = res * 4.3f;                   /* self-oscillation from ~4 */
            const float x = shape / 127.0f, drive = 1.0f + 7.0f * x * x;
            const float makeup = (1.0f + 0.45f * k) / std::sqrt(drive);   /* the ladder's bass loss with resonance */
            float g = gOf(hz0); const float dg = (gOf(hz1) - g) / (float) n;
            for (int i = 0; i < n; i++, g += dg) {
                L[i] = lad[0].tick(L[i], g, k, drive) * makeup;
                R[i] = lad[1].tick(R[i], g, k, drive) * makeup;
            }
            break; }
        case FT_COMB_NEG: case FT_COMB_POS: {
            const float fb = (type == FT_COMB_POS ? 1.0f : -1.0f) * 0.985f * std::pow(res, 0.6f);
            const float dmp = shape / 127.0f * 0.9f;      /* one-pole low pass in the loop */
            const float out = std::sqrt(1.0f - std::fabs(fb));             /* the comb's peaks stay near unity */
            float d = fs / std::max(hz0, 12.0f); const float dd = (fs / std::max(hz1, 12.0f) - d) / (float) n;
            for (int i = 0; i < n; i++, d += dd) {
                const float D = std::clamp(d, 1.0f, (float) (kComb - 2));
                const float rp = (float) pos - D;
                const int i0 = (int) std::floor(rp); const float fr = rp - (float) i0;
                const int a = i0 & (kComb - 1), b = (i0 + 1) & (kComb - 1);
                for (int c = 0; c < 2; c++) {
                    float *buf = comb[c];
                    const float dl = buf[a] + (buf[b] - buf[a]) * fr;
                    damp[c] = dl + (damp[c] - dl) * dmp;
                    float *io = c ? R : L;
                    const float y = io[i] + fb * damp[c];
                    buf[pos] = y;
                    io[i] = y * out;
                }
                pos = (pos + 1) & (kComb - 1);
            }
            break; }
        case FT_EQ: {
            const float Q = 0.5f * std::pow(16.0f, res);
            for (int c = 0; c < 2; c++) bq[c][0].peak(w0Of(0.5f * (hz0 + hz1)), Q, eqGainDb(shape));
            for (int i = 0; i < n; i++) { L[i] = bq[0][0].tick(L[i]); R[i] = bq[1][0].tick(R[i]); }
            break; }
        default: {                                        /* LEGACY LP / HP: 1.0-1.5's filter */
            const float Q = 0.70710678f / (1.0f - res * 0.99f);
            const int stages = legacy24(shape) ? 2 : 1;
            /* the cutoff in 4 steps across the block, as it always moved */
            const int sub = n >= 16 ? 4 : 1, len = (n + sub - 1) / sub;
            for (int j = 0, o = 0; j < sub && o < n; j++, o += len) {
                const float hz = hz0 + (hz1 - hz0) * (float) (j + 1) / (float) sub;
                for (int c = 0; c < 2; c++)
                    for (int s = 0; s < stages; s++)
                        type == FT_LEGACY_LP ? bq[c][s].lowpass(w0Of(hz), Q) : bq[c][s].highpass(w0Of(hz), Q);
                const int m = std::min(len, n - o);
                for (int s = 0; s < stages; s++)
                    for (int i = 0; i < m; i++) { L[o + i] = bq[0][s].tick(L[o + i]); R[o + i] = bq[1][s].tick(R[o + i]); }
            }
            break; }
        }
    }

    /* LP types fully open with no resonance and no base-width: the filter can be skipped */
    static bool transparent(int type, float fnMin, float res, float shape, float bwloV, float bwhiV)
    {
        if (bwloV > 0.5f || bwhiV < 126.5f) return false;
        if (type == FT_EQ) return std::fabs(eqGainDb(shape)) < 0.05f;
        const bool lp = type == FT_LEGACY_LP || (type == FT_MULTI && shape < 0.5f) || (type == FT_LP4 && shape < 0.5f);
        return lp && res < 0.01f && noteHz(fnMin) >= 19000.0f;
    }

private:
    static float noteHz(float n) { return 440.0f * std::exp2((n - 69.0f) / 12.0f); }
    float gOf(float hz) const { return std::tan((float) M_PI * std::min(hz, fs * 0.49f) / fs); }
    float w0Of(float hz) const { return 2.0f * (float) M_PI * std::min(hz, fs * 0.49f) / fs; }

    void baseWidth(float lo, float hi, float *L, float *R, int n)
    {
        const bool doLo = lo > 0.5f, doHi = hi < 126.5f;
        if (!doLo && !doHi) return;
        const float k = 1.41421356f;                      /* Q 0.707 */
        const float gl = doLo ? gOf(baseWidthHz(lo)) : 0.0f, gh = doHi ? gOf(baseWidthHz(hi)) : 0.0f;
        for (int i = 0; i < n; i++) {
            float lp, bp, hp;
            if (doLo) { bwLo[0].tick(L[i], gl, k, lp, bp, hp); L[i] = hp; bwLo[1].tick(R[i], gl, k, lp, bp, hp); R[i] = hp; }
            if (doHi) { bwHi[0].tick(L[i], gh, k, lp, bp, hp); L[i] = lp; bwHi[1].tick(R[i], gh, k, lp, bp, hp); R[i] = lp; }
        }
    }

    static constexpr int kComb = 4096;                    /* 93 ms at 44.1 kHz: down to ~11 Hz */
    float fs = 44100.0f;
    Svf svf[2], bwLo[2], bwHi[2];
    Ladder lad[2];
    Biquad bq[2][2];
    float comb[2][kComb] = {};
    float damp[2] = {};
    int pos = 0, lastType = -1;
};

} // namespace nw
