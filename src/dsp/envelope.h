/* DeathCoast envelopes and glide (1.8: written for DeathCoast, replacing the ADSR and smoother it inherited).
 *
 * Envelope: attack, decay, sustain, release with analog-style curves. Each stage is a one-pole lag heading for a
 * target placed past where the stage ends, so the stage ends on time and keeps its curve: the attack aims a little
 * over the top (a fast, slightly rounded rise), decay and release aim a hair below their end (the long exponential
 * fall of a capacitor discharging). A stage finishes the moment its end level is crossed.
 *   - noteOn from silence starts at 0; from a sounding envelope it rises from where it is (no click).
 *   - attack 0 jumps to the top at once (percussive sounds).
 *   - noteOff from any stage goes to release from the current level.
 * Glide: a value that moves to its target in a straight line at a set speed (the note, for portamento). */
#pragma once

#include <cmath>

namespace dc {

class Envelope {
public:
    enum class State { idle, attack, decay, sustain, release };

    void setSampleRate(double sr) { sampleRate = (float) sr; dirty = true; }
    void setAttack(float seconds)  { if (seconds != atk) { atk = seconds; dirty = true; } }
    void setDecay(float seconds)   { if (seconds != dec) { dec = seconds; dirty = true; } }
    void setRelease(float seconds) { if (seconds != rel) { rel = seconds; dirty = true; } }
    void setSustainLevel(float s)  { if (s != sus) { sus = s; dirty = true; } }

    float getOutput() const { return out; }
    State getState() const { return state; }

    void noteOn()
    {
        update();
        if (state == State::idle) out = 0.0f;
        if (atk <= 0.0f) { out = 1.0f; state = State::decay; }
        else state = State::attack;
    }
    void noteOff() { state = out > 0.0f ? State::release : State::idle; }
    void reset() { state = State::idle; out = 0.0f; }

    inline float process()
    {
        switch (state) {
        case State::attack:
            out += (kAtkTarget - out) * atkC;
            if (out >= 1.0f) { out = 1.0f; state = State::decay; }
            break;
        case State::decay:
            out += (sus - kTail - out) * decC;
            if (out <= sus) { out = sus; state = State::sustain; }
            break;
        case State::sustain:
            out = sus;                                     /* follows SUSTAIN as it moves */
            break;
        case State::release:
            out += (-kTail - out) * relC;
            if (out <= 0.0f) { out = 0.0f; state = State::idle; }
            break;
        case State::idle: break;
        }
        return out;
    }

    float process(int n)
    {
        update();
        for (int i = 0; i < n; i++) process();
        return out;
    }

    /* the VCA: multiplies stereo audio in place, sample by sample */
    void processMultiplying(float *l, float *r, int n)
    {
        update();
        for (int i = 0; i < n; i++) { const float g = process(); l[i] *= g; r[i] *= g; }
    }

private:
    /* how far past its end a stage aims: the attack 0.61 over the top, decay and release 0.0067 under */
    static constexpr float kAtkOver = 0.60653066f, kTail = 0.0067379470f;
    static constexpr float kAtkTarget = 1.0f + kAtkOver;

    /* the lag coefficient that crosses from `from` to the stage's end in `seconds` heading for `over` past it:
     * the distance to the target shrinks by (over / (span + over)) over the stage */
    float coef(float seconds, float span, float over) const
    {
        const float samples = std::fmax(seconds * sampleRate, 1.0f);
        return 1.0f - std::exp(std::log(over / (span + over)) / samples);
    }
    void update()
    {
        if (!dirty) return;
        dirty = false;
        atkC = coef(atk, 1.0f, kAtkOver);
        decC = coef(dec, 1.0f, kTail);                     /* the time for a full fall, whatever the sustain */
        relC = coef(rel, 1.0f, kTail);
    }

    State state = State::idle;
    float sampleRate = 44100.0f;
    float atk = 0.2f, dec = 0.2f, sus = 0.8f, rel = 0.2f;
    float atkC = 0.0f, decC = 0.0f, relC = 0.0f, out = 0.0f;
    bool dirty = true;
};

class Glide {
public:
    void setSampleRate(double sr) { sampleRate = sr; setTime(time); }
    void setTime(double seconds)
    {
        time = seconds;
        step = time > 0.0 ? (float) (1.0 / (sampleRate * time)) : 1.0f;   /* a full 0..1 sweep takes `time` */
    }
    void setValue(float v) { target = v; }
    void setValueUnsmoothed(float v) { target = value = v; }
    float getCurrentValue() const { return value; }
    float getTargetValue() const { return target; }

    void process(int n)
    {
        if (value == target) return;
        const float d = step * (float) n;
        if (value < target) value = std::fmin(target, value + d);
        else value = std::fmax(target, value - d);
    }

private:
    double sampleRate = 44100.0, time = 0.1;
    float step = 0.0f, value = 0.0f, target = 0.0f;
};

} // namespace dc
