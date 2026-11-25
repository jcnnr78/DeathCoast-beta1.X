/* NorthWest -- the four machines and the models they run.
 *
 *   Engine: notes (per machine: its own MIDI channel or the plugin's track), shared modulation, effects, master
 *     └─ Slot x4: model (WAVETABLE | FM | WEST COAST | ANALOG) -> filter (filters.h) -> amp env -> level/pan/sends
 *
 * A machine is monophonic; the model is read when a note starts. Each machine owns 14 knobs K1..K14 whose meaning
 * depends on the model (knobName / knobText below are what MPC shows). Work is done in control blocks of kBlock
 * samples: modulation, envelopes and the filter's cutoff target are worked out once a block, so 32 samples
 * (0.7 ms) is how finely they move. FM, WEST COAST and ANALOG can run at twice the rate (OVERSAMPLING).
 */
#pragma once

#include "../ported/wt_oscillator.h"
#include "../ported/analog_tables.h"
#include "filters.h"
#include "oversample.h"
#include "../ported/adsr.h"
#include "lfo.h"
#include "param_map.h"
#include "params.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace nw {

inline constexpr int kBlock = 32;
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

/* a slot's parameter: P[slotParam(n, NW_P_M1_CUTOFF)] */
inline int slotParam(int slot, int s1Index) { return s1Index + slot * NW_SLOT_STRIDE; }

/* ------------------------------------------------------------------ knob meanings */
/* WAVETABLE: K1 POSITION, K2 FX 1, K3 FX 2, K4 DETUNE, K7 UNISON (1..4), K8 SPREAD (64 = half),
 *            SUB (the right-hand 2x2 block, one Q-Link column): K5 SUB LEVEL, K6 SUB DRIVE,
 *            K11 SUB OCTAVE (-2, -1, 0), K12 SUB WAVE
 * FM:        K1-K3 OP2-OP4 LEVEL, K4 FEEDBACK, K5 PITCH ENV, K6 NOISE, K7 MOD DECAY,
 *            K8-K10 OP2-OP4 RATIO, K11 PITCH DECAY, K12 VEL > FM
 * WEST COAST: K1 FM AMOUNT, K2 FOLD, K3 SYMMETRY, K4 OSC MIX, K7 MOD RATIO (43 = x1), K8 MOD FINE,
 *            LPG (the right-hand block): K5 LPG DEPTH, K6 LPG RESPONSE, K11 LPG MODE, K12 VEL > FOLD
 * ANALOG:    K1 FM 1>2, K2 NOISE, K3 DRIVE, K4 PULSE WIDTH, K7 OSC 1 LEVEL, K8 OSC 1 FREQ, K9 OSC 2 LEVEL,
 *            K10 OSC 2 FREQ, PITCH: K5 PITCH ENV, K6 PITCH DECAY, K11 P.ENV TARGET, K12 OSC 2 FINE,
 *            K13 VEL > P.ENV, K14 OSC 1 FINE
 * Matrix TIMBRE A..D = K1..K4. */
inline int wtUnison(float v) { return 1 + (int) std::lround(v / 127.0f * 3.0f); }
/* sub: OCTAVE in 3 steps (default 43 = one octave down), WAVE in 6 (default 64 = SINE) */
inline int wtSubOct(float v) { return v < 43.0f ? -2 : v < 86.0f ? -1 : 0; }
inline int wtSubWave(float v) { return std::clamp((int) (v * 6.0f / 128.0f), 0, 5); }
inline const char *wtSubWaveName(int i)
{
    static const char *n[6] = { "SQUARE", "PULSE 25", "PULSE 12", "SINE", "TRIANGLE", "SAW" };
    return n[std::clamp(i, 0, 5)];
}

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

/* WEST COAST: LPG RESPONSE = the vactrol's fall, 20 ms .. 2 s */
inline float wcResponseSeconds(float v) { return 0.02f * std::pow(100.0f, v / 127.0f); }
/* MOD FINE: +-50 cents, 64 = 0 */
inline float wcFineSemis(float v) { return (v - 64.0f) / 63.0f * 0.5f; }
/* MOD RATIO: the knob's default (43) is x1 */
inline float wcRatio(float v)
{
    static const float r[27] = { 0.25f, 0.333f, 0.5f, 0.75f, 1.0f, 1.41f, 1.5f, 2.0f, 2.5f, 2.76f, 3.0f, 3.5f, 4.0f,
                                 4.5f, 5.0f, 5.4f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
    const int i = v < 43.0f ? (int) (v / 43.0f * 4.0f) : 4 + (int) ((v - 43.0f) / 84.01f * 22.99f);
    return r[std::clamp(i, 0, 26)];
}
/* LPG MODE: VCA (gain only), COMBO (the default: filter and gain), LP (filter only) */
enum { LPG_VCA = 0, LPG_COMBO, LPG_LP };
inline int wcLpgMode(float v) { return std::clamp((int) (v * 3.0f / 128.0f), 0, 2); }

/* ANALOG */
inline float anPitchEnvSemis(float v) { const float x = v / 127.0f; return x * x * 48.0f; }
inline float anPulseWidth(float v) { return 0.5f - 0.45f * v / 127.0f; }
inline float anTuneSemis(float v) { return std::round((v - 64.0f) / 63.0f * 24.0f); }
inline int anTarget(float v) { return std::clamp((int) (v * 3.0f / 128.0f), 0, 2); }   /* 0 OSC 1, 1 BOTH, 2 OSC 2 */

inline const char *knobName(int machine, int k, int fxMode)
{
    static const char *wt[14] = { "POSITION", "FX 1", "FX 2", "DETUNE", "SUB LEVEL", "SUB DRIVE", "UNISON", "SPREAD",
                                  "-", "-", "SUB OCTAVE", "SUB WAVE", "-", "-" };
    static const char *fxn[5][2] = { { "FX 1", "FX 2" }, { "FM AMOUNT", "FM RATIO" }, { "SYNC", "PULSE WIDTH" },
                                     { "ASYMMETRY", "FOLD" }, { "BEND", "FORMANT" } };
    static const char *fm[14] = { "OP2 LEVEL", "OP3 LEVEL", "OP4 LEVEL", "FEEDBACK", "PITCH ENV", "NOISE",
                                  "MOD DECAY", "OP2 RATIO", "OP3 RATIO", "OP4 RATIO", "PITCH DECAY", "VEL > FM", "-", "OP1 RATIO" };
    static const char *wc[14] = { "FM AMOUNT", "FOLD", "SYMMETRY", "OSC MIX", "LPG DEPTH", "LPG RESPONSE", "MOD RATIO",
                                  "MOD FINE", "-", "-", "LPG MODE", "VEL > FOLD", "-", "-" };
    static const char *an[14] = { "FM 1>2", "NOISE", "DRIVE", "PULSE WIDTH", "PITCH ENV", "PITCH DECAY", "OSC 1 LEVEL",
                                  "OSC 1 FREQ", "OSC 2 LEVEL", "OSC 2 FREQ", "P.ENV TARGET", "OSC 2 FINE", "VEL > P.ENV",
                                  "OSC 1 FINE" };
    if (k < 0 || k >= 14) return "-";
    switch (machine) {
    case M_WAVETABLE: return (k == 1 || k == 2) ? fxn[std::clamp(fxMode, 0, 4)][k - 1] : wt[k];
    case M_FM: return fm[k];
    case M_WESTCOAST: return wc[k];
    case M_ANALOG: return an[k];
    default: return "-";
    }
}

static inline void secondsText(char *buf, int len, float s)
{
    if (s < 1.0f) snprintf(buf, len, "%d ms", (int) std::lround(s * 1000.0f));
    else snprintf(buf, len, "%.1f s", (double) s);
}

inline void knobText(char *buf, int len, int machine, int k, float v)
{
    switch (machine) {
    case M_WAVETABLE:
        if (k == 6) { snprintf(buf, len, "%d VOICES", wtUnison(v)); return; }
        if (k == 10) { snprintf(buf, len, "%d OCT", wtSubOct(v)); return; }
        if (k == 11) { snprintf(buf, len, "%s", wtSubWaveName(wtSubWave(v))); return; }
        break;
    case M_FM:
        if ((k >= 7 && k <= 9) || k == 13) { snprintf(buf, len, "x%.2f", (double) fmRatio(v)); return; }
        if (k == 6 && v >= 126.5f) { snprintf(buf, len, "HOLD"); return; }
        if (k == 6 || k == 10) { secondsText(buf, len, fmDecaySeconds(v)); return; }
        if (k == 4) { snprintf(buf, len, "+%d st", (int) std::lround(v / 127.0f * 48.0f)); return; }
        break;
    case M_WESTCOAST:
        if (k == 6) { snprintf(buf, len, "x%.2f", (double) wcRatio(v)); return; }
        if (k == 7) { snprintf(buf, len, "%+d ct", (int) std::lround(wcFineSemis(v) * 100.0f)); return; }
        if (k == 5) { secondsText(buf, len, wcResponseSeconds(v)); return; }
        if (k == 10) { static const char *t[3] = { "VCA", "COMBO", "LP" }; snprintf(buf, len, "%s", t[wcLpgMode(v)]); return; }
        break;
    case M_ANALOG:
        if (k == 4) { snprintf(buf, len, "+%.1f st", (double) anPitchEnvSemis(v)); return; }
        if (k == 5) { secondsText(buf, len, fmDecaySeconds(v)); return; }
        if (k == 3) { snprintf(buf, len, "%d %%", (int) std::lround(anPulseWidth(v) * 100.0f)); return; }
        if (k == 7 || k == 9) { snprintf(buf, len, "%+d st", (int) anTuneSemis(v)); return; }
        if (k == 11 || k == 13) { snprintf(buf, len, "%+d ct", (int) std::lround(wcFineSemis(v) * 100.0f)); return; }
        if (k == 10) { static const char *t[3] = { "OSC 1", "BOTH", "OSC 2" }; snprintf(buf, len, "%s", t[anTarget(v)]); return; }
        break;
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
    explicit WtMachine(AnalogTables &a) : sub(a) {}
    void setSampleRate(double sr) { osc.setSampleRate(sr); fmOsc.setSampleRate(sr); fmOsc.setBlockDC(false); sub.setSampleRate(sr); }

    void start(const Wavetable *t, int umode)
    {
        osc.setWavetable(t);
        float ph[8];
        for (int i = 0; i < 8; i++) ph[i] = umode == UNI_PHASESYNC ? 0.0f : (float) rand() / (float) RAND_MAX;
        osc.noteOn(ph, 8);
        fmOsc.noteOn(0.0f);
        sub.noteOn(0.0f);
        last = t;
    }

    /* note: the slot's pitch (MIDI note, float); K: the slot's 12 knobs; ta..: matrix TIMBRE offsets */
    void render(const Wavetable *t, const float *P, int slot, float note, const float *mo, float *L, float *R, int n)
    {
        if (!t || t->size() == 0) return;
        const bool swapped = last && t != last;
        last = t;
        osc.setWavetable(t);
        const float *K = P + slotParam(slot, NW_P_M1_K1);
        VoicedWTParams vp;
        vp.voices   = std::clamp(wtUnison(K[6]), 1, 4);
        vp.position = std::clamp(pot01(K[0]) + mo[MD_TA], 0.0f, 1.0f);
        vp.detune   = std::clamp(potDetune(K[3]) + mo[MD_TD], 0.0f, 1.0f);
        vp.spread   = pot01(K[7]);
        vp.gain = 1.0f; vp.pan = 0.0f;
        vp.mode = std::clamp((int) P[slotParam(slot, NW_P_M1_WT_UMODE)], 0, UNI_COUNT - 1);
        const int fx = std::clamp((int) P[slotParam(slot, NW_P_M1_WT_FX)], 0, 4);
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
        /* sub oscillator: K5 level, K6 drive, K11 octave, K12 wave (centred, through the filter like the table) */
        const float subLvl = potSquared(K[4]);
        if (subLvl > 0.0f) {
            StereoOscillator::Params sp;
            switch (wtSubWave(K[11])) {
            case 0: sp.wave = Wave::pulse; sp.pw = 0.5f; break;
            case 1: sp.wave = Wave::pulse; sp.pw = 0.25f; break;
            case 2: sp.wave = Wave::pulse; sp.pw = 0.125f; break;
            case 3: sp.wave = Wave::sine; break;
            case 4: sp.wave = Wave::triangle; break;
            default: sp.wave = Wave::sawUp; break;
            }
            const float drive = pot01(K[5]);
            const float subNote = note + 12.0f * (float) wtSubOct(K[10]);
            if (drive <= 0.0f) {
                sp.leftGain = sp.rightGain = subLvl;
                sub.processAdding(subNote, sp, L, R, n);
            } else {                                      /* DRIVE: the sub alone through a soft clipper */
                float sb[kBlock] = {}, dummy[kBlock] = {};
                sp.leftGain = 1.0f; sp.rightGain = 0.0f;
                sub.processAdding(subNote, sp, sb, dummy, n);
                const float g = 1.0f + drive * 9.0f, norm = subLvl / std::tanh(g);
                for (int i = 0; i < n; i++) { const float y = std::tanh(sb[i] * g) * norm; L[i] += y; R[i] += y; }
            }
        }
        if (swapped)                                      /* table swapped mid-note: fade the block in */
            for (int i = 0; i < n; i++) { float g = (float) i / (float) n; L[i] *= g; R[i] *= g; }
    }

private:
    VoicedWTOscillator osc { 4 };
    WTOscillator fmOsc;
    StereoOscillator sub;
    float fmBuf[kBlock] = {};
    const Wavetable *last = nullptr;
};

/* ------------------------------------------------------------------ FM machine */
/* 4 sine operators. Op 1 is always a carrier (OP1 LEVEL: mN_op1, OP1 RATIO: K14); a modulator is
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

    /* mono, n * os samples into M (os = 1, or 2 when oversampling) */
    void render(const float *P, int slot, float note, const float *mo, float *M, int n, int os)
    {
        const float *K = P + slotParam(slot, NW_P_M1_K1);
        const int algo = std::clamp((int) P[slotParam(slot, NW_P_M1_FM_ALGO)], 0, 7);
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

        float lvl[4] = { pot01(P[slotParam(slot, NW_P_M1_OP1)]), 0, 0, 0 };
        for (int i = 1; i < 4; i++) lvl[i] = std::clamp(pot01(K[i - 1]) + (i == 1 ? mo[MD_TA] : i == 2 ? mo[MD_TB] : mo[MD_TC]),
                                                       0.0f, 1.0f);
        const float fb = std::clamp(pot01(K[3]) + mo[MD_TD], 0.0f, 1.0f);
        const float ratio[4] = { fmRatio(K[13]), fmRatio(K[7]), fmRatio(K[8]), fmRatio(K[9]) };
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
        for (int i = 0; i < 4; i++) inc[i] = std::min(f0 * ratio[i], nyq) / (sampleRate * (float) os);
        float carSum = 0.0f;                                   /* carriers mix: each at its level */
        for (int i = 0; i < 4; i++) if ((car >> i) & 1) carSum += lvl[i];
        const float carNorm = 1.0f / std::max(1.0f, carSum);
        float amp[4];
        for (int i = 0; i < 4; i++)
            amp[i] = ((car >> i) & 1) ? lvl[i] * carNorm
                                      : lvl[i] * lvl[i] * 2.5f * modEnv * idxVel;   /* index, in cycles */
        const float fbAmt = fb * fb * 0.6f * (((car >> 3) & 1) ? 1.0f : modEnv);
        const uint8_t *mb = modBy[algo];

        for (int s = 0; s < n * os; s++) {
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
            M[s] = y;
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

/* ------------------------------------------------------------------ the analog oscillator pair */
/* Shared by WEST COAST (and ANALOG, 1.5): osc 1 is the modulator / sync master, osc 2 the carrier.
 * Band-limited waves from AnalogTables, read at a phase this class keeps, so FM (as phase modulation)
 * and hard sync work per sample. */
struct OscPair {
    float ph1 = 0, ph2 = 0;
    void reset() { ph1 = ph2 = 0; }
    static float wave(const AnalogTables &t, int w, float note, float ph)
    {
        switch (w) {
        case 0: return t.processSine(note, ph);
        case 1: return t.processTriangle(note, ph);
        case 2: return t.processSawUp(note, ph);
        default: return t.processPulse(note, ph, 0.5f);
        }
    }
    static float wave(const AnalogTables &t, int w, float note, float ph, float pw)
    {
        return w == 3 ? t.processPulse(note, ph, pw) : wave(t, w, note, ph);
    }
};

/* ------------------------------------------------------------------ WEST COAST machine */
/* carrier (osc 2) phase-modulated by osc 1 -> sine wavefolder (3 folds deep at the top of FOLD, SYMMETRY
 * adds an offset before folding) -> low pass gate: a 2-pole low pass and a VCA both driven by the amp
 * envelope through a vactrol (fast rise, slow fall set by LPG RESPONSE); LPG MODE keeps only the VCA or only
 * the filter. The slot does not apply its amp envelope again for this model: the gate is the VCA. */
class WcMachine {
public:
    explicit WcMachine(AnalogTables &a) : tables(a)
    {
        for (int i = 0; i <= kSin; i++) sinTab[i] = std::sin(2.0 * M_PI * (double) i / kSin);
    }
    void setSampleRate(double sr) { sampleRate = (float) sr; }
    void start(float vel) { osc.reset(); lp1 = lp2 = 0.0f; velocity = vel; }
    float gate() const { return vac; }

    /* mono, n * os samples into M; env: the amp envelope, n samples at the base rate */
    void render(const float *P, int slot, float note, const float *mo, const float *env, float *M, int n, int os)
    {
        const float *K = P + slotParam(slot, NW_P_M1_K1);
        const int w1 = (int) P[slotParam(slot, NW_P_M1_OSC_W1)], w2 = (int) P[slotParam(slot, NW_P_M1_OSC_W2)];
        const bool sync = (int) P[slotParam(slot, NW_P_M1_OSC_SYNC)] != 0;
        const float fm = std::clamp(pot01(K[0]) + mo[MD_TA], 0.0f, 1.0f);
        const float velAmt = pot01(K[11]);
        const float fold = std::clamp(pot01(K[1]) + mo[MD_TB], 0.0f, 1.0f) * (1.0f - velAmt + velAmt * velocity);
        const float sym = std::clamp(pot01(K[2]) + mo[MD_TC], 0.0f, 1.0f);
        const float mix = std::clamp(pot01(K[3]) + mo[MD_TD], 0.0f, 1.0f);
        const float depth = pot01(K[4]);
        const int mode = wcLpgMode(K[10]);
        const float srOs = sampleRate * (float) os;
        const float fall = std::exp(-1.0f / (wcResponseSeconds(K[5]) * srOs));
        const float rise = std::exp(-1.0f / (0.003f * srOs));

        const float note1 = note + 12.0f * std::log2(wcRatio(K[6])) + wcFineSemis(K[7]);
        const float f1 = std::min(midiNoteToHz(note1), sampleRate * 0.45f) / srOs;
        const float f2 = std::min(midiNoteToHz(note), sampleRate * 0.45f) / srOs;
        const float pm = fm * fm * 2.0f;                         /* modulation depth, cycles */
        const float gainIn = 1.0f + fold * 7.0f;                /* up to ~3 folds */
        const float off = sym * 0.8f;

        for (int i = 0; i < n * os; i++) {
            const float m = OscPair::wave(tables, w1, note1, osc.ph1);
            float c2 = osc.ph2 + pm * m;
            c2 -= std::floor(c2);
            const float c = OscPair::wave(tables, w2, note, c2);
            osc.ph1 += f1;
            if (osc.ph1 >= 1.0f) { osc.ph1 -= 1.0f; if (sync) osc.ph2 = osc.ph1 * f2 / f1; }
            osc.ph2 += f2; if (osc.ph2 >= 1.0f) osc.ph2 -= 1.0f;

            float x = (c * (1.0f - mix) + m * mix) * gainIn + off;
            x = fold > 0.0f || off > 0.0f ? sinLookup(x * 0.25f) : x;   /* sin(pi/2 * x): folds past +-1 */

            /* vactrol: follows the amp envelope up fast, falls slowly */
            const float e = env[i / os];
            vac = e > vac ? e + (vac - e) * rise : e + (vac - e) * fall;
            /* low pass gate: cutoff 40 Hz .. 18 kHz with the vactrol (LPG DEPTH 0 = always open; VCA mode: open);
             * the coefficient is worked out every 8 samples (pow + exp are dear on the MPC) */
            if ((i & 7) == 0) {
                const float open = mode == LPG_VCA ? 1.0f : 1.0f - depth + depth * vac;
                const float fc = 40.0f * std::pow(450.0f, open);
                g = 1.0f - std::exp(-2.0f * (float) M_PI * std::min(fc, sampleRate * 0.45f) / srOs);
            }
            lp1 += g * (x - lp1);
            lp2 += g * (lp1 - lp2);
            /* LP mode: the filter alone shapes the level (a closed gate is near silent anyway at depth) */
            M[i] = mode == LPG_LP ? lp2 * 1.5f * std::min(1.0f, vac * 8.0f) : lp2 * vac * 1.5f;
        }
    }

private:
    static constexpr int kSin = 4096;
    float sinLookup(float x) const              /* sin(2 pi x) */
    {
        x -= std::floor(x);
        const float f = x * kSin; const int i = (int) f; const float fr = f - (float) i;
        return sinTab[i] + (sinTab[i + 1] - sinTab[i]) * fr;
    }
    AnalogTables &tables;
    OscPair osc;
    float sinTab[kSin + 1];
    float lp1 = 0, lp2 = 0, vac = 0, velocity = 0, sampleRate = 44100.0f, g = 1.0f;
};

/* ------------------------------------------------------------------ ANALOG machine */
/* DFAM-like: two band-limited oscillators (sine/tri/saw/pulse with PULSE WIDTH), osc 1 phase-modulates osc 2
 * (FM 1>2), hard sync of osc 2 to osc 1, white noise, a decay-only pitch envelope (amount, decay, velocity, which
 * oscillator) and a DRIVE into the machine's filter (LP 4 is the ladder). The amp envelope is the slot's. */
class AnMachine {
public:
    explicit AnMachine(AnalogTables &a) : tables(a) {}
    void setSampleRate(double sr) { sampleRate = (float) sr; }
    void start(float vel, uint32_t seed) { osc.reset(); trigger(vel); rng = seed | 1u; }
    void trigger(float vel) { penv = 1.0f; velocity = vel; }

    /* mono, n * os samples into M */
    void render(const float *P, int slot, float note, const float *mo, float *M, int n, int os)
    {
        const float *K = P + slotParam(slot, NW_P_M1_K1);
        const int w1 = (int) P[slotParam(slot, NW_P_M1_OSC_W1)], w2 = (int) P[slotParam(slot, NW_P_M1_OSC_W2)];
        const bool sync = (int) P[slotParam(slot, NW_P_M1_OSC_SYNC)] != 0;
        const float fm = std::clamp(pot01(K[0]) + mo[MD_TA], 0.0f, 1.0f);
        const float noise = std::clamp(pot01(K[1]) + mo[MD_TB], 0.0f, 1.0f);
        const float drive = std::clamp(pot01(K[2]) + mo[MD_TC], 0.0f, 1.0f);
        const float pwk = std::clamp(K[3] + mo[MD_TD] * 127.0f, 0.0f, 127.0f);
        const float lvl1 = pot01(K[6]), lvl2 = pot01(K[8]);
        const float velAmt = pot01(K[12]);                                       /* VEL > P.ENV */
        const float penvSemis = anPitchEnvSemis(K[4]) * (1.0f - velAmt + velAmt * velocity);
        const float srOs = sampleRate * (float) os;
        const float pdec = std::exp(-1.0f / (fmDecaySeconds(K[5]) * srOs));
        const float pw = anPulseWidth(pwk);
        const float off2 = anTuneSemis(K[9]) + wcFineSemis(K[11]);
        const float n1base = note + anTuneSemis(K[7]) + wcFineSemis(K[13]);
        const int target = anTarget(K[10]);
        const float t1 = target <= 1 ? 1.0f : 0.0f, t2 = target >= 1 ? 1.0f : 0.0f;
        const float pm = fm * fm * 2.0f;
        const float g = 1.0f + drive * 6.0f, norm = 1.0f / fastTanh(g);
        const float nl = noise * noise;
        const float sum = 1.0f / std::max(1.0f, lvl1 + lvl2 + nl);   /* levels are absolute until they add past 1 */

        float note1 = n1base, note2 = note + off2, f1 = 0, f2 = 0;
        for (int i = 0; i < n * os; i++) {
            if ((i & 15) == 0) {          /* pitch (pow) every 16 samples: the envelope is smooth enough */
                const float pe = penvSemis * penv;
                note1 = n1base + pe * t1; note2 = note + off2 + pe * t2;
                f1 = std::min(midiNoteToHz(note1), sampleRate * 0.45f) / srOs;
                f2 = std::min(midiNoteToHz(note2), sampleRate * 0.45f) / srOs;
            }
            penv *= pdec;
            const float a = OscPair::wave(tables, w1, note1, osc.ph1, pw);
            float c2 = osc.ph2 + pm * a;
            c2 -= std::floor(c2);
            const float b = OscPair::wave(tables, w2, note2, c2, pw);
            osc.ph1 += f1;
            if (osc.ph1 >= 1.0f) { osc.ph1 -= 1.0f; if (sync) osc.ph2 = osc.ph1 * f2 / f1; }
            osc.ph2 += f2; if (osc.ph2 >= 1.0f) osc.ph2 -= 1.0f;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float wn = (float) (int32_t) rng * (1.0f / 2147483648.0f);
            float x = (a * lvl1 + b * lvl2 + wn * nl) * sum;
            if (drive > 0.0f) x = fastTanh(x * g) * norm;
            M[i] = x * 0.8f;
        }
        if (penv < 1e-6f) penv = 0.0f;
    }

private:
    AnalogTables &tables;
    OscPair osc;
    float penv = 0, velocity = 0, sampleRate = 44100.0f;
    uint32_t rng = 1;
};

/* ------------------------------------------------------------------ slot */
class Slot {
public:
    explicit Slot(AnalogTables &a) : wt(a), wc(a), an(a) {}
    void setSampleRate(double sr)
    {
        wt.setSampleRate(sr); fm.setSampleRate(sr); wc.setSampleRate(sr); an.setSampleRate(sr);
        filter.setSampleRate((float) sr); amp.setSampleRate(sr); fenv.setSampleRate(sr);
        noteSmoother.setSampleRate(sr);
        sampleRate = sr;
    }
    void setSeed(uint32_t s) { drs = s | 1u; }
    bool isActive() const { return active; }
    float ampOut() const { return amp.getOutput(); }

    void start(int idx, const float *P, const Wavetable *t, int note, float vel, float glideFrom, uint32_t seed)
    {
        slot = idx;
        machine = std::clamp((int) P[sp(NW_P_M1_MODEL)], 0, M_COUNT - 1);
        drum = machine == M_FM && (int) P[sp(NW_P_M1_FM_MODE)] == 1;
        midiNote = note; velocity = vel; active = true;
        const int gm = (int) P[NW_P_GLIDE_MODE];
        glissando = gm == GM_GLISSANDO;
        if (glideFrom >= 0.0f && (gm == GM_GLISSANDO || gm == GM_PORTAMENTO)) {
            noteSmoother.setTime(potGlideTime(P[NW_P_GLIDE]));
            noteSmoother.setValueUnsmoothed(glideFrom / 127.0f);
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        filter.reset(); lastFn = -1000.0f; wasOpen = false;
        dec.reset();
        if (machine == M_WAVETABLE) wt.start(t, (int) P[sp(NW_P_M1_WT_UMODE)]);
        if (machine == M_FM) fm.start(vel, seed);
        if (machine == M_WESTCOAST) wc.start(vel);
        if (machine == M_ANALOG) an.start(vel, seed);
        updateEnvs(P, 0);
        fenv.reset(); fenv.noteOn();
        amp.reset(); amp.noteOn();
    }

    /* a new note while held: no phase reset; LEGATO keeps the envelopes running */
    void retrigger(const float *P, int note, float vel, uint32_t seed)
    {
        const int gm = (int) P[NW_P_GLIDE_MODE];
        glissando = gm == GM_GLISSANDO;
        midiNote = note; velocity = vel;
        if (gm != GM_OFF) {
            noteSmoother.setTime(potGlideTime(P[NW_P_GLIDE]));
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        if (gm != GM_LEGATO) {
            fenv.noteOn(); amp.noteOn();
            if (machine == M_FM) fm.start(vel, seed);
            if (machine == M_ANALOG) an.trigger(vel);
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
        updateDrift(P);

        /* the matrix rows aimed at ALL or at this slot */
        float mo[MD_COUNT] = {};
        ms.src[MS_AMPENV] = amp.getOutput();
        ms.src[MS_FLTENV] = fenv.getOutput();
        ms.src[MS_VELOCITY] = velocity;
        ms.src[MS_KEY] = std::clamp(((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
        for (int m = 0; m < kMatrixRows; m++) {
            const int b = NW_P_MM1_SRC + 4 * m;
            const int src = (int) P[b], dst = (int) P[b + 1], tgt = (int) P[b + 3];
            if (src > MS_NONE && src < MS_COUNT && dst > MD_NONE && dst < MD_COUNT && (tgt == 0 || tgt == slot + 1))
                mo[dst] += ms.src[src] * potBipolar(P[b + 2]);
        }

        float note = noteSmoother.getCurrentValue() * 127.0f;
        if (glissando) note = (float) (int) (note + 0.5f);
        note += P[sp(NW_P_M1_TUNE)] + ms.pitchBendSemis + mo[MD_PITCH] * 24.0f + driftPitch;

        float L[kBlock] = {}, R[kBlock] = {};
        if (machine == M_WAVETABLE) {
            wt.render(t, P, slot, note, mo, L, R, n);
        } else {
            /* FM, WEST COAST, ANALOG: mono, at twice the rate with OVERSAMPLING on */
            const int os = (int) P[NW_P_OVERSAMPLE] != 0 && needsOversampling(P) ? 2 : 1;
            if (os != lastOs) { dec.reset(); lastOs = os; }
            float M[2 * kBlock];
            if (machine == M_FM) fm.render(P, slot, note, mo, M, n, os);
            else if (machine == M_WESTCOAST) {
                /* the low pass gate is this model's VCA: it gets the amp envelope sample by sample */
                float env[kBlock], dummy[kBlock];
                for (int i = 0; i < n; i++) env[i] = dummy[i] = 1.0f;
                amp.processMultiplying(env, dummy, n);
                wc.render(P, slot, note, mo, env, M, n, os);
            } else {
                an.render(P, slot, note, mo, M, n, os);
            }
            if (os == 2) dec.process(M, L, n);
            else std::memcpy(L, M, (size_t) n * sizeof(float));
            std::memcpy(R, L, (size_t) n * sizeof(float));
        }

        /* filter: the cutoff in MIDI-note units, gliding from the last block's to this one's */
        const float filterWidth = 12.0f * std::log2(20000.0f / 440.0f) + 69.0f;
        float fn = potFilterNote(P[sp(NW_P_M1_CUTOFF)]);
        fn += ((float) midiNote - 60.0f) * pot01(P[sp(NW_P_M1_FKEY)]);
        fn += fenv.getOutput() * potBipolar(P[sp(NW_P_M1_FENV)]) * filterWidth;
        fn += mo[MD_CUTOFF] * filterWidth + driftCutoff;
        const float res = std::clamp(pot01(P[sp(NW_P_M1_RESO)]) + mo[MD_RESO], 0.0f, 1.0f);
        const int ft = (int) P[sp(NW_P_M1_FTYPE)];
        const float shape = P[sp(NW_P_M1_FSHAPE)], bwlo = P[sp(NW_P_M1_BWLO)], bwhi = P[sp(NW_P_M1_BWHI)];
        if (lastFn < -999.0f) lastFn = fn;
        const bool open = MachineFilter::transparent(ft, std::min(fn, lastFn), res, shape, bwlo, bwhi);
        if (!open) {
            if (wasOpen) filter.reset();                  /* it was skipped: start from rest, not stale state */
            filter.process(ft, lastFn, fn, res, shape, bwlo, bwhi, L, R, n);
        }
        lastFn = fn; wasOpen = open;

        /* velocity, amp envelope, level, pan */
        const float g = velocityToGain(velocity, pot01(P[sp(NW_P_M1_VEL)])) *
                        std::clamp(potSquared(P[sp(NW_P_M1_LEVEL)]) + mo[MD_LEVEL], 0.0f, 1.0f);
        const float pan = std::clamp(potBipolar(P[sp(NW_P_M1_PAN)]) + mo[MD_PAN], -1.0f, 1.0f);
        if (machine != M_WESTCOAST) amp.processMultiplying(L, R, n);
        const float gl = g * std::min(1.0f, 1.0f - pan), gr = g * std::min(1.0f, 1.0f + pan);
        for (int i = 0; i < n; i++) { outL[i] += L[i] * gl; outR[i] += R[i] * gr; }

        noteSmoother.process(n);
        const auto st = amp.getState();
        if ((st == AnalogADSR::State::idle && (machine != M_WESTCOAST || wc.gate() < 1e-4f)) ||
            (drum && st == AnalogADSR::State::sustain && amp.getOutput() < 1e-4f))
            active = false;
    }

private:
    int sp(int s1Index) const { return slotParam(slot, s1Index); }

    void updateEnvs(const float *P, int n)
    {
        amp.setAttack(potAttackTime(P[sp(NW_P_M1_AMP_A)]));
        amp.setDecay(potEnvTime(P[sp(NW_P_M1_AMP_D)]));
        amp.setSustainLevel(drum ? 0.0f : pot01(P[sp(NW_P_M1_AMP_S)]));
        amp.setRelease(potEnvTime(P[sp(NW_P_M1_AMP_R)]));
        const float fd = potEnvTime(P[sp(NW_P_M1_FDEC)]);
        fenv.setAttack(0.002f); fenv.setDecay(fd); fenv.setSustainLevel(0.0f); fenv.setRelease(fd);
        if (n > 0) fenv.process(n);
    }

    /* where the 2x rate earns its cost: the fold, sync and phase modulation, drive; FM only at a high index or with
     * much feedback (a soft FM bell gains nothing and would cost twice) */
    bool needsOversampling(const float *P) const
    {
        const float *K = P + sp(NW_P_M1_K1);
        switch (machine) {
        case M_WESTCOAST: return true;
        case M_ANALOG: return (int) P[sp(NW_P_M1_OSC_SYNC)] != 0 || K[0] > 0.5f || K[2] > 0.5f;
        case M_FM: return K[0] > 80.0f || K[1] > 80.0f || K[2] > 80.0f || K[3] > 50.0f;
        default: return false;
        }
    }

    /* DRIFT: a slow random walk of pitch (up to 15 cents) and cutoff (up to 1 semitone), its own per machine */
    void updateDrift(const float *P)
    {
        const float amt = pot01(P[NW_P_DRIFT]);
        if (amt <= 0.0f) { driftPitch = driftCutoff = 0.0f; return; }
        auto rnd = [this] { drs ^= drs << 13; drs ^= drs >> 17; drs ^= drs << 5; return (float) (drs & 0xFFFF) / 32768.0f - 1.0f; };
        if ((drs & 511) < 4) { tgtPitch = rnd(); tgtCutoff = rnd(); }     /* a new target every ~0.2 s on average */
        rnd();
        driftPitch += (tgtPitch * 0.15f * amt - driftPitch) * 0.004f;
        driftCutoff += (tgtCutoff * 1.0f * amt - driftCutoff) * 0.004f;
    }

    WtMachine wt;
    FmMachine fm;
    WcMachine wc;
    AnMachine an;
    MachineFilter filter;
    Decimator2x dec;
    AnalogADSR amp, fenv;
    ValueSmoother<float> noteSmoother;
    double sampleRate = 44100.0;
    int slot = 0, machine = 0, midiNote = 60, lastOs = 1;
    float velocity = 0.0f, lastFn = -1000.0f;
    float driftPitch = 0.0f, driftCutoff = 0.0f, tgtPitch = 0.0f, tgtCutoff = 0.0f;
    uint32_t drs = 1;
    bool active = false, glissando = false, drum = false, wasOpen = false;
};

} // namespace nw
