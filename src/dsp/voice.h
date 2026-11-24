/* Tablor voice — the per-voice signal path, structured after
 * WavetableVoice.cpp in the original (FigBug/Wavetable, BSD-3):
 *
 *   osc1/osc2 (unison WT) ─┬─ preFilter ── Filter ──┐
 *   sub, noise ────────────┴─ postFilter ───────────┴─ VCA ADSR ─ out
 *
 * Per-source pre/post-filter routing, per-voice filter ADSR, 3 per-voice
 * glide via noteSmoother. Parameters are read from the engine's pot
 * array each block through param_map.h; the mod matrix contributes offsets.
 */
#pragma once

#include "../ported/wt_oscillator.h"
#include "../ported/analog_tables.h"
#include "../ported/filter.h"
#include "../ported/adsr.h"
#include "lfo.h"
#include "param_map.h"
#include "params.h"

#include <cstdlib>
#include <cstring>

namespace tb {

inline float velocityToGain(float velocity, float sensitivity)
{
    float v = velocity * sensitivity + 1.0f - sensitivity;
    float g = v * std::pow(25.0f, v) * 0.04f;
    return std::clamp(g, 0.0f, 1.0f);
}

inline constexpr int kBlock = 128;

/* Everything a voice needs from the engine each block. */
/* Matrix destinations. The ORDER is the option order of mmN_dst (DST2 in
 * tools/gen_wavetablor.py) -- the DSP receives the selection as an index, so
 * the two lists are one contract and must move together. NorthWest v1.1
 * dropped the four Bend/Formant entries: bend and formant are now FX1/FX2 of
 * the WARP effect mode, reached through the WTn FX1/FX2 destinations. */
enum ModDst {
    MD_NONE = 0,
    MD_WT1_POS, MD_WT2_POS, MD_WT1_LEVEL, MD_WT2_LEVEL, MD_WT1_TUNE, MD_WT2_TUNE,
    MD_WT1_PAN, MD_WT2_PAN,
    MD_FLT_FREQ, MD_FLT_RES, MD_SUB_LEVEL, MD_NOISE_LEVEL, MD_AMP,
    MD_PITCH, MD_WT1_FX1, MD_WT1_FX2, MD_WT2_FX1, MD_WT2_FX2, MD_UNI_DETUNE,
    MD_FLT2_FREQ, MD_FLT2_RES,
    MD_COUNT
};

/* GLIDE MODE options (tb_opts_5). LEGATO = 303-style slide: the pitch glides
 * and the envelopes carry on only when the new note overlaps the old one. */
enum GlideMode { GM_OFF = 0, GM_GLISSANDO, GM_PORTAMENTO, GM_LEGATO };

/* Matrix sources: the order is the option order of mmN_src in tools/gen_wavetablor.py */
enum ModSrc {
    MS_NONE = 0, MS_EG1, MS_EG2, MS_LFO1, MS_LFO2, MS_VELOCITY, MS_KEY, MS_MODWHEEL,
    MS_AFTERTOUCH, MS_PITCHBEND, MS_RANDOM, MS_AMPENV, MS_FLTENV, MS_COUNT
};
inline constexpr int kMatrixSlots = 8;

inline constexpr int kModEnvs = 2;

struct VoiceContext {
    const float *pots = nullptr;            /* TB_PARAM_COUNT raw values   */
    const Wavetable *table1 = nullptr;
    const Wavetable *table2 = nullptr;
    AnalogTables *analog = nullptr;
    float modWheel = 0.0f;                  /* 0..1  */
    float aftertouch = 0.0f;                /* 0..1  */
    float pitchBendSemis = 0.0f;            /* +/- pb_range already applied */
    float bpm = 120.0f;
};

class Voice {
public:
    explicit Voice(AnalogTables &analog) : sub(analog), noise(analog) {}

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        osc1.setSampleRate(sr); osc2.setSampleRate(sr);
        sub.setSampleRate(sr); noise.setSampleRate(sr);
        filter.setSampleRate(sr); filter2.setSampleRate(sr);
        for (auto &l : lfo) l.setSampleRate(sr);
        for (auto &f : fmOsc) { f.setSampleRate(sr); f.setBlockDC(false); }
        filterADSR.setSampleRate(sr);
        adsr.setSampleRate(sr);
        for (auto &e : modEnv) e.setSampleRate(sr);
        noteSmoother.setSampleRate(sr);
    }

    bool isActive() const  { return active; }
    bool isReleasing() const { return adsr.getState() == AnalogADSR::State::release; }
    int  currentNote() const { return midiNote; }
    uint32_t serialNumber() const { return serial; }
    float envOutput() const { return adsr.getOutput(); }

    void start(const VoiceContext &c, int note, float vel, uint32_t serial_,
               float glideFromNote)
    {
        midiNote = note;
        velocity = vel;
        serial = serial_;
        active = true;
        fastKill = false;

        osc1.setWavetable(c.table1);
        osc2.setWavetable(c.table2);

        const float *P = c.pots;
        const int glideMode = (int) P[TB_P_GLIDE_MODE];
        glissando  = glideMode == GM_GLISSANDO;
        portamento = glideMode == GM_PORTAMENTO;
        legatoMode = glideMode == GM_LEGATO;
        /* a fresh note (nothing held) glides only in GLISSANDO / PORTAMENTO;
         * LEGATO slides only between overlapping notes (retrigger below) */
        if (glideFromNote >= 0.0f && (glissando || portamento)) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValueUnsmoothed(glideFromNote / 127.0f);
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }

        filter.reset(); filter2.reset();
        rs = serial_ * 2654435761u + 12345u; if (!rs) rs = 1;
        noteRand = nextRand() * 2.0f - 1.0f;
        for (int i = 0; i < 2; i++) lfoVal[i] = 0.0f;
        filterADSR.reset();
        for (auto &e : modEnv) e.reset();

        updateParams(c, 0);
        for (int i = 0; i < 2; i++)
            lfo[i].noteOn((int) P[TB_P_LFO1_RETRIG + 5 * i] == 0, nextRandBits());
        for (auto &f : fmOsc) f.noteOn(0.0f);

        /* oscillator phases: free-running (the old retrig-off default —
         * the retrig switch was cut from the surface) */
        for (int o = 0; o < 2; o++) {
            auto &osc = o == 0 ? osc1 : osc2;
            float phases[8];
            for (int i = 0; i < 8; i++)
                phases[i] = (osc_mode(o) == UNI_PHASESYNC) ? 0.0f : (float) rand() / (float) RAND_MAX;
            osc.noteOn(phases, 8);
        }
        sub.noteOn(0.0f);
        noise.noteOn(0.0f);

        filterADSR.noteOn();
        for (auto &e : modEnv) e.noteOn();

        adsr.reset();
        adsr.noteOn();
    }

    /* A new note while the voice is still held: no phase reset. The glide
     * mode is read now, so a change made while a note is held applies. */
    void retrigger(const VoiceContext &c, int note, float vel)
    {
        const float *P = c.pots;
        const int glideMode = (int) P[TB_P_GLIDE_MODE];
        glissando  = glideMode == GM_GLISSANDO;
        portamento = glideMode == GM_PORTAMENTO;
        legatoMode = glideMode == GM_LEGATO;
        midiNote = note;
        velocity = vel;
        if (glissando || portamento || legatoMode) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        if (!legatoMode) {
            filterADSR.noteOn();
            for (auto &e : modEnv) e.noteOn();
            adsr.noteOn();
        }
    }

    void stop(bool allowTail)
    {
        adsr.noteOff();
        filterADSR.noteOff();
        for (auto &e : modEnv) e.noteOff();
        if (!allowTail) {
            active = false;
        }
    }

    void kill()
    {
        fastKill = true;
        adsr.noteOff(); filterADSR.noteOff();
        for (auto &e : modEnv) e.noteOff();
    }

    void render(const VoiceContext &c, float *outL, float *outR, int n)
    {
        if (!active) return;

        /* Re-point at the context's tables every block: the loader may have
         * swapped them since noteStarted, and the block's shared_ptr is the
         * only thing keeping a table alive. A held raw pointer would dangle.
         * A swap mid-note lands as a waveform step at this block's first
         * sample — fade the block in from silence (2.9 ms) to mask it. */
        bool swapped = (lastTable1 && c.table1 != lastTable1) ||
                       (lastTable2 && c.table2 != lastTable2);
        lastTable1 = c.table1;
        lastTable2 = c.table2;
        osc1.setWavetable(c.table1);
        osc2.setWavetable(c.table2);

        updateParams(c, n);

        float preL[kBlock] = {}, preR[kBlock] = {};
        float postL[kBlock] = {}, postR[kBlock] = {};

        const float *P = c.pots;

        /* FM: the modulator is the OTHER oscillator's table, read once, mono, at a
         * harmonic ratio of this oscillator's pitch. It is computed first so the
         * carrier can take it as a per-sample phase offset. */
        for (int o = 0; o < 2; o++) {
            vp(o).fm = nullptr;
            if (!fmActive[o]) continue;
            const Wavetable *src = o == 0 ? c.table2 : c.table1;
            if (!src || src->size() == 0) continue;
            float dummy[kBlock];
            std::memset(fmBuf[o], 0, (size_t) n * sizeof(float));
            std::memset(dummy, 0, (size_t) n * sizeof(float));
            WTOscillator::Params wp;
            wp.position = vp(1 - o).position; wp.leftGain = 1.0f; wp.rightGain = 0.0f;
            fmOsc[o].setWavetable(src);
            fmOsc[o].processAdding(oscNote[o] + fmRatioNote[o], wp, fmBuf[o], dummy, n);
            vp(o).fm = fmBuf[o];
        }

        /* Split routing sends osc 2 and the noise to the second filter; every
         * other routing (and the default, filter 2 off) is a single bus. */
        const bool split = flt2Type != 0 && route == 2;
        float bL[kBlock] = {}, bR[kBlock] = {};
        float *s2L = split ? bL : preL, *s2R = split ? bR : preR;

        /* Routing switches cut: every source runs through the filter. */
        if (oscGain[0] > 0.0f)
            osc1.processAdding(oscNote[0], vp1, preL, preR, n);
        if (oscGain[1] > 0.0f)
            osc2.processAdding(oscNote[1], vp2, s2L, s2R, n);
        if (subParams.leftGain + subParams.rightGain > 0.0f)
            sub.processAdding(subNote, subParams, preL, preR, n);
        if (noiseParams.leftGain + noiseParams.rightGain > 0.0f)
            noise.processAdding(60.0f, noiseParams, s2L, s2R, n);

        /* velocity */
        float vgain = velocityToGain(velocity, ampVelTrack) * ampModGain;
        for (int i = 0; i < n; i++) {
            preL[i] *= vgain;  preR[i] *= vgain;
            postL[i] *= vgain; postR[i] *= vgain;
            if (split) { bL[i] *= vgain; bR[i] *= vgain; }
        }

        float cL[kBlock], cR[kBlock];                          /* dry copy for parallel routing */
        if (flt2Type != 0 && route == 1) {
            std::memcpy(cL, preL, (size_t) n * sizeof(float));
            std::memcpy(cR, preR, (size_t) n * sizeof(float));
        }
#ifdef TB_DEBUG_FILTER
        {
            float pre = 0; for (int i = 0; i < n; i++) pre += preL[i] * preL[i];
            filter.process(preL, preR, n);
            float post = 0; for (int i = 0; i < n; i++) post += preL[i] * preL[i];
            static int dbg = 0;
            if (dbg++ % 100 == 0)
                printf("[voice] filt f=%.1f preRMS=%.4f postRMS=%.4f gain0=%.2f\n",
                       filter.getFrequency(), std::sqrt(pre / n), std::sqrt(post / n),
                       oscGain[0]);
        }
#else
        filter.process(preL, preR, n);
#endif

        /* Filter 2 (off by default, which leaves the path above untouched). */
        if (flt2Type != 0) {
            if (route == 0) {                                   /* serial */
                filter2.process(preL, preR, n);
            } else if (route == 1) {                            /* parallel */
                filter2.process(cL, cR, n);
                const float k = 0.70710678f;                    /* -3 dB: two paths add */
                for (int i = 0; i < n; i++) { preL[i] = (preL[i] + cL[i]) * k; preR[i] = (preR[i] + cR[i]) * k; }
            } else {                                            /* split */
                filter2.process(bL, bR, n);
                for (int i = 0; i < n; i++) { preL[i] += bL[i]; preR[i] += bR[i]; }
            }
        }

        for (int i = 0; i < n; i++) {
            postL[i] += preL[i];
            postR[i] += preR[i];
        }

        adsr.processMultiplying(postL, postR, n);

        if (swapped) {
            for (int i = 0; i < n; i++) {
                float g = (float) i / (float) n;
                postL[i] *= g;
                postR[i] *= g;
            }
        }

        if (adsr.getState() == AnalogADSR::State::idle)
            active = false;

        for (int i = 0; i < n; i++) {
            outL[i] += postL[i];
            outR[i] += postR[i];
        }

        noteSmoother.process(n);
    }

private:
    uint32_t nextRandBits() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
    float nextRand() { return (float) (nextRandBits() & 0xFFFFFF) / 16777216.0f; }
    int osc_mode(int o) const { return o == 0 ? vp1.mode : vp2.mode; }
    VoicedWTParams &vp(int o) { return o == 0 ? vp1 : vp2; }

    void updateParams(const VoiceContext &c, int blockSize)
    {
        const float *P = c.pots;

        /* ---- envelopes ---- */
        filterADSR.setAttack(potAttackTime(P[TB_P_FLT_A]));
        filterADSR.setDecay(potEnvTime(P[TB_P_FLT_D]));
        filterADSR.setSustainLevel(pot01(P[TB_P_FLT_S]));
        filterADSR.setRelease(potEnvTime(P[TB_P_FLT_R]));
        if (blockSize > 0) filterADSR.process(blockSize);

        adsr.setAttack(potAttackTime(P[TB_P_VCA_A]));
        adsr.setDecay(potEnvTime(P[TB_P_VCA_D]));
        adsr.setSustainLevel(pot01(P[TB_P_VCA_S]));
        adsr.setRelease(fastKill ? 0.01f : potEnvTime(P[TB_P_VCA_R]));

        /* ---- EG1 / EG2: modulation sources only, routed in the matrix ---- */
        for (int i = 0; i < MD_COUNT; i++) modOff[i] = 0.0f;
        {
            static const struct { int a, d, s, r; } k[kModEnvs] = {
                { TB_P_ME1_A, TB_P_ME1_D, TB_P_ME1_S, TB_P_ME1_R },
                { TB_P_ME2_A, TB_P_ME2_D, TB_P_ME2_S, TB_P_ME2_R },
            };
            for (int i = 0; i < kModEnvs; i++) {
                modEnv[i].setAttack(potAttackTime(P[k[i].a]));
                modEnv[i].setDecay(potEnvTime(P[k[i].d]));
                modEnv[i].setSustainLevel(pot01(P[k[i].s]));
                modEnv[i].setRelease(potEnvTime(P[k[i].r]));
                if (blockSize > 0) modEnv[i].process(blockSize);
            }
        }

        /* ---- LFOs ---- */
        for (int i = 0; i < 2; i++) {
            const int b = TB_P_LFO1_SHAPE + 5 * i;
            const int shape = std::clamp((int) P[b], 0, LFO_SHAPES - 1);
            float hz;
            if ((int) P[b + 1] == 1) {                       /* tempo-synced: the knob picks a division */
                const int div = (int) (pot01(P[b + 2]) * (float) (kLfoDivs - 1) + 0.5f);
                hz = (std::max(20.0f, c.bpm) / 60.0f) / lfoDivBeats(div);
            } else {
                hz = potLfoRate(P[b + 2]);
            }
            const float fade = P[b + 3] < 0.5f ? 0.0f : potAttackTime(P[b + 3]);
            if (blockSize > 0) lfoVal[i] = lfo[i].process(blockSize, hz, fade, shape);
        }

        /* ---- the modulation matrix: 8 slots of source -> destination x amount ---- */
        srcVal[MS_NONE]       = 0.0f;
        srcVal[MS_EG1]        = modEnv[0].getOutput();
        srcVal[MS_EG2]        = modEnv[1].getOutput();
        srcVal[MS_LFO1]       = lfoVal[0];
        srcVal[MS_LFO2]       = lfoVal[1];
        srcVal[MS_VELOCITY]   = velocity;
        srcVal[MS_KEY]        = std::clamp(((float) midiNote - 60.0f) / 60.0f, -1.0f, 1.0f);
        srcVal[MS_MODWHEEL]   = c.modWheel;
        srcVal[MS_AFTERTOUCH] = c.aftertouch;
        srcVal[MS_PITCHBEND]  = P[TB_P_PB_RANGE] > 0.0f
                                ? std::clamp(c.pitchBendSemis / P[TB_P_PB_RANGE], -1.0f, 1.0f) : 0.0f;
        srcVal[MS_RANDOM]     = noteRand;
        srcVal[MS_AMPENV]     = adsr.getOutput();
        srcVal[MS_FLTENV]     = filterADSR.getOutput();
        for (int m = 0; m < kMatrixSlots; m++) {
            const int b = TB_P_MM1_SRC + 3 * m;
            const int src = (int) P[b], dst = (int) P[b + 1];
            if (src > MS_NONE && src < MS_COUNT && dst > MD_NONE && dst < MD_COUNT)
                modOff[dst] += srcVal[src] * potBipolar(P[b + 2]);
        }

        /* ---- oscillators ---- */
        float baseNote = noteSmoother.getCurrentValue() * 127.0f;
        if (glissando) baseNote = (float) (int) (baseNote + 0.5f);
        baseNote += c.pitchBendSemis + modOff[MD_PITCH] * 24.0f;       /* matrix: +-2 octaves at full amount */

        for (int o = 0; o < 2; o++) {
            const int kTable  = o == 0 ? TB_P_WT1_TABLE : TB_P_WT2_TABLE;
            const int kPos    = o == 0 ? TB_P_WT1_POS : TB_P_WT2_POS;
            const int kLevel  = o == 0 ? TB_P_WT1_LEVEL : TB_P_WT2_LEVEL;
            const int kTune   = o == 0 ? TB_P_WT1_TUNE : TB_P_WT2_TUNE;
            const int kUni    = o == 0 ? TB_P_WT1_UNI : TB_P_WT2_UNI;
            const int kDet    = o == 0 ? TB_P_WT1_DETUNE : TB_P_WT2_DETUNE;
            const int kSpread = o == 0 ? TB_P_WT1_SPREAD : TB_P_WT2_SPREAD;
            const int kPan    = o == 0 ? TB_P_WT1_PAN : TB_P_WT2_PAN;
            (void) kTable;

            const int dPos  = o == 0 ? MD_WT1_POS : MD_WT2_POS;
            const int dLvl  = o == 0 ? MD_WT1_LEVEL : MD_WT2_LEVEL;
            const int dTun  = o == 0 ? MD_WT1_TUNE : MD_WT2_TUNE;
            const int dPan  = o == 0 ? MD_WT1_PAN : MD_WT2_PAN;
            const int dFx1  = o == 0 ? MD_WT1_FX1 : MD_WT2_FX1;
            const int dFx2  = o == 0 ? MD_WT1_FX2 : MD_WT2_FX2;

            VoicedWTParams &vp = o == 0 ? vp1 : vp2;

            /* tune modulation reads as an octave at full amount */
            oscNote[o] = baseNote + P[kTune] + modOff[dTun] * 12.0f;

            vp.voices   = std::clamp((int) P[kUni], 1, 6);
            vp.position = std::clamp(pot01(P[kPos]) + modOff[dPos], 0.0f, 1.0f);
            vp.gain     = std::clamp(potSquared(P[kLevel]) + modOff[dLvl], 0.0f, 1.0f);
            vp.pan      = std::clamp(potBipolar(P[kPan]) + modOff[dPan], -1.0f, 1.0f);
            vp.detune   = std::clamp(potDetune(P[kDet]) + modOff[MD_UNI_DETUNE], 0.0f, 1.0f);
            vp.spread   = pot01(P[kSpread]);
            oscGain[o] = vp.gain;

            /* ---- effect mode: what FX1 / FX2 mean depends on the mode ---- */
            const int fb = TB_P_WT1_FX_MODE + 4 * o;
            const int fxMode = std::clamp((int) P[fb], 0, 4);
            const float fx1 = std::clamp(pot01(P[fb + 1]) + modOff[dFx1], 0.0f, 1.0f);
            const float fx2 = std::clamp(pot01(P[fb + 2]) + modOff[dFx2], 0.0f, 1.0f);
            vp.mode = std::clamp((int) P[fb + 3], 0, UNI_COUNT - 1);
            vp.sync = 1.0f; vp.width = 0.0f; vp.fmDepth = 0.0f; vp.asym = 0.0f; vp.fold = 0.0f;
            vp.bend = 0.0f; vp.formant = 0.0f;
            fmActive[o] = false;
            switch (fxMode) {
            case 1: {                                        /* FM: FX1 = amount, FX2 = modulator ratio */
                static const float ratio[9] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
                vp.fmDepth = fx1 * fx1 * 2.0f;
                fmRatioNote[o] = 12.0f * std::log2(ratio[(int) (fx2 * 8.0f + 0.5f)]);
                fmActive[o] = vp.fmDepth > 0.0f;
                break; }
            case 2:                                          /* Sync: FX1 = hard sync, FX2 = width */
                vp.sync  = fx1 < 0.01f ? 1.0f : 1.0f + fx1 * fx1 * 7.0f;
                vp.width = fx2 < 0.01f ? 0.0f : fx2;
                break;
            case 3:                                          /* Fold: FX1 = asymmetry, FX2 = fold */
                vp.asym = fx1; vp.fold = fx2;
                break;
            case 4:                                          /* Warp: FX1 = bend, FX2 = formant */
                vp.bend = fx1; vp.formant = fx2;             /* (the old BEND/FORMANT knobs, upper half) */
                break;
            default: break;
            }
        }

        /* ---- sub + noise ---- */
        subNote = baseNote + P[TB_P_SUB_TUNE];
        float subLvl = std::clamp(potSquared(P[TB_P_SUB_LEVEL]) + modOff[MD_SUB_LEVEL],
                                  0.0f, 1.0f);
        float subPan = 0.0f;                 /* pan control cut: centered */
        switch ((int) P[TB_P_SUB_WAVE]) {
        case 0: subParams.wave = Wave::sine; break;
        case 1: subParams.wave = Wave::triangle; break;
        case 2: subParams.wave = Wave::sawUp; break;
        case 3: subParams.wave = Wave::pulse; subParams.pw = 0.50f; break;
        case 4: subParams.wave = Wave::pulse; subParams.pw = 0.25f; break;
        default: subParams.wave = Wave::pulse; subParams.pw = 0.125f; break;
        }
        subParams.leftGain  = subLvl * (1.0f - subPan);
        subParams.rightGain = subLvl * (1.0f + subPan);

        float nzLvl = std::clamp(potSquared(P[TB_P_NOISE_LEVEL]) + modOff[MD_NOISE_LEVEL],
                                 0.0f, 1.0f);
        float nzPan = 0.0f;                  /* pan control cut: centered */
        noiseParams.wave = (int) P[TB_P_NOISE_TYPE] == 0 ? Wave::whiteNoise : Wave::pinkNoise;
        noiseParams.leftGain  = nzLvl * (1.0f - nzPan);
        noiseParams.rightGain = nzLvl * (1.0f + nzPan);

        /* ---- amp ---- */
        ampVelTrack = pot01(P[TB_P_VCA_VEL]);
        ampModGain = std::clamp(1.0f + modOff[MD_AMP], 0.0f, 2.0f);

        /* ---- filter (the original's law, verbatim) ---- */
        {
            const float filterWidth = 12.0f * std::log2(20000.0f / 440.0f) + 69.0f; /* midi(20k) */
            float env = filterADSR.getOutput();
            float sens = pot01(P[TB_P_FLT_VEL]);
            sens = velocity * sens + 1.0f - sens;

            float n = potFilterNote(P[TB_P_FLT_FREQ]);
            n += ((float) midiNote - 60.0f) * pot01(P[TB_P_FLT_KEY]);
            n += env * sens * potBipolar(P[TB_P_FLT_ENV]) * filterWidth;
            n += modOff[MD_FLT_FREQ] * filterWidth;

            float f = midiNoteToHz(n);
            float maxFreq = std::min(20000.0f, (float) (sampleRate / 2.0));
            f = std::clamp(f, 4.0f, maxFreq);

            float res = std::clamp(pot01(P[TB_P_FLT_RES]) + modOff[MD_FLT_RES],
                                   0.0f, 1.0f);
            float q = kFilterQ / (1.0f - res * 0.99f);

            static const Filter::Type kTypes[8] = {
                Filter::lowpass, Filter::lowpass, Filter::highpass, Filter::highpass,
                Filter::bandpass, Filter::bandpass, Filter::notch, Filter::notch };
            int t = (int) P[TB_P_FLT_TYPE] & 7;
            filter.setType(kTypes[t]);
            filter.setSlope((t & 1) ? Filter::db24 : Filter::db12);
            filter.setParams(f, q);

            /* ---- filter 2: same law, its own cutoff / resonance / key / env amount ---- */
            route = std::clamp((int) P[TB_P_FLT_ROUTE], 0, 2);
            flt2Type = std::clamp((int) P[TB_P_FLT2_TYPE], 0, 8);
            if (flt2Type != 0) {
                float n2 = potFilterNote(P[TB_P_FLT2_FREQ]);
                n2 += ((float) midiNote - 60.0f) * pot01(P[TB_P_FLT2_KEY]);
                n2 += env * sens * potBipolar(P[TB_P_FLT2_ENV]) * filterWidth;
                n2 += modOff[MD_FLT2_FREQ] * filterWidth;
                float f2 = std::clamp(midiNoteToHz(n2), 4.0f, maxFreq);
                float res2 = std::clamp(pot01(P[TB_P_FLT2_RES]) + modOff[MD_FLT2_RES], 0.0f, 1.0f);
                const int t2 = flt2Type - 1;
                filter2.setType(kTypes[t2]);
                filter2.setSlope((t2 & 1) ? Filter::db24 : Filter::db12);
                filter2.setParams(f2, kFilterQ / (1.0f - res2 * 0.99f));
            }
        }
    }

    VoicedWTOscillator osc1 { 4 }, osc2 { 4 };
    VoicedWTParams vp1, vp2;
    StereoOscillator sub, noise;
    StereoOscillator::Params subParams, noiseParams;
    Filter filter, filter2;
    Lfo lfo[2];
    float lfoVal[2] = {};
    WTOscillator fmOsc[2];
    float fmBuf[2][kBlock] = {};
    bool  fmActive[2] = {};
    float fmRatioNote[2] = {};
    float noteRand = 0.0f;
    uint32_t rs = 1;
    float srcVal[MS_COUNT] = {};
    int   route = 0, flt2Type = 0;
    AnalogADSR filterADSR, adsr;
    AnalogADSR modEnv[kModEnvs];        /* the two spare, routable envelopes */
    float modOff[MD_COUNT] = {};        /* their summed offset per destination */
    ValueSmoother<float> noteSmoother;
    const Wavetable *lastTable1 = nullptr, *lastTable2 = nullptr;

    double sampleRate = 44100.0;
    float oscNote[2] = { 60.0f, 60.0f }, oscGain[2] = {};
    float subNote = 48.0f;
    float velocity = 0.0f, ampVelTrack = 1.0f, ampModGain = 1.0f;
    int midiNote = -1;
    uint32_t serial = 0;
    bool active = false, fastKill = false, glissando = false, portamento = false, legatoMode = false;
};

} // namespace tb
