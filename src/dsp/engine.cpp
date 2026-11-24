#include "engine.h"

#include <cmath>
#include <cstring>

namespace tb {

Engine::Engine()
{
    for (int i = 0; i < TB_PARAM_COUNT; i++)
        pots[i] = tb_params[i].def;
    for (auto &s : slots) s.setSampleRate(44100.0);
    for (auto &e : modEnv) e.setSampleRate(44100.0);
    for (auto &l : lfo) l.setSampleRate(44100.0);
    auto init = makeInitTable();
    for (auto &t : tables) std::atomic_store(&t, init);
}

/* EG1/EG2, LFOs and the per-note random: started by a new note, retriggered unless LEGATO */
void Engine::modStart(bool fresh)
{
    if (fresh) {
        noteRand = (float) (nextRandBits() & 0xFFFFFF) / 16777216.0f * 2.0f - 1.0f;
        for (int i = 0; i < 2; i++)
            lfo[i].noteOn((int) pots[TB_P_LFO1_RETRIG + 5 * i] == 0, nextRandBits());
        for (auto &e : modEnv) e.reset();
    }
    for (auto &e : modEnv) e.noteOn();
}

void Engine::updateMod(int n)
{
    const float *P = pots;
    static const struct { int a, d, s, r; } k[kModEnvs] = {
        { TB_P_ME1_A, TB_P_ME1_D, TB_P_ME1_S, TB_P_ME1_R }, { TB_P_ME2_A, TB_P_ME2_D, TB_P_ME2_S, TB_P_ME2_R } };
    for (int i = 0; i < kModEnvs; i++) {
        modEnv[i].setAttack(potAttackTime(P[k[i].a]));
        modEnv[i].setDecay(potEnvTime(P[k[i].d]));
        modEnv[i].setSustainLevel(pot01(P[k[i].s]));
        modEnv[i].setRelease(potEnvTime(P[k[i].r]));
        modEnv[i].process(n);
    }
    for (int i = 0; i < 2; i++) {
        const int b = TB_P_LFO1_SHAPE + 5 * i;
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
    const float pbr = P[TB_P_PB_RANGE];
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

void Engine::noteOn(int note, float vel)
{
    if (monoStackLen < kStackMax) monoStack[monoStackLen++] = note;
    const bool glideOn = (int) pots[TB_P_GLIDE_MODE] != 0;
    const bool legato = (int) pots[TB_P_GLIDE_MODE] == GM_LEGATO;
    const bool wasHeld = held;
    held = true; pedalHolds = false;
    curNote = note; velocity = vel;
    if (!wasHeld || !legato) modStart(!wasHeld);     /* LEGATO: EG1/EG2 keep running between overlapping notes */
    for (int s = 0; s < TB_SLOTS; s++) {
        if (!slotOn(s)) { if (slots[s].isActive()) slots[s].kill(); continue; }
        auto t = std::atomic_load(&tables[s]);
        if (wasHeld && slots[s].isActive())
            slots[s].retrigger(pots, note, vel, ++serialCounter);
        else
            slots[s].start(s, pots, t.get(), note, vel, glideOn ? lastPlayedNote : -1.0f, ++serialCounter);
    }
    lastPlayedNote = (float) note;
}

void Engine::releaseAll()
{
    held = false;
    for (auto &e : modEnv) e.noteOff();
    for (auto &s : slots) if (s.isActive()) s.stop();
}

void Engine::noteOff(int note)
{
    for (int i = 0; i < monoStackLen; i++)
        if (monoStack[i] == note) {
            for (int j = i; j < monoStackLen - 1; j++) monoStack[j] = monoStack[j + 1];
            monoStackLen--;
            break;
        }
    if (!held || note != curNote) return;
    if (monoStackLen > 0) {                 /* back to the most recent held note */
        const int prev = monoStack[monoStackLen - 1];
        curNote = prev;
        for (auto &s : slots) if (s.isActive()) s.retrigger(pots, prev, velocity, ++serialCounter);
        if ((int) pots[TB_P_GLIDE_MODE] != GM_LEGATO) for (auto &e : modEnv) e.noteOn();
    } else if (sustainDown) {
        pedalHolds = true;
    } else {
        releaseAll();
    }
}

void Engine::allNotesOff(bool allowTail)
{
    for (auto &s : slots) if (s.isActive()) { if (allowTail) s.stop(); else s.kill(); }
    for (auto &e : modEnv) allowTail ? e.noteOff() : e.reset();
    monoStackLen = 0; held = false; pedalHolds = false;
}

void Engine::onMidi(const uint8_t *msg, int len)
{
    if (len < 2) return;
    const uint8_t status = msg[0] & 0xF0;
    switch (status) {
    case 0x90:
        if (len >= 3 && msg[2] > 0) { noteOn(msg[1], (float) msg[2] / 127.0f); break; }
        [[fallthrough]];
    case 0x80:
        noteOff(msg[1]);
        break;
    case 0xB0:
        if (len >= 3) {
            switch (msg[1]) {
            case 1: modWheel = (float) msg[2] / 127.0f; break;
            case 64: {
                const bool down = msg[2] >= 64;
                if (sustainDown && !down && pedalHolds) { pedalHolds = false; releaseAll(); }
                sustainDown = down;
                break; }
            case 120: case 123: allNotesOff(msg[1] == 123); break;
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
    std::shared_ptr<Wavetable> t[TB_SLOTS];
    for (int s = 0; s < TB_SLOTS; s++) t[s] = std::atomic_load(&tables[s]);
    float L[kBlock], R[kBlock];
    int done = 0;
    while (done < frames) {
        const int n = std::min(frames - done, kBlock);
        std::memset(L, 0, (size_t) n * sizeof(float));
        std::memset(R, 0, (size_t) n * sizeof(float));
        updateMod(n);
        for (int s = 0; s < TB_SLOTS; s++) {
            if (!slots[s].isActive()) continue;
            if ((int) pots[slotParam(s, TB_P_M1_MUTE)] != 0) { slots[s].kill(); continue; }
            slots[s].render(pots, t[s].get(), ms, L, R, n);
        }
        const float master = potSquared(pots[TB_P_VOLUME]) * 0.8f;
        for (int i = 0; i < n; i++) {
            const float l = std::clamp(L[i] * master, -1.0f, 1.0f);
            const float r = std::clamp(R[i] * master, -1.0f, 1.0f);
            out[(done + i) * 2 + 0] = (int16_t) (l * 32767.0f);
            out[(done + i) * 2 + 1] = (int16_t) (r * 32767.0f);
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

} // namespace tb
