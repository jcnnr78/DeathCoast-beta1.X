/* DeathCoast: knob position (0..127, what the screen shows) -> the value the sound uses. Every curve in one place,
 * so a knob means the same everywhere and a preset keeps sounding the same. (1.8: written for DeathCoast.) */
#pragma once

#include <cmath>

namespace dc {

inline float pot01(float v)      { return v * (1.0f / 127.0f); }          /* 0 .. 1 */
inline float potBipolar(float v) { return (v - 64.0f) * (1.0f / 63.0f); } /* -1 .. 1, 64 = 0 */
inline float potSquared(float v) { const float x = pot01(v); return x * x; }   /* levels and sends */

/* DECAY, RELEASE, FILTER DECAY: 1 ms .. 10 s, the same ratio for every step of the knob */
inline float potEnvTime(float v) { return std::pow(10.0f, 4.0f * pot01(v) - 3.0f); }

/* ATTACK: 0.5 ms .. 10 s on a fifth-power curve, so the useful short attacks (up to ~60 ms at a quarter turn) get
 * most of the knob and 0 stays an instant attack for percussive sounds */
inline float potAttackTime(float v) { const float x = pot01(v), x2 = x * x; return 0.0005f + 10.0f * x2 * x2 * x; }

/* LFO RATE (FREE): 0.02 .. 20 Hz */
inline float potLfoRate(float v) { return 0.02f * std::pow(1000.0f, pot01(v)); }

/* CUTOFF as a MIDI note: 8 .. 135 (about 13 Hz .. 20 kHz) */
inline float potFilterNote(float v) { return 8.0f + 127.0f * pot01(v); }

/* GLIDE: 1 ms .. 2 s */
inline float potGlideTime(float v) { return 0.001f * std::pow(2000.0f, pot01(v)); }

/* WAVETABLE DETUNE: 0 .. 1 semitone across the unison stack */
inline float potDetune(float v) { return pot01(v); }

} // namespace dc
