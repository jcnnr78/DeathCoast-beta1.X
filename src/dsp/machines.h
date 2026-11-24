/* NorthWest v2.0 -- the four slots and their machines.
 *
 *   Engine: note stack, glide mode, shared modulation (EG1/EG2, LFO1/LFO2, random, controllers)
 *     └─ Slot x4: machine (WAVETABLE | FM | WEST COAST* | ANALOG*) -> filter -> amp env -> level/pan
 *        (* v2.1 / v2.2: silent until then)
 *
 * Every slot plays every note (layers), monophonically. The machine of a slot is read when a note
 * starts; a change while a note sounds applies from the next note. Each slot owns 12 machine knobs
 * K1..K12 whose meaning depends on the machine (knobName / knobText below are what MPC shows).
 */
#pragma once

#include "../ported/wt_oscillator.h"
#include "../ported/filter.h"
#include "../ported/adsr.h"
#include "lfo.h"
#include "param_map.h"
#include "params.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tb {

inline constexpr int kBlock = 128;
inline constexpr int kMatrixRows = 8;
inline constexpr int kModEnvs = 2;

enum Machine { M_WAVETABLE = 0, M_FM, M_WESTCOAST, M_ANALOG, M_COUNT };
enum GlideMode { GM_OFF = 0, GM_GLISSANDO, GM_PORTAMENTO, GM_LEGATO };
/* option order of mmN_src / mmN_dst in tools/gen_params_v2.py: one contract */
enum ModSrc { MS_NONE = 0, MS_EG1, MS_EG2, MS_LFO1, MS_LFO2, MS_VELOCITY, MS_KEY, MS_MODWHEEL,
              MS_AFTERTOUCH, MS_PITCHBEND, MS_RANDOM, MS_AMPENV, MS_FLTENV, MS_COUNT };
enum ModDst { MD_NONE = 0, MD_PITCH, MD_LEVEL, MD_PAN, MD_CUTOFF, MD_RESO,
              MD_TA, MD_TB, MD_TC, MD_TD, MD_COUNT };

inline float velocityToGain(float velocity, float sensitivity)
{
    float v = velocity * sensitivity + 1.0f - sensitivity;
    float g = v * std::pow(25.0f, v) * 0.04f;
    return std::clamp(g, 0.0f, 1.0f);
}

/* a slot's parameter: P[slotParam(n, TB_P_M1_CUTOFF)] */
inline int slotParam(int slot, int s1Index) { return s1Index + slot * TB_SLOT_STRIDE; }

/* ------------------------------------------------------------------ knob meanings */
/* WAVETABLE: K1 POSITION, K2 FX 1, K3 FX 2, K4 DETUNE, K5 SPREAD, K7 UNISON (1..4)
 * FM:        K1-K3 OP2-OP4 LEVEL, K4 FEEDBACK, K5 PITCH ENV, K6 NOISE, K7 MOD DECAY,
 *            K8-K10 OP2-OP4 RATIO, K11 PITCH DECAY, K12 VEL > FM
 * Matrix TIMBRE A..D = K1..K4. */
inline int wtUnison(float v) { return 1 + (int) std::lround(v / 127.0f * 3.0f); }

inline float fmRatio(float v)
{
    static const float lo[4] = { 0.25f, 0.333f, 0.5f, 0.75f };
    static const float hi[23] = { 1.0f, 1.41f, 1.5f, 2.0f, 2.5f, 2.76f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.4f,
                                  6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
    if (v < 64.0f) return lo[std::clamp((int) (v / 64.0f * 4.0f), 0, 3)];
    return hi[std::clamp((int) ((v - 64.0f) / 64.0f * 23.0f), 0, 22)];
}
/* MOD DECAY / PITCH DECAY: 5 ms .. 4 s; MOD DECAY at 127 holds (no decay) */
inline float fmDecaySeconds(float v) { return 0.005f * std::pow(800.0f, v / 127.0f); }

inline const char *knobName(int machine, int k, int fxMode)
{
    static const char *wt[12] = { "POSITION", "FX 1", "FX 2", "DETUNE", "SPREAD", "-", "UNISON", "-", "-", "-", "-", "-" };
    static const char *fxn[5][2] = { { "FX 1", "FX 2" }, { "FM AMOUNT", "FM RATIO" }, { "SYNC", "PULSE WIDTH" },
                                     { "ASYMMETRY", "FOLD" }, { "BEND", "FORMANT" } };
    static const char *fm[12] = { "OP2 LEVEL", "OP3 LEVEL", "OP4 LEVEL", "FEEDBACK", "PITCH ENV", "NOISE",
                                  "MOD DECAY", "OP2 RATIO", "OP3 RATIO", "OP4 RATIO", "PITCH DECAY", "VEL > FM" };
    if (k < 0 || k > 11) return "-";
    if (machine == M_WAVETABLE) {
        if (k == 1 || k == 2) return fxn[std::clamp(fxMode, 0, 4)][k - 1];
        return wt[k];
    }
    if (machine == M_FM) return fm[k];
    return "-";
}

inline void knobText(char *buf, int len, int machine, int k, float v)
{
    if (machine == M_WAVETABLE && k == 6) { snprintf(buf, len, "%d VOICES", wtUnison(v)); return; }
    if (machine == M_FM) {
        if (k >= 7 && k <= 9) { snprintf(buf, len, "x%.2f", (double) fmRatio(v)); return; }
        if (k == 6 && v >= 126.5f) { snprintf(buf, len, "HOLD"); return; }
        if (k == 6 || k == 10) {
            float s = fmDecaySeconds(v);
            if (s < 1.0f) snprintf(buf, len, "%d ms", (int) std::lround(s * 1000.0f));
            else snprintf(buf, len, "%.1f s", (double) s);
            return;
        }
        if (k == 4) { snprintf(buf, len, "+%d st", (int) std::lround(v / 127.0f * 48.0f)); return; }
    }
    snprintf(buf, len, "%d", (int) std::lround(v));
}

/* ------------------------------------------------------------------ shared modulation */
struct ModShared {
    float src[MS_COUNT] = {};        /* this block's global sources; AMP/FILTER ENV filled per slot */
    float pitchBendSemis = 0.0f;
    float bpm = 120.0f;
};

/* ------------------------------------------------------------------ WAVETABLE machine */
class WtMachine {
public:
    void setSampleRate(double sr) { osc.setSampleRate(sr); fmOsc.setSampleRate(sr); fmOsc.setBlockDC(false); }

    void start(const Wavetable *t, int umode)
    {
        osc.setWavetable(t);
        float ph[8];
        for (int i = 0; i < 8; i++) ph[i] = umode == UNI_PHASESYNC ? 0.0f : (float) rand() / (float) RAND_MAX;
        osc.noteOn(ph, 8);
        fmOsc.noteOn(0.0f);
        last = t;
    }

    /* note: the slot's pitch (MIDI note, float); K: the slot's 12 knobs; ta..: matrix TIMBRE offsets */
    void render(const Wavetable *t, const float *P, int slot, float note, const float *mo, float *L, float *R, int n)
    {
        if (!t || t->size() == 0) return;
        const bool swapped = last && t != last;
        last = t;
        osc.setWavetable(t);
        const float *K = P + slotParam(slot, TB_P_M1_K1);
        VoicedWTParams vp;
        vp.voices   = std::clamp(wtUnison(K[6]), 1, 4);
        vp.position = std::clamp(pot01(K[0]) + mo[MD_TA], 0.0f, 1.0f);
        vp.detune   = std::clamp(potDetune(K[3]) + mo[MD_TD], 0.0f, 1.0f);
        vp.spread   = pot01(K[4]);
        vp.gain = 1.0f; vp.pan = 0.0f;
        vp.mode = std::clamp((int) P[slotParam(slot, TB_P_M1_WT_UMODE)], 0, UNI_COUNT - 1);
        const int fx = std::clamp((int) P[slotParam(slot, TB_P_M1_WT_FX)], 0, 4);
        const float fx1 = std::clamp(pot01(K[1]) + mo[MD_TB], 0.0f, 1.0f);
        const float fx2 = std::clamp(pot01(K[2]) + mo[MD_TC], 0.0f, 1.0f);
        vp.fm = nullptr;
        switch (fx) {
        case 1: {      /* FM: the same table as modulator, FX 2 picks the ratio */
            static const float ratio[9] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
            vp.fmDepth = fx1 * fx1 * 2.0f;
            if (vp.fmDepth > 0.0f) {
                float dummy[kBlock];
                std::memset(fmBuf, 0, (size_t) n * sizeof(float));
                std::memset(dummy, 0, (size_t) n * sizeof(float));
                WTOscillator::Params wp;
                wp.position = vp.position; wp.leftGain = 1.0f; wp.rightGain = 0.0f;
                fmOsc.setWavetable(t);
                fmOsc.processAdding(note + 12.0f * std::log2(ratio[(int) (fx2 * 8.0f + 0.5f)]), wp, fmBuf, dummy, n);
                vp.fm = fmBuf;
            }
            break; }
        case 2: vp.sync = fx1 < 0.01f ? 1.0f : 1.0f + fx1 * fx1 * 7.0f; vp.width = fx2 < 0.01f ? 0.0f : fx2; break;
        case 3: vp.asym = fx1; vp.fold = fx2; break;
        case 4: vp.bend = fx1; vp.formant = fx2; break;
        default: break;
        }
        osc.processAdding(note, vp, L, R, n);
        if (swapped)                                      /* table swapped mid-note: fade the block in */
            for (int i = 0; i < n; i++) { float g = (float) i / (float) n; L[i] *= g; R[i] *= g; }
    }

private:
    VoicedWTOscillator osc { 4 };
    WTOscillator fmOsc;
    float fmBuf[kBlock] = {};
    const Wavetable *last = nullptr;
};

/* ------------------------------------------------------------------ FM machine */
/* 4 sine operators. Op 1 is always a carrier at ratio 1 (TUNE moves the slot); a modulator is
 * always a higher-numbered operator, so they are computed 4 -> 1. Op 4 has feedback. */
class FmMachine {
public:
    FmMachine()
    {
        for (int i = 0; i <= kSin; i++) sinTab[i] = std::sin(2.0 * M_PI * (double) i / kSin);
    }
    void setSampleRate(double sr) { sampleRate = (float) sr; }

    void start(float vel, uint32_t seed)
    {
        for (auto &p : ph) p = 0.0;
        fb1 = fb2 = 0.0f;
        modEnv = 1.0f; pitchEnv = 1.0f; noiseEnv = 1.0f;
        velocity = vel;
        rs = seed ? seed : 1;
    }

    void render(const float *P, int slot, float note, const float *mo, float *L, float *R, int n)
    {
        const float *K = P + slotParam(slot, TB_P_M1_K1);
        const int algo = std::clamp((int) P[slotParam(slot, TB_P_M1_FM_ALGO)], 0, 7);
        /* which operators each operator hears (bit i = op i+1), and which are carriers */
        static const uint8_t modBy[8][4] = {
            { 0x2, 0x4, 0x8, 0 },     /* 4>3>2>1   */
            { 0x2, 0xC, 0, 0 },       /* 3+4>2>1   */
            { 0x6, 0, 0x8, 0 },       /* 4>3>1<2   */
            { 0x2, 0, 0x8, 0 },       /* 4>3 2>1   */
            { 0x8, 0x8, 0x8, 0 },     /* 4>1+2+3   */
            { 0, 0x4, 0x8, 0 },       /* 4>3>2 +1  */
            { 0x2, 0, 0, 0 },         /* 2>1 +3 +4 */
            { 0, 0, 0, 0 } };         /* 1+2+3+4   */
        static const uint8_t carriers[8] = { 0x1, 0x1, 0x1, 0x5, 0x7, 0x3, 0xD, 0xF };
        const uint8_t car = carriers[algo];

        float lvl[4] = { 1.0f, 0, 0, 0 };
        for (int i = 1; i < 4; i++) lvl[i] = std::clamp(pot01(K[i - 1]) + (i == 1 ? mo[MD_TA] : i == 2 ? mo[MD_TB] : mo[MD_TC]),
                                                       0.0f, 1.0f);
        const float fb = std::clamp(pot01(K[3]) + mo[MD_TD], 0.0f, 1.0f);
        const float ratio[4] = { 1.0f, fmRatio(K[7]), fmRatio(K[8]), fmRatio(K[9]) };
        const float velAmt = pot01(K[11]);
        const float idxVel = 1.0f - velAmt + velAmt * velocity * 2.0f;           /* VEL > FM */
        const float noiseLvl = potSquared(K[5]) * 0.5f;

        /* per-block envelopes (exponential) */
        const float dt = (float) n / sampleRate;
        const bool hold = K[6] >= 126.5f;
        const float mdec = hold ? 1.0f : std::exp(-dt / fmDecaySeconds(K[6]));
        const float pdec = std::exp(-dt / fmDecaySeconds(K[10]));
        const float pitchSemis = pot01(K[4]) * 48.0f * pitchEnv * (0.5f + 0.5f * velocity);
        const float f0 = midiNoteToHz(note + pitchSemis);
        const float nyq = sampleRate * 0.45f;

        double inc[4];
        for (int i = 0; i < 4; i++) inc[i] = std::min(f0 * ratio[i], nyq) / sampleRate;
        float carSum = 0.0f;                                   /* carriers mix: op 1 at full, others at their level */
        for (int i = 0; i < 4; i++) if ((car >> i) & 1) carSum += i == 0 ? 1.0f : lvl[i];
        const float carNorm = 1.0f / std::max(1.0f, carSum);
        float amp[4];
        for (int i = 0; i < 4; i++)
            amp[i] = ((car >> i) & 1) ? (i == 0 ? 1.0f : lvl[i]) * carNorm
                                      : lvl[i] * lvl[i] * 2.5f * modEnv * idxVel;   /* index, in cycles */
        const float fbAmt = fb * fb * 0.6f * (((car >> 3) & 1) ? 1.0f : modEnv);
        const uint8_t *mb = modBy[algo];

        for (int s = 0; s < n; s++) {
            float out[4];
            for (int i = 3; i >= 0; i--) {
                float m = 0.0f;
                for (int j = i + 1; j < 4; j++)
                    if (mb[i] & (1 << j)) m += out[j];
                if (i == 3) m += (fb1 + fb2) * 0.5f * fbAmt;
                out[i] = sinLookup(ph[i] + m) * amp[i];
                ph[i] += inc[i];
                if (ph[i] >= 1.0) ph[i] -= 1.0;
            }
            fb2 = fb1; fb1 = out[3] / std::max(amp[3], 1e-6f);
            float y = 0.0f;
            for (int i = 0; i < 4; i++) if ((car >> i) & 1) y += out[i];
            if (noiseLvl > 0.0f) y += noiseLvl * noiseEnv * (nextRand() * 2.0f - 1.0f);
            L[s] += y; R[s] += y;
        }
        modEnv *= mdec; pitchEnv *= pdec; noiseEnv *= pdec * pdec;
    }

private:
    static constexpr int kSin = 4096;
    float sinLookup(double x) const
    {
        x -= std::floor(x);
        float f = (float) (x * kSin);
        int i = (int) f; float fr = f - (float) i;
        return sinTab[i] + (sinTab[i + 1] - sinTab[i]) * fr;
    }
    float nextRand() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float) (rs & 0xFFFFFF) / 16777216.0f; }

    float sinTab[kSin + 1];
    double ph[4] = {};
    float fb1 = 0, fb2 = 0, modEnv = 1, pitchEnv = 1, noiseEnv = 1, velocity = 0;
    float sampleRate = 44100.0f;
    uint32_t rs = 1;
};

/* ------------------------------------------------------------------ slot */
class Slot {
public:
    void setSampleRate(double sr)
    {
        wt.setSampleRate(sr); fm.setSampleRate(sr);
        filter.setSampleRate(sr); amp.setSampleRate(sr); fenv.setSampleRate(sr);
        noteSmoother.setSampleRate(sr);
        sampleRate = sr;
    }
    bool isActive() const { return active; }
    float ampOut() const { return amp.getOutput(); }

    void start(int idx, const float *P, const Wavetable *t, int note, float vel, float glideFrom, uint32_t seed)
    {
        slot = idx;
        machine = std::clamp((int) P[sp(TB_P_M1_MODEL)], 0, M_COUNT - 1);
        drum = machine == M_FM && (int) P[sp(TB_P_M1_FM_MODE)] == 1;
        midiNote = note; velocity = vel; active = true;
        const int gm = (int) P[TB_P_GLIDE_MODE];
        glissando = gm == GM_GLISSANDO;
        if (glideFrom >= 0.0f && (gm == GM_GLISSANDO || gm == GM_PORTAMENTO)) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValueUnsmoothed(glideFrom / 127.0f);
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        filter.reset(); lastFn = -1000.0f;
        if (machine == M_WAVETABLE) wt.start(t, (int) P[sp(TB_P_M1_WT_UMODE)]);
        if (machine == M_FM) fm.start(vel, seed);
        updateEnvs(P, 0);
        fenv.reset(); fenv.noteOn();
        amp.reset(); amp.noteOn();
    }

    /* a new note while held: no phase reset; LEGATO keeps the envelopes running */
    void retrigger(const float *P, int note, float vel, uint32_t seed)
    {
        const int gm = (int) P[TB_P_GLIDE_MODE];
        glissando = gm == GM_GLISSANDO;
        midiNote = note; velocity = vel;
        if (gm != GM_OFF) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        if (gm != GM_LEGATO) {
            fenv.noteOn(); amp.noteOn();
            if (machine == M_FM) fm.start(vel, seed);
        }
    }

    void stop()
    {
        if (drum) return;               /* FM DRUM: one shot, the note plays out its decay */
        amp.noteOff(); fenv.noteOff();
    }
    void kill() { active = false; amp.reset(); }

    void render(const float *P, const Wavetable *t, ModShared &ms, float *outL, float *outR, int n)
    {
        if (!active) return;
        updateEnvs(P, n);

        /* the matrix rows aimed at ALL or at this slot */
        float mo[MD_COUNT] = {};
        ms.src[MS_AMPENV] = amp.getOutput();
        ms.src[MS_FLTENV] = fenv.getOutput();
        ms.src[MS_VELOCITY] = velocity;
        ms.src[MS_KEY] = std::clamp(((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
        for (int m = 0; m < kMatrixRows; m++) {
            const int b = TB_P_MM1_SRC + 4 * m;
            const int src = (int) P[b], dst = (int) P[b + 1], tgt = (int) P[b + 3];
            if (src > MS_NONE && src < MS_COUNT && dst > MD_NONE && dst < MD_COUNT && (tgt == 0 || tgt == slot + 1))
                mo[dst] += ms.src[src] * potBipolar(P[b + 2]);
        }

        float note = noteSmoother.getCurrentValue() * 127.0f;
        if (glissando) note = (float) (int) (note + 0.5f);
        note += P[sp(TB_P_M1_TUNE)] + ms.pitchBendSemis + mo[MD_PITCH] * 24.0f;

        float L[kBlock] = {}, R[kBlock] = {};
        if (machine == M_WAVETABLE) wt.render(t, P, slot, note, mo, L, R, n);
        else if (machine == M_FM)   fm.render(P, slot, note, mo, L, R, n);
        /* WEST COAST / ANALOG: v2.1 / v2.2 */

        /* filter */
        const float filterWidth = 12.0f * std::log2(20000.0f / 440.0f) + 69.0f;
        float fn = potFilterNote(P[sp(TB_P_M1_CUTOFF)]);
        fn += ((float) midiNote - 60.0f) * pot01(P[sp(TB_P_M1_FKEY)]);
        fn += fenv.getOutput() * potBipolar(P[sp(TB_P_M1_FENV)]) * filterWidth;
        fn += mo[MD_CUTOFF] * filterWidth;
        const float fmax = std::min(20000.0f, (float) (sampleRate / 2.0));
        const float res = std::clamp(pot01(P[sp(TB_P_M1_RESO)]) + mo[MD_RESO], 0.0f, 1.0f);
        static const Filter::Type kTypes[8] = { Filter::lowpass, Filter::lowpass, Filter::highpass, Filter::highpass,
                                                Filter::bandpass, Filter::bandpass, Filter::notch, Filter::notch };
        const int ft = (int) P[sp(TB_P_M1_FTYPE)] & 7;
        if (lastFn < -999.0f) lastFn = fn;
        const bool open = ft <= 1 && midiNoteToHz(std::min(fn, lastFn)) >= 19000.0f && res < 0.01f;   /* LP fully open */
        if (!open) {
            if (wasOpen) filter.reset();                  /* it was skipped: start from rest, not stale state */
            filter.setType(kTypes[ft]);
            filter.setSlope((ft & 1) ? Filter::db24 : Filter::db12);
            /* the cutoff glides across the block in 4 steps: a fast envelope jumping it once per block
             * made a resonant filter spike */
            const int sub = 4, len = (n + sub - 1) / sub;
            for (int j = 0, o = 0; j < sub && o < n; j++, o += len) {
                const float fj = lastFn + (fn - lastFn) * (float) (j + 1) / (float) sub;
                filter.setParams(std::clamp(midiNoteToHz(fj), 4.0f, fmax), kFilterQ / (1.0f - res * 0.99f));
                filter.process(L + o, R + o, std::min(len, n - o));
            }
        }
        lastFn = fn; wasOpen = open;

        /* velocity, amp envelope, level, pan */
        const float g = velocityToGain(velocity, pot01(P[sp(TB_P_M1_VEL)])) *
                        std::clamp(potSquared(P[sp(TB_P_M1_LEVEL)]) + mo[MD_LEVEL], 0.0f, 1.0f);
        const float pan = std::clamp(potBipolar(P[sp(TB_P_M1_PAN)]) + mo[MD_PAN], -1.0f, 1.0f);
        amp.processMultiplying(L, R, n);
        const float gl = g * std::min(1.0f, 1.0f - pan), gr = g * std::min(1.0f, 1.0f + pan);
        for (int i = 0; i < n; i++) { outL[i] += L[i] * gl; outR[i] += R[i] * gr; }

        noteSmoother.process(n);
        const auto st = amp.getState();
        if (st == AnalogADSR::State::idle ||
            (drum && st == AnalogADSR::State::sustain && amp.getOutput() < 1e-4f))
            active = false;
    }

private:
    int sp(int s1Index) const { return slotParam(slot, s1Index); }

    void updateEnvs(const float *P, int n)
    {
        amp.setAttack(potAttackTime(P[sp(TB_P_M1_AMP_A)]));
        amp.setDecay(potEnvTime(P[sp(TB_P_M1_AMP_D)]));
        amp.setSustainLevel(drum ? 0.0f : pot01(P[sp(TB_P_M1_AMP_S)]));
        amp.setRelease(potEnvTime(P[sp(TB_P_M1_AMP_R)]));
        const float fd = potEnvTime(P[sp(TB_P_M1_FDEC)]);
        fenv.setAttack(0.002f); fenv.setDecay(fd); fenv.setSustainLevel(0.0f); fenv.setRelease(fd);
        if (n > 0) fenv.process(n);
    }

    WtMachine wt;
    FmMachine fm;
    Filter filter;
    AnalogADSR amp, fenv;
    ValueSmoother<float> noteSmoother;
    double sampleRate = 44100.0;
    int slot = 0, machine = 0, midiNote = 60;
    float velocity = 0.0f, lastFn = -1000.0f;
    bool active = false, glissando = false, drum = false, wasOpen = false;
};

} // namespace tb
