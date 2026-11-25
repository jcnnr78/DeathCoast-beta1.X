/* DeathCoast engine -- 4 monophonic machines (slots), each played from the plugin's track or from its own MIDI
 * channel on the plugin's MIDI port; per machine: last-note priority, glide, sustain pedal. Shared: the modulation
 * (EG1-EG4, LFO1-LFO4, random, controllers), the effects (delay, reverb, DIRT) and the master limiter.
 * The machines themselves are in machines.h.
 */
#pragma once

#include "machines.h"
#include "fx.h"

#include <atomic>
#include <memory>

namespace dc {

class Engine {
public:
    Engine();

    /* Pots are written by set_param and read by the audio thread; aligned float stores are atomic. */
    float pots[DC_PARAM_COUNT] = {};

    void setHostBpm(float bpm) { hostBpm = bpm; }
    /* MIDI from the plugin's own track (the machines on TRACK) / from its MIDI port (the machines on CH n) */
    void onTrackMidi(const uint8_t *msg, int len) { onMidi(0, msg, len); }
    void onPortMidi(const uint8_t *msg, int len) { onMidi(1 + (msg[0] & 0x0F), msg, len); }
    void renderBlock(int16_t *outInterleavedLr, int frames);

    /* machine n's wavetable (the loader thread swaps them) */
    void setTable(int slot, std::shared_ptr<Wavetable> t)
    {
        if (slot >= 0 && slot < DC_SLOTS) std::atomic_store(&tables[slot], std::move(t));
    }
    std::shared_ptr<Wavetable> table(int slot) const
    {
        return slot >= 0 && slot < DC_SLOTS ? std::atomic_load(&tables[slot]) : nullptr;
    }
    void allNotesOff(int source, bool allowTail);
    /* the MPC's transport (HAS_TRANSPORT): on stop the delay and reverb fade away; play or a new note ends the fade */
    void setTransport(bool playing);

private:
    /* a machine's notes: the keys held for it, last-note priority */
    struct Voice {
        static constexpr int kStackMax = 32;
        int stack[kStackMax];
        int len = 0;
        bool held = false, pedalHolds = false;
        int curNote = 60;
        float velocity = 0.0f, lastPlayed = -1.0f;
    };

    void onMidi(int source, const uint8_t *msg, int len);
    int sourceOf(int s) const { return (int) pots[slotParam(s, DC_P_M1_CHAN)]; }
    bool slotOn(int s) const
    {
        return pots[slotParam(s, DC_P_M1_LEVEL)] > 0.0f && (int) pots[slotParam(s, DC_P_M1_MUTE)] == 0;
    }
    void noteOn(int source, int note, float vel);
    void noteOff(int source, int note);
    void release(int s);
    bool anyHeld() const;
    void modStart(bool fresh);
    void updateMod(int n);

    AnalogTables analog { 44100.0 };
    Slot slots[DC_SLOTS] = { Slot(analog), Slot(analog), Slot(analog), Slot(analog) };
    Voice voices[DC_SLOTS];
    std::shared_ptr<Wavetable> tables[DC_SLOTS];

    /* shared modulation */
    Envelope modEnv[kModEnvs];
    Lfo lfo[kLfos];
    float lfoVal[kLfos] = {};
    ModShared ms;
    float noteRand = 0.0f;
    uint32_t rs = 1;
    uint32_t nextRandBits() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
    uint32_t serialCounter = 0;

    /* the fade when the MPC stops: its time in samples (-1: none) */
    static constexpr int kStopFb = 44100, kStopFadeStart = 44100, kStopFadeEnd = 132300;   /* 1 s, 1 s, 3 s */
    int stopT = -1;

    /* effects and master */
    Delay delay;
    Reverb reverb;
    Dirt dirt;
    Limiter limiter;

    /* controller state; the sustain pedal per source (0 = track, 1..16 = port channels) */
    float modWheel = 0.0f, aftertouch = 0.0f, pitchBendNorm = 0.0f, hostBpm = 120.0f;
    bool sustainDown[17] = {};
};

/* Built-in "Init" wavetable: 64-frame PWM morph, generated in memory so the synth makes sound with no files. */
std::shared_ptr<Wavetable> makeInitTable();

} // namespace dc
