/* DeathCoast -- 2x oversampling for the machines whose waveshaping aliases (the WEST COAST fold, FM at a high index,
 * hard sync in ANALOG and WEST COAST, the DIRT shaper): the sound is worked out at twice the rate and brought back
 * down through a half-band low pass, so the harmonics past the original Nyquist are filtered out instead of
 * folding back into the audible range as inharmonic hash.
 *
 * Decimator2x: a 31-tap half-band FIR (Blackman-windowed sinc, about -70 dB past 0.75 of the original Nyquist).
 * Every other tap of a half-band filter is zero (all the odd ones but the centre), so an output sample costs 16
 * multiplies plus the centre.
 * Upsampler2x: the same filter run the other way, for an effect fed at the base rate. */
#pragma once

#include <cmath>
#include <cstring>

namespace dc {

struct HalfBand {
    static constexpr int kTaps = 31, kHalf = 15;
    float h[kTaps];
    HalfBand()
    {
        for (int i = 0; i < kTaps; i++) {
            const int k = i - kHalf;
            const double x = k * 0.5;                                   /* cutoff at half the band */
            const double sinc = k == 0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            const double w = 0.42 - 0.5 * std::cos(2 * M_PI * i / (kTaps - 1)) + 0.08 * std::cos(4 * M_PI * i / (kTaps - 1));
            h[i] = (float) (0.5 * sinc * w);
        }
        float sum = 0;
        for (float v : h) sum += v;
        for (float &v : h) v /= sum;                                    /* unity gain at DC */
    }
};

class Decimator2x {
public:
    /* in: 2n samples at the doubled rate; out: n samples */
    void process(const float *in, float *out, int n)
    {
        static const HalfBand hb;
        for (int i = 0; i < n; i++) {
            push(in[2 * i]); push(in[2 * i + 1]);
            /* newest sample at back 0: tap t reads the sample t steps back */
            float acc = hb.h[HalfBand::kHalf] * at(HalfBand::kHalf);
            for (int t = 0; t < HalfBand::kTaps; t += 2) acc += hb.h[t] * at(t);
            out[i] = acc;
        }
    }
    void reset() { std::memset(hist, 0, sizeof hist); p = 0; }

private:
    inline void push(float x) { hist[p] = x; hist[p + kN] = x; p = (p + 1) & (kN - 1); }
    inline float at(int back) const { return hist[p + kN - 1 - back]; }
    static constexpr int kN = 32;
    float hist[2 * kN] = {};
    int p = 0;
};

class Upsampler2x {
public:
    /* in: n samples; out: 2n samples at the doubled rate (zero-stuffed and filtered, gain 2 restores the level) */
    void process(const float *in, float *out, int n)
    {
        static const HalfBand hb;
        for (int i = 0; i < n; i++) {
            push(in[i]);
            /* even output: the even taps over the input; odd output: the centre tap alone */
            float e = 0;
            for (int t = 0; t < HalfBand::kTaps; t += 2) e += hb.h[t] * at(t / 2);
            out[2 * i] = 2.0f * e;
            out[2 * i + 1] = 2.0f * hb.h[HalfBand::kHalf] * at(HalfBand::kHalf / 2);
        }
    }
    void reset() { std::memset(hist, 0, sizeof hist); p = 0; }

private:
    inline void push(float x) { hist[p] = x; hist[p + kN] = x; p = (p + 1) & (kN - 1); }
    inline float at(int back) const { return hist[p + kN - 1 - back]; }
    static constexpr int kN = 16;
    float hist[2 * kN] = {};
    int p = 0;
};

} // namespace dc
