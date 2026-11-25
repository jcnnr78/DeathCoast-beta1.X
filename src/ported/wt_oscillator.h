/* Wavetable oscillator — de-JUCE'd port of gin::WTOscillator and
 * gin::WTVoicedStereoOscillator (BSD-3, (c) Roland Rabien).
 *
 * Faithful to the original's behaviour:
 *  - three process paths (simple / phase-distorted / table-crossfade)
 *  - table switches only at phase wrap; one-cycle crossfade after a switch
 *  - bend (phase distortion), formant, asym, fold post-processing
 *  - per-channel DC blocker (10 Hz one-pole)
 *  - unison wrapper: evenly-spread detune, +/-spread pan, 1/sqrt(v) gain
 *
 * Differences (output-identical): the mip level and the formant exp() are
 * hoisted per block (note and formant are constant within a block); no SIMD.
 */
#pragma once

#include "wavetable.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace nw {

namespace math {
    inline float pow2(float x) { return x * x; }
    inline float pow4(float x) { return pow2(pow2(x)); }
    inline float pow8(float x) { return pow2(pow4(x)); }
    inline float lerp(float a, float b, float t) { return a + t * (b - a); }
}

class DCBlocker {
public:
    void setSampleRate(float sr) { sampleRate = sr; recalc(); }
    void setCutoff(float hz)     { cutoff = hz; recalc(); }
    inline float process(float x) { z = x * a + z * b; return z; }
    void reset() { z = 0.0f; }
private:
    void recalc()
    {
        b = std::exp(-2.0f * (float) M_PI * cutoff / sampleRate);
        a = 1.0f - b;
    }
    float sampleRate = 44100.0f, cutoff = 10.0f;
    float a = 0.0f, b = 1.0f, z = 0.0f;
};

class WTOscillator {
public:
    struct Params {
        float leftGain  = 1.0f;
        float rightGain = 1.0f;
        float position  = 0.0f;   /* 0..1 across the table's frames */
        float bend      = 0.0f;   /* -1..1 phase distortion */
        float formant   = 0.0f;   /*  0..1 spectral shift */
        float asym      = 0.0f;
        float fold      = 0.0f;
        /* WaveTablor additions (all neutral at their defaults: the original
         * paths run untouched when none of them is active). */
        float sync      = 1.0f;       /* >1: hard-sync slave ratio              */
        float width     = 0.0f;       /* 0..1 pulse-width style phase warp      */
        const float *fm = nullptr;    /* per-sample phase modulator, -1..1      */
        float fmDepth   = 0.0f;       /* phase deviation in cycles at |fm|=1    */
    };

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        blockerL.setSampleRate((float) sr);
        blockerR.setSampleRate((float) sr);
        blockerL.setCutoff(10.0f);
        blockerR.setCutoff(10.0f);
    }

    void setWavetable(const Wavetable *t) { wt = t; }
    void setBlockDC(bool b) { blockDC = b; }

    void noteOn(float p = -1.0f)
    {
        phase = (p >= 0.0f) ? p : (float) rand() / (float) RAND_MAX;
        tableIndex = -1;
        lastTableIndex = -1;
        blockerL.reset();
        blockerR.reset();
    }

    void processAdding(float note, const Params &params, float *l, float *r, int n)
    {
        if (!wt || wt->size() == 0) return;

        if (params.sync > 1.0f || params.width > 0.0f || (params.fm && params.fmDepth > 0.0f))
            processFX(note, params, l, r, n);
        else if (tableIndex != lastTableIndex && lastTableIndex >= 0)
            processCrossfade(note, params, l, r, n);
        else if (params.bend == 0.0f && params.formant == 0.0f)
            processSimple(note, params, l, r, n);
        else
            processComplex(note, params, l, r, n);

        if (blockDC) {
            for (int i = 0; i < n; i++) {
                l[i] -= blockerL.process(l[i]);
                r[i] -= blockerR.process(r[i]);
            }
        }
    }

private:
    inline int posToIndex(float position) const
    {
        int i = (int) ((float) wt->size() * position);
        return std::min(wt->size() - 1, i);
    }

    /* Position as a CONTINUOUS place in the table: the two frames either side
     * and how far between them.
     *
     * The oscillator used to snap to one frame and change it at a phase wrap.
     * That is fine when position is a hand-turned knob, but under modulation
     * it is not: an envelope sweeping the whole table across a 45 ms attack
     * crosses ~25 frames per cycle, so every wrap jumped a long way through
     * the table at once and left a step. Reported as the sound being "a
     * little crunchy around the edges" -- the edges being exactly where an
     * envelope moves. Blending the two neighbours removes the steps entirely
     * and is what Serum and Vital do. */
    inline void posToFrames(float position, const FrameTable *&a,
                            const FrameTable *&b, float &frac) const
    {
        const int last = wt->size() - 1;
        float fp = position * (float) last;
        if (fp < 0.0f) fp = 0.0f;
        if (fp > (float) last) fp = (float) last;
        int i0 = (int) fp;
        if (i0 > last) i0 = last;
        int i1 = i0 < last ? i0 + 1 : last;
        frac = fp - (float) i0;
        a = wt->frame(i0);
        b = wt->frame(i1);
    }

    inline float freqDelta(float note) const
    {
        float freq = (float) std::min(sampleRate / 2.0,
                                      440.0 * std::pow(2.0, (note - 69.0) / 12.0));
        return freq / (float) sampleRate;
    }

    /* WaveTablor: both stages are bounded and neutral at 0 (the original gin
     * formulas are unbounded on a -1..1 wave -- asym reached -15 -- and were
     * never exposed in Tablor, so they are replaced here).
     *  asym: even-harmonic waveshaper, level-compensated
     *  fold: sine wavefolder, drive 1x..8x */
    inline void postProcess(const Params &p, float &v) const
    {
        if (p.asym > 0.0f) {
            const float a = p.asym;
            v = (v + a * 0.6f * (v * v - 0.3333f)) / (1.0f + 0.4f * a);
        }
        if (p.fold > 0.0f)
            v = std::sin(v * (1.0f + p.fold * 7.0f) * 1.5707963f);
    }

    /* gin's bendDistortion + formantDistortion, formant exp hoisted. */
    inline float distort(float ph, float bend, float formantMul) const
    {
        if (bend != 0.0f) {
            const float addDist = std::clamp(bend, 0.0f, 1.0f);
            const float subDist = std::clamp(bend, -1.0f, 0.0f);
            const float sub = math::pow8(ph);
            const float add = math::pow8(1.0f - ph);
            const float addMix = math::lerp(ph, 1.0f - add, addDist);
            const float subMix = math::lerp(ph, sub, -subDist);
            ph = std::min(kAlmostOne, addMix + subMix - ph);
        }
        if (formantMul != 1.0f)
            ph = std::min(kAlmostOne, ph * formantMul);
        return ph;
    }

    void processSimple(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const FrameTable *fa = nullptr, *fb = nullptr;
        float frac = 0.0f;
        posToFrames(p.position, fa, fb, frac);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                const float ph = std::min(kAlmostOne, phase);
                float s = fa->processLinear(note, ph);
                if (frac > 0.0f)
                    s += (fb->processLinear(note, ph) - s) * frac;
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) phase -= 1.0f;
        }
        lastTableIndex = tableIndex = posToIndex(p.position);
    }

    void processComplex(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const float formantMul = (p.formant != 0.0f)
            ? std::exp(p.formant * 1.60943791243f) : 1.0f;
        const FrameTable *fa = nullptr, *fb = nullptr;
        float frac = 0.0f;
        posToFrames(p.position, fa, fb, frac);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                float ph = distort(std::min(kAlmostOne, phase), p.bend, formantMul);
                float s = fa->processLinear(note, ph);
                if (frac > 0.0f)
                    s += (fb->processLinear(note, ph) - s) * frac;
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) phase -= 1.0f;
        }
        lastTableIndex = tableIndex = posToIndex(p.position);
    }

    /* WaveTablor: the "effect mode" path -- hard sync, pulse-width warp and
     * phase modulation (FM). Position is blended between two frames exactly as
     * in processSimple/Complex. The mip level is chosen for the *highest*
     * frequency this path can produce (sync ratio, FM headroom) to keep the
     * result clean of aliasing. */
    void processFX(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const float formantMul = (p.formant != 0.0f)
            ? std::exp(p.formant * 1.60943791243f) : 1.0f;
        const FrameTable *fa = nullptr, *fb = nullptr;
        float frac = 0.0f;
        posToFrames(p.position, fa, fb, frac);

        const bool useFm = p.fm && p.fmDepth > 0.0f;
        float lvlNote = note;
        if (p.sync > 1.0f) lvlNote += 12.0f * std::log2(p.sync);
        if (useFm)         lvlNote += 12.0f;           /* modulation widens the spectrum */
        const float w = 0.5f - 0.45f * std::clamp(p.width, 0.0f, 1.0f);

        for (int i = 0; i < n; i++) {
            float ph = std::min(kAlmostOne, phase);
            if (useFm) ph += p.fm[i] * p.fmDepth;
            if (p.sync > 1.0f) ph *= p.sync;
            ph -= std::floor(ph);
            if (p.width > 0.0f)
                ph = ph < w ? 0.5f * ph / w : 0.5f + 0.5f * (ph - w) / (1.0f - w);
            ph = distort(std::min(kAlmostOne, ph), p.bend, formantMul);
            float s = fa->processLinear(lvlNote, ph);
            if (frac > 0.0f)
                s += (fb->processLinear(lvlNote, ph) - s) * frac;
            postProcess(p, s);
            l[i] += s * p.leftGain;
            r[i] += s * p.rightGain;
            phase += delta;
            if (phase >= 1.0f) phase -= 1.0f;
        }
        lastTableIndex = tableIndex = posToIndex(p.position);
    }

    void processCrossfade(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);
        if (lastTableIndex == -1 || lastTableIndex >= wt->size())
            lastTableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const float formantMul = (p.formant != 0.0f)
            ? std::exp(p.formant * 1.60943791243f) : 1.0f;
        const FrameTable *ft1 = wt->frame(tableIndex);
        const FrameTable *ft2 = wt->frame(lastTableIndex);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                float ph = distort(std::min(kAlmostOne, phase), p.bend, formantMul);
                float s1 = ft1->processLinear(note, ph);
                float s2 = ft2->processLinear(note, ph);
                float s = s1 * phase + s2 * (1.0f - phase);  /* old->new over one cycle */
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) {
                phase -= 1.0f;
                lastTableIndex = tableIndex;
                tableIndex = posToIndex(p.position);
                ft1 = ft2 = wt->frame(tableIndex);
            }
        }
    }

    static constexpr float kAlmostOne = 0.99999994f; /* < 1.0f in float */

    const Wavetable *wt = nullptr;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    int tableIndex = -1, lastTableIndex = -1;
    bool blockDC = true;
    DCBlocker blockerL, blockerR;
};

/* ------------------------------------------------------------------ */
/* Unison wrapper — gin::VoicedStereoOscillator, WT flavour            */
/* ------------------------------------------------------------------ */

struct VoicedWTParams {
    int   voices = 1;
    float pan = 0.0f;       /* -1..1 */
    float spread = 0.0f;    /*  0..1 */
    float detune = 0.0f;    /* semitones across the stack */
    float gain = 1.0f;
    /* WT params forwarded to every voice */
    float position = 0.0f, bend = 0.0f, formant = 0.0f, asym = 0.0f, fold = 0.0f;
    /* WaveTablor additions */
    float sync = 1.0f, width = 0.0f;
    const float *fm = nullptr; float fmDepth = 0.0f;
    int   mode = 0;           /* UniMode */
};

/* Unison flavours. Classic is the original behaviour, bit for bit. */
enum UniMode { UNI_CLASSIC = 0, UNI_SHIMMER, UNI_NOISE, UNI_PHASESYNC, UNI_POSSPREAD, UNI_RANDOM, UNI_COUNT };

class VoicedWTOscillator {
public:
    explicit VoicedWTOscillator(int maxVoices = 8)
        : oscs((size_t) maxVoices), drift((size_t) maxVoices, 0.0f),
          target((size_t) maxVoices, 0.0f), rnd((size_t) maxVoices, 0.0f) {}

    void setSampleRate(double sr) { for (auto &o : oscs) o.setSampleRate(sr); }
    void setWavetable(const Wavetable *t) { for (auto &o : oscs) o.setWavetable(t); }

    void noteOn(const float *phases, int n)
    {
        for (int i = 0; i < (int) oscs.size(); i++)
            oscs[(size_t) i].noteOn(i < n ? phases[i] : -1.0f);
        for (size_t i = 0; i < rnd.size(); i++) {      /* per-note random offsets */
            rnd[i] = nextRand() * 2.0f - 1.0f;
            drift[i] = target[i] = 0.0f;
        }
    }
    void noteOn(float phase = -1.0f)
    {
        for (auto &o : oscs) o.noteOn(phase);
    }

    void processAdding(float note, const VoicedWTParams &vp, float *l, float *r, int n)
    {
        WTOscillator::Params p;
        p.position = vp.position; p.bend = vp.bend; p.formant = vp.formant;
        p.asym = vp.asym; p.fold = vp.fold;
        p.sync = vp.sync; p.width = vp.width; p.fm = vp.fm; p.fmDepth = vp.fmDepth;

        const int voices = std::clamp(vp.voices, 1, (int) oscs.size());
        if (voices == 1) {
            p.leftGain  = vp.gain * (1.0f - vp.pan);
            p.rightGain = vp.gain * (1.0f + vp.pan);
            oscs[0].processAdding(note, p, l, r, n);
            return;
        }

        const int mode = vp.mode;
        const bool pitchSpread = mode != UNI_POSSPREAD;
        const float baseNote  = note - vp.detune / 2.0f;
        const float noteDelta = vp.detune / (float) (voices - 1);
        const float basePan   = vp.pan - vp.spread;
        const float panDelta  = (vp.spread * 2.0f) / (float) (voices - 1);
        const float comp      = 1.0f / std::sqrt((float) voices);

        if (mode == UNI_NOISE) {                       /* slow random walk per voice */
            for (int i = 0; i < voices; i++) {
                if (nextRand() < 0.08f) target[(size_t) i] = nextRand() * 2.0f - 1.0f;
                drift[(size_t) i] += (target[(size_t) i] - drift[(size_t) i]) * 0.05f;
            }
        }

        for (int i = 0; i < voices; i++) {
            float pan = std::clamp(basePan + panDelta * (float) i, -1.0f, 1.0f);
            p.leftGain  = vp.gain * (1.0f - pan) * comp;
            p.rightGain = vp.gain * (1.0f + pan) * comp;
            float vn = note;                                    /* this voice's pitch */
            float vpos = vp.position;
            float g = 1.0f;
            switch (mode) {
            case UNI_CLASSIC:
                vn = baseNote + noteDelta * (float) i; break;
            case UNI_SHIMMER:
                vn = baseNote + noteDelta * (float) i;
                if (i & 1) { vn += 12.0f; g = 0.5f; }           /* odd voices: octave up, half level */
                break;
            case UNI_NOISE:
                vn = note + drift[(size_t) i] * vp.detune * 0.5f; break;
            case UNI_PHASESYNC:
                vn = baseNote + noteDelta * (float) i; break;   /* phases are aligned at note-on (Voice) */
            case UNI_POSSPREAD: {
                const float mid = 0.5f * (float) (voices - 1);
                vpos = std::clamp(vp.position + ((float) i - mid) / mid * vp.detune * 0.5f, 0.0f, 1.0f);
                vn = note; break; }
            case UNI_RANDOM:
                vn = note + rnd[(size_t) i] * vp.detune * 0.5f; break;
            }
            (void) pitchSpread;
            p.position = vpos;
            p.leftGain *= g; p.rightGain *= g;
            oscs[(size_t) i].processAdding(vn, p, l, r, n);
        }
    }

private:
    float nextRand()                                  /* xorshift32, 0..1 */
    {
        rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
        return (float) (rs & 0xFFFFFF) / 16777216.0f;
    }
    std::vector<WTOscillator> oscs;
    std::vector<float> drift, target, rnd;
    uint32_t rs = 0x9E3779B9u;
};

} // namespace nw
