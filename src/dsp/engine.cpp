#include "engine.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace nw {

Engine::Engine()
{
    for (int i = 0; i < NW_PARAM_COUNT; i++) pots[i] = nw_params[i].def;
    for (int s = 0; s < NW_SLOTS; s++) { slots[s].setSampleRate(44100.0); slots[s].setSeed(0x9E3779B9u * (uint32_t) (s + 1)); }
    for (auto &e : modEnv) e.setSampleRate(44100.0);
    for (auto &l : lfo) l.setSampleRate(44100.0);
    delay.setSampleRate(44100.0); reverb.setSampleRate(44100.0); dirt.setSampleRate(44100.0);
    limiter.setSampleRate(44100.0);
    auto init = makeInitTable();
    for (auto &t : tables) std::atomic_store(&t, init);
}

/* EG1/EG2, LFOs and the per-note random: started by a new note, retriggered unless LEGATO */
void Engine::modStart(bool fresh)
{
    if (fresh) {
        noteRand = (float) (nextRandBits() & 0xFFFFFF) / 16777216.0f * 2.0f - 1.0f;
        for (int i = 0; i < 2; i++)
            lfo[i].noteOn((int) pots[NW_P_LFO1_RETRIG + 5 * i] == 0, nextRandBits());
        for (auto &e : modEnv) e.reset();
    }
    for (auto &e : modEnv) e.noteOn();
}

void Engine::updateMod(int n)
{
    const float *P = pots;
    static const struct { int a, d, s, r; } k[kModEnvs] = {
        { NW_P_ME1_A, NW_P_ME1_D, NW_P_ME1_S, NW_P_ME1_R }, { NW_P_ME2_A, NW_P_ME2_D, NW_P_ME2_S, NW_P_ME2_R } };
    for (int i = 0; i < kModEnvs; i++) {
        modEnv[i].setAttack(potAttackTime(P[k[i].a]));
        modEnv[i].setDecay(potEnvTime(P[k[i].d]));
        modEnv[i].setSustainLevel(pot01(P[k[i].s]));
        modEnv[i].setRelease(potEnvTime(P[k[i].r]));
        modEnv[i].process(n);
    }
    for (int i = 0; i < 2; i++) {
        const int b = NW_P_LFO1_SHAPE + 5 * i;
        const int shape = std::clamp((int) P[b], 0, LFO_SHAPES - 1);
        float hz;
        if ((int) P[b + 1] == 1) {
            const int div = (int) (pot01(P[b + 2]) * (float) (kLfoDivs - 1) + 0.5f);
            hz = (std::max(20.0f, hostBpm) / 60.0f) / lfoDivBeats(div);
        } else {
            hz = potLfoRate(P[b + 2]);
        }
        const float fade = P[b + 3] < 0.5f ? 0.0f : potAttackTime(P[b + 3]);
        lfoVal[i] = lfo[i].process(n, hz, fade, shape);
    }
    const float pbr = P[NW_P_PB_RANGE];
    ms.pitchBendSemis = pitchBendNorm * pbr;
    ms.bpm = hostBpm;
    ms.src[MS_NONE] = 0.0f;
    ms.src[MS_EG1] = modEnv[0].getOutput();
    ms.src[MS_EG2] = modEnv[1].getOutput();
    ms.src[MS_LFO1] = lfoVal[0];
    ms.src[MS_LFO2] = lfoVal[1];
    ms.src[MS_MODWHEEL] = modWheel;
    ms.src[MS_AFTERTOUCH] = aftertouch;
    ms.src[MS_PITCHBEND] = pbr > 0.0f ? std::clamp(pitchBendNorm, -1.0f, 1.0f) : 0.0f;
    ms.src[MS_RANDOM] = noteRand;
}

bool Engine::anyHeld() const
{
    for (const auto &v : voices) if (v.held) return true;
    return false;
}

void Engine::noteOn(int source, int note, float vel)
{
    const int gm = (int) pots[NW_P_GLIDE_MODE];
    const bool glideOn = gm != GM_OFF, legato = gm == GM_LEGATO;
    const bool wasAny = anyHeld();
    bool started = false;
    for (int s = 0; s < NW_SLOTS; s++) {
        if (sourceOf(s) != source) continue;
        Voice &v = voices[s];
        if (v.len < Voice::kStackMax) v.stack[v.len++] = note;
        const bool wasHeld = v.held;
        v.held = true; v.pedalHolds = false;
        v.curNote = note; v.velocity = vel;
        if (!slotOn(s)) { if (slots[s].isActive()) slots[s].kill(); continue; }
        if (wasHeld && slots[s].isActive())
            slots[s].retrigger(pots, note, vel, ++serialCounter);
        else
            slots[s].start(s, pots, std::atomic_load(&tables[s]).get(), note, vel, glideOn ? v.lastPlayed : -1.0f,
                           ++serialCounter);
        v.lastPlayed = (float) note;
        started = true;
    }
    /* EG1/EG2: a fresh start when nothing was held; LEGATO keeps them running between overlapping notes */
    if (started && (!wasAny || !legato)) modStart(!wasAny);
}

void Engine::release(int s)
{
    voices[s].held = false;
    if (slots[s].isActive()) slots[s].stop();
    if (!anyHeld()) for (auto &e : modEnv) e.noteOff();
}

void Engine::noteOff(int source, int note)
{
    for (int s = 0; s < NW_SLOTS; s++) {
        if (sourceOf(s) != source) continue;
        Voice &v = voices[s];
        for (int i = 0; i < v.len; i++)
            if (v.stack[i] == note) {
                for (int j = i; j < v.len - 1; j++) v.stack[j] = v.stack[j + 1];
                v.len--;
                break;
            }
        if (!v.held || note != v.curNote) continue;
        if (v.len > 0) {                                  /* back to the most recent held note */
            const int prev = v.stack[v.len - 1];
            v.curNote = prev;
            if (slots[s].isActive()) slots[s].retrigger(pots, prev, v.velocity, ++serialCounter);
            v.lastPlayed = (float) prev;
            if ((int) pots[NW_P_GLIDE_MODE] != GM_LEGATO) for (auto &e : modEnv) e.noteOn();
        } else if (sustainDown[source]) {
            v.pedalHolds = true;
        } else {
            release(s);
        }
    }
}

void Engine::allNotesOff(int source, bool allowTail)
{
    for (int s = 0; s < NW_SLOTS; s++) {
        if (source >= 0 && sourceOf(s) != source) continue;
        if (slots[s].isActive()) { if (allowTail) slots[s].stop(); else slots[s].kill(); }
        voices[s].len = 0; voices[s].held = false; voices[s].pedalHolds = false;
    }
    if (!anyHeld()) for (auto &e : modEnv) allowTail ? e.noteOff() : e.reset();
}

void Engine::onMidi(int source, const uint8_t *msg, int len)
{
    if (len < 2) return;
    switch (msg[0] & 0xF0) {
    case 0x90:
        if (len >= 3 && msg[2] > 0) { noteOn(source, msg[1], (float) msg[2] / 127.0f); break; }
        [[fallthrough]];
    case 0x80:
        noteOff(source, msg[1]);
        break;
    case 0xB0:
        if (len >= 3) {
            switch (msg[1]) {
            case 1: modWheel = (float) msg[2] / 127.0f; break;
            case 64: {
                const bool down = msg[2] >= 64;
                if (sustainDown[source] && !down)
                    for (int s = 0; s < NW_SLOTS; s++)
                        if (sourceOf(s) == source && voices[s].pedalHolds) { voices[s].pedalHolds = false; release(s); }
                sustainDown[source] = down;
                break; }
            case 120: case 123: allNotesOff(source, msg[1] == 123); break;
            }
        }
        break;
    case 0xD0: aftertouch = (float) msg[1] / 127.0f; break;
    case 0xE0:
        if (len >= 3) pitchBendNorm = (float) (((msg[2] << 7) | msg[1]) - 8192) / 8192.0f;
        break;
    }
}

void Engine::renderBlock(int16_t *out, int frames)
{
    std::shared_ptr<Wavetable> t[NW_SLOTS];
    for (int s = 0; s < NW_SLOTS; s++) t[s] = std::atomic_load(&tables[s]);
    float L[kBlock], R[kBlock], SL[kBlock], SR[kBlock];
    float DL[kBlock], DR[kBlock], VL[kBlock], VR[kBlock], XL[kBlock], XR[kBlock];
    int done = 0;
    while (done < frames) {
        const int n = std::min(frames - done, kBlock);
        const size_t bytes = (size_t) n * sizeof(float);
        for (float *b : { L, R, DL, DR, VL, VR, XL, XR }) std::memset(b, 0, bytes);
        updateMod(n);
        for (int s = 0; s < NW_SLOTS; s++) {
            if (!slots[s].isActive()) continue;
            if ((int) pots[slotParam(s, NW_P_M1_MUTE)] != 0) { slots[s].kill(); continue; }
            std::memset(SL, 0, bytes); std::memset(SR, 0, bytes);
            slots[s].render(pots, t[s].get(), ms, SL, SR, n);
            /* dry into the mix, and the machine's sends into the delay, reverb and DIRT buses */
            const float ds = potSquared(pots[slotParam(s, NW_P_M1_DSEND)]);
            const float rv = potSquared(pots[slotParam(s, NW_P_M1_RSEND)]);
            const float xs = potSquared(pots[slotParam(s, NW_P_M1_XSEND)]);
            for (int i = 0; i < n; i++) {
                L[i] += SL[i]; R[i] += SR[i];
                DL[i] += SL[i] * ds; DR[i] += SR[i] * ds;
                VL[i] += SL[i] * rv; VR[i] += SR[i] * rv;
                XL[i] += SL[i] * xs; XR[i] += SR[i] * xs;
            }
        }
        dirt.process(XL, XR, L, R, n, pot01(pots[NW_P_FX_XDRIVE]), pot01(pots[NW_P_FX_XCHAR]),
                     pot01(pots[NW_P_FX_XTONE]), pot01(pots[NW_P_FX_XGRIT]));
        delay.process(DL, DR, L, R, n, pot01(pots[NW_P_FX_DTIME]), pot01(pots[NW_P_FX_DFB]), pot01(pots[NW_P_FX_DTONE]),
                      (int) pots[NW_P_FX_DMODE] == 1, potSquared(pots[NW_P_FX_DLEVEL]));
        reverb.process(VL, VR, L, R, n, pot01(pots[NW_P_FX_RSIZE]), pot01(pots[NW_P_FX_RDAMP]),
                       potSquared(pots[NW_P_FX_RLEVEL]));
        const float master = potSquared(pots[NW_P_VOLUME]) * 0.8f;
        for (int i = 0; i < n; i++) { L[i] *= master; R[i] *= master; }
        limiter.process(L, R, n);
        for (int i = 0; i < n; i++) {
            out[(done + i) * 2 + 0] = (int16_t) std::lrint(std::clamp(L[i], -1.0f, 1.0f) * 32767.0f);
            out[(done + i) * 2 + 1] = (int16_t) std::lrint(std::clamp(R[i], -1.0f, 1.0f) * 32767.0f);
        }
        done += n;
    }
}

std::shared_ptr<Wavetable> makeInitTable()
{
    /* 64 frames x 2048: pulse morphing 50% -> 6% duty with a saw underlay.
     * Naive shapes on purpose — wtBuild band-limits every level. */
    const int F = 64, S = 2048;
    std::vector<float> data((size_t) F * S);
    for (int f = 0; f < F; f++) {
        float m = (float) f / (F - 1);
        float pw = 0.5f - 0.44f * m;
        for (int i = 0; i < S; i++) {
            float ph = (float) i / S;
            float pulse = ph < pw ? 1.0f : -1.0f;
            float saw = 2.0f * ph - 1.0f;
            data[(size_t) f * S + i] = 0.8f * ((1.0f - m * 0.5f) * pulse * 0.6f +
                                               m * 0.4f * saw);
        }
    }
    /* normalize the source to 0.9 peak so the default patch lands hot */
    float peak = 0.0f;
    for (float v : data) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f)
        for (float &v : data) v *= 0.9f / peak;

    auto wt = std::make_shared<Wavetable>();
    wtBuild(*wt, data.data(), F, S, 44100.0f, 44100.0f);
    return wt;
}

} // namespace nw
