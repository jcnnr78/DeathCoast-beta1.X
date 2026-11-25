/* DeathCoast LFO (1.8: written for DeathCoast, replacing the one it inherited).
 *
 * Shapes: sine, triangle, saw up, saw down, square, sample & hold, smooth random (a new random level each cycle,
 * glided to across the cycle). RATE in Hz, or a note value at the MPC's tempo (SYNC). FADE IN brings the depth up
 * from 0 after a restart. RESTART KEY starts the cycle at 0 on a fresh note; FREE starts it anywhere.
 * Worked out once a 32-sample block (0.7 ms), fine enough that even a fast LFO moves in small steps. Output -1..1,
 * the fade applied. */
#pragma once

#include <cmath>
#include <cstdint>

namespace dc {

enum LfoShape { LFO_SINE = 0, LFO_TRI, LFO_SAW_UP, LFO_SAW_DOWN, LFO_SQUARE, LFO_SH, LFO_SMOOTH, LFO_SHAPES };

/* SYNC: beats (quarter notes) a cycle, slowest first: 2 bars .. 1/32 triplet */
inline constexpr int kLfoDivs = 12;
inline float lfoDivBeats(int i)
{
    static const float beats[kLfoDivs] = { 8.0f, 4.0f, 2.0f, 4.0f / 3.0f, 1.0f, 2.0f / 3.0f,
                                           0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f };
    return beats[i < 0 ? 0 : i >= kLfoDivs ? kLfoDivs - 1 : i];
}

class Lfo {
public:
    void setSampleRate(double sr) { invRate = 1.0f / (float) sr; }

    /* a fresh note: restart (RESTART KEY) or carry on from a random point (FREE), and fade in again */
    void restart(bool fromZero, uint32_t seed)
    {
        rng = seed ? seed : 0x2545F491u;
        pos = fromZero ? 0.0f : random01();
        from = random11(); to = random11();
        depth = 0.0f;
    }

    /* moves on n samples at hz; the value at the end of them */
    float advance(int n, float hz, float fadeSeconds, int shape)
    {
        pos += hz * (float) n * invRate;
        while (pos >= 1.0f) { pos -= 1.0f; from = to; to = random11(); }   /* a new cycle: the next random level */
        depth = fadeSeconds <= 0.001f ? 1.0f : std::fmin(1.0f, depth + (float) n * invRate / fadeSeconds);
        return shapeAt(shape) * depth;
    }

private:
    float shapeAt(int shape) const
    {
        switch (shape) {
        case LFO_SINE:     return std::sin(6.2831853f * pos);
        case LFO_TRI:      return 1.0f - 4.0f * std::fabs(pos - 0.5f);       /* -1 at 0, +1 half way */
        case LFO_SAW_UP:   return 2.0f * pos - 1.0f;
        case LFO_SAW_DOWN: return 1.0f - 2.0f * pos;
        case LFO_SQUARE:   return pos < 0.5f ? 1.0f : -1.0f;
        case LFO_SH:       return from;
        default: {                                           /* smooth random: a cosine glide between levels */
            const float t = 0.5f - 0.5f * std::cos(3.14159265f * pos);
            return from + (to - from) * t;
        }
        }
    }
    uint32_t next() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
    float random01() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float random11() { return random01() * 2.0f - 1.0f; }

    float invRate = 1.0f / 44100.0f, pos = 0.0f, from = 0.0f, to = 0.0f, depth = 1.0f;
    uint32_t rng = 0x2545F491u;
};

} // namespace dc
