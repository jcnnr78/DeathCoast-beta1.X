/* NorthWest engine (v2.0) -- 4 monophonic slots in layers. Note handling (last-note priority,
 * glide, sustain pedal), the shared modulation (EG1/EG2, LFO1/LFO2, random, controllers), the
 * four wavetables (one per slot), master out. The slots themselves are in machines.h.
 */
#pragma once

#include "machines.h"

#include <atomic>
#include <memory>

namespace tb {

class Engine {
public:
    Engine();

    /* Pots are written by set_param (UI thread) and read by the audio
     * thread; aligned float stores are atomic. */
    float pots[TB_PARAM_COUNT] = {};

    void setHostBpm(float bpm) { hostBpm = bpm; }
    void onMidi(const uint8_t *msg, int len);
    void renderBlock(int16_t *outInterleavedLr, int frames);

    /* slot n's wavetable (the loader thread swaps them) */
    void setTable(int slot, std::shared_ptr<Wavetable> t)
    {
        if (slot >= 0 && slot < TB_SLOTS) std::atomic_store(&tables[slot], std::move(t));
    }
    void allNotesOff(bool allowTail);

private:
    void noteOn(int note, float vel);
    void noteOff(int note);
    void releaseAll();
    bool slotOn(int s) const
    {
        return pots[slotParam(s, TB_P_S1_LEVEL)] > 0.0f && (int) pots[slotParam(s, TB_P_S1_MUTE)] == 0;
    }
    void modStart(bool fresh);
    void updateMod(int n);

    Slot slots[TB_SLOTS];
    std::shared_ptr<Wavetable> tables[TB_SLOTS];

    /* shared modulation */
    AnalogADSR modEnv[kModEnvs];
    Lfo lfo[2];
    float lfoVal[2] = {};
    ModShared ms;
    float noteRand = 0.0f, velocity = 0.0f;
    int curNote = 60;
    uint32_t rs = 1;
    uint32_t nextRandBits() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

    /* held-note stack, last-note priority */
    static constexpr int kStackMax = 32;
    int  monoStack[kStackMax];
    int  monoStackLen = 0;
    bool held = false;            /* a key (or the pedal) is holding the current note */
    float lastPlayedNote = -1.0f;
    uint32_t serialCounter = 0;

    /* controller state */
    float modWheel = 0.0f, aftertouch = 0.0f;
    float pitchBendNorm = 0.0f;
    float hostBpm = 120.0f;
    bool  sustainDown = false, pedalHolds = false;
};

/* Built-in "Init" wavetable: 64-frame PWM morph, generated in memory so the
 * synth makes sound with no files installed. */
std::shared_ptr<Wavetable> makeInitTable();

} // namespace tb
