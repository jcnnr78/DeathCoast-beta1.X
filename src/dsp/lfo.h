/* NorthWest LFO -- one per voice, block-rate output.
 *
 * Shapes: sine, triangle, saw up, saw down, square, sample & hold, smooth
 * random. Free (Hz) or tempo-synced rate, a fade-in, and key-retrigger or
 * random start phase. The value is read once per block (kBlock = 128 samples,
 * ~2.9 ms), which is plenty for modulation and keeps the cost negligible.
 */
#pragma once
#include <cmath>
#include <cstdint>

namespace nw {

enum LfoShape { LFO_SINE = 0, LFO_TRI, LFO_SAW_UP, LFO_SAW_DOWN, LFO_SQUARE, LFO_SH, LFO_SMOOTH, LFO_SHAPES };

/* beats (quarter notes) per cycle for the 12 sync divisions */
inline constexpr int kLfoDivs = 12;
inline float lfoDivBeats(int i)
{
    static const float b[kLfoDivs] = { 8.0f, 4.0f, 2.0f, 4.0f / 3.0f, 1.0f, 2.0f / 3.0f,
                                       0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f };
    return b[i < 0 ? 0 : (i >= kLfoDivs ? kLfoDivs - 1 : i)];
}

class Lfo {
public:
    void setSampleRate(double sr) { sampleRate = sr; }

    void noteOn(bool keyRetrig, uint32_t seed)
    {
        rs = seed ? seed : 0x1234567u;
        phase = keyRetrig ? 0.0f : rand01();
        held = rand01() * 2.0f - 1.0f;
        next = rand01() * 2.0f - 1.0f;
        fade = 0.0f;
        lastPhase = phase;
    }

    /* advance by n samples at rate hz; returns the value in -1..1 (fade applied) */
    float process(int n, float hz, float fadeSeconds, int shape)
    {
        const float inc = hz * (float) n / (float) sampleRate;
        lastPhase = phase;
        phase += inc;
        while (phase >= 1.0f) {                        /* a cycle boundary: draw new random values */
            phase -= 1.0f;
            held = next;
            next = rand01() * 2.0f - 1.0f;
        }
        if (fadeSeconds <= 0.001f) fade = 1.0f;
        else { fade += (float) n / (float) sampleRate / fadeSeconds; if (fade > 1.0f) fade = 1.0f; }

        float v;
        switch (shape) {
        case LFO_SINE:     v = std::sin(6.28318530718f * phase); break;
        case LFO_TRI:      v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; break;
        case LFO_SAW_UP:   v = 2.0f * phase - 1.0f; break;
        case LFO_SAW_DOWN: v = 1.0f - 2.0f * phase; break;
        case LFO_SQUARE:   v = phase < 0.5f ? 1.0f : -1.0f; break;
        case LFO_SH:       v = held; break;
        default:           v = held + (next - held) * phase; break;      /* smooth random */
        }
        return v * fade;
    }

private:
    float rand01()
    {
        rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
        return (float) (rs & 0xFFFFFF) / 16777216.0f;
    }
    double sampleRate = 44100.0;
    float phase = 0.0f, lastPhase = 0.0f, held = 0.0f, next = 0.0f, fade = 1.0f;
    uint32_t rs = 0x1234567u;
};

} // namespace nw
