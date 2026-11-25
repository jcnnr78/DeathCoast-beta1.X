#!/usr/bin/env python3
"""NorthWest factory presets (presets/northwest.tbl), written from the readable list below.

Test sounds for 1.5, not the final library. Every name starts with its kind: BASS, MELODY, DRUM, EFX, DRONE.
A preset is the defaults plus what it lists (unlisted keys keep their default). Knob numbers (kN) mean what the
machine's MODEL makes them mean (src/dsp/machines.h knobName). Machines 2-4 are silent (level 0) unless given one.
Run:  python3 tools/make_presets.py"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WT, FM, WC, AN = 0, 1, 2, 3                       # MODEL
SINE, TRI, SAW, PULSE = 0, 1, 2, 3                # osc_w1 / osc_w2
LP12, LP24, HP12, HP24, BP12, BP24 = range(6)     # ftype
# matrix: sources and destinations (tools/gen_params_v2.py MOD_SRC / MOD_DST)
EG1, EG2, LFO1, LFO2, VEL, KEY, RND = 1, 2, 3, 4, 5, 6, 10
PITCH, LEVEL, PAN, CUTOFF, RESO, TA, TB, TC, TD = range(1, 10)
SH, SMOOTH = 5, 6                                 # LFO shapes
LIB = "/data/UserData/UserLibrary/Wavetables/NorthWest/"   # the plugin maps this to <module>/wavetables/


def table(path): return LIB + path + " 2048.wav"


def m(n, model, **kv):
    """machine n's settings: m(2, FM, level=80, k1=60) -> m2_model=1, m2_level=80, m2_k1=60"""
    d = {"m%d_model" % n: model}
    if n > 1 and "level" not in kv: kv["level"] = 100
    for k, v in kv.items(): d["m%d_%s" % (n, k)] = table(v) if k == "table" else v
    return d


def mod(row, src, dst, amt, slot=0):
    """matrix row: amount 64 = 0, 127 = +full; slot 0 = every machine, 1..4 = that machine"""
    return {"mm%d_src" % row: src, "mm%d_dst" % row: dst, "mm%d_amt" % row: amt, "mm%d_slot" % row: slot}


def P(name, *parts, **glob):
    d = {}
    for p in parts: d.update(p)
    d.update(glob)
    return name, d


PRESETS = [
    P("Init", m(1, WT)),

    # ---------------------------------------------------------------- BASS
    P("BASS Acid Analog",
      m(1, AN, osc_w1=SAW, k3=70, k4=43, k7=127, k9=0, tune=-12, ftype=LP24, cutoff=45, reso=95, fenv=110, fdec=60, amp_d=70, amp_s=40, amp_r=30, dsend=25),
      glide=40, glide_mode=3),
    P("BASS DFAM Pulse",
      m(1, AN, osc_w1=PULSE, k1=30, k4=43, k5=50, k6=40, k7=127, k9=80, k10=82, osc_w2=SAW, tune=-12, cutoff=60, reso=60, fenv=100, fdec=55, amp_d=75, amp_s=0, amp_r=45)),
    P("BASS FM Growl",
      m(1, FM, fm_algo=0, k1=90, k2=60, k3=40, k4=30, k7=60, k8=64, k9=73, k10=64, tune=-12, amp_s=90, cutoff=90,
        reso=30),
      m(2, AN, osc_w1=SINE, k4=43, k7=127, k9=0, tune=-24, level=80, amp_s=110)),
    P("BASS West Rubber",
      m(1, WC, k1=50, k2=60, k3=20, k5=110, k7=45, tune=-12, amp_d=60, amp_s=0, amp_r=50),
      glide=30, glide_mode=3),
    P("BASS Wavetable Reese",
      m(1, WT, table="Basics/Sine to Saw", k1=90, k4=30, k7=85, k8=40, k5=80, k11=60, k12=70, tune=-12, cutoff=70,
        reso=20, fenv=85, fdec=70, amp_s=110)),

    # ---------------------------------------------------------------- MELODY
    P("MELODY Glass Keys",
      m(1, FM, fm_algo=6, k1=70, k8=84, k7=75, amp_d=85, amp_s=0, amp_r=85, dsend=40, rsend=70),
      m(2, WT, table="Instruments/E-Piano Tine", level=60, amp_a=5, amp_d=90, amp_s=30, amp_r=85, rsend=60)),
    P("MELODY Sync Lead",
      m(1, AN, osc_w1=SAW, k4=43, k5=70, k6=70, k7=127, k9=127, k10=96, k11=127, osc_w2=SAW, osc_sync=1, cutoff=100, reso=20, amp_s=110, dsend=50),
      glide=30, glide_mode=2),
    P("MELODY West Arp Bells",
      m(1, WC, k1=60, k8=84, k2=30, k5=110, k7=60, k12=60, amp_d=50, amp_s=0, amp_r=50, dsend=60, rsend=50),
      fx_dtime=85),
    P("MELODY Vocal Pluck",
      m(1, WT, table="Vocal/Vowels Female", k1=40, amp_d=70, amp_s=20, amp_r=60, cutoff=100, rsend=50),
      mod(1, LFO1, TA, 90), lfo1_rate=40),
    P("MELODY Four Machines",
      m(1, WT, table="Basics/Triangle to Saw", k4=20, amp_d=80, amp_s=40),
      m(2, FM, fm_algo=6, k1=60, k8=73, level=50, tune=12, amp_d=70, amp_s=0, amp_r=70),
      m(3, AN, osc_w1=PULSE, k4=43, k7=127, k9=0, level=60, tune=-12, amp_d=70, amp_s=30, cutoff=80),
      m(4, WC, k1=40, k5=100, level=40, tune=7, amp_d=60, amp_s=0, pan=90, dsend=60)),

    # ---------------------------------------------------------------- DRUM
    P("DRUM FM Kick",
      m(1, FM, fm_mode=1, fm_algo=7, k5=55, k11=38, k6=18, amp_d=68, tune=-24, vel=60)),
    P("DRUM Analog Kick",
      m(1, AN, osc_w1=SINE, k3=40, k4=43, k5=90, k6=35, k7=127, k9=0, tune=-24, amp_d=70, amp_s=0, amp_r=60)),
    P("DRUM Snare",
      m(1, FM, fm_mode=1, fm_algo=6, k1=75, k8=68, k7=40, k5=25, k11=30, k6=95, amp_d=55, cutoff=110),
      m(2, AN, osc_w1=TRI, k2=127, k4=43, k7=0, k9=0, ftype=HP12, cutoff=80, amp_d=55, amp_s=0, amp_r=45, level=90)),
    P("DRUM Metal Hat",
      m(1, FM, fm_mode=1, fm_algo=0, k1=110, k2=100, k3=90, k4=40, k8=99, k9=110, k10=121, k7=30, k6=60, amp_d=35,
        ftype=HP24, cutoff=100, tune=12)),
    P("DRUM West Tom",
      m(1, WC, k1=50, k8=73, k2=40, k5=127, k7=35, tune=-5, amp_d=45, amp_s=0, amp_r=40, rsend=30)),
    P("DRUM Perc Clank",
      m(1, FM, fm_mode=1, fm_algo=4, k1=80, k2=70, k3=60, k8=67, k9=78, k10=99, k7=35, k11=25, k5=20, amp_d=50,
        rsend=40)),

    # ---------------------------------------------------------------- EFX
    P("EFX Laser Zap",
      m(1, AN, osc_w1=SAW, k4=43, k5=127, k6=55, k7=127, k9=0, amp_d=60, amp_s=0, amp_r=50, cutoff=90, reso=70, dsend=70),
      fx_dfb=80),
    P("EFX Noise Riser",
      m(1, AN, osc_w1=SINE, k2=127, k4=43, k7=0, k9=0, ftype=BP24, cutoff=40, reso=100, amp_a=110, amp_s=127, amp_r=90, rsend=90),
      mod(1, EG1, CUTOFF, 127), me1_a=115, me1_s=127),
    P("EFX Ring Robot",
      m(1, WC, k1=127, k8=78, k2=90, k5=0, k12=0, amp_s=127, dsend=40),
      mod(1, LFO1, TB, 110), lfo1_shape=SH, lfo1_rate=90),
    P("EFX Glitch Steps",
      m(1, WT, table="Complex/Digital Steps", wt_fx=2, k2=90, amp_d=50, amp_s=0, dsend=60),
      mod(1, LFO2, TA, 127), lfo2_shape=SH, lfo2_rate=95),

    # ---------------------------------------------------------------- DRONE
    P("DRONE Dark Fifths",
      m(1, AN, osc_w1=SAW, k4=43, k7=127, k9=100, k10=82, k12=70, osc_w2=SAW, tune=-24, amp_a=100, amp_s=127, amp_r=120, cutoff=50, reso=50, rsend=80),
      m(2, WT, table="Sound Design/Hollow Pad", level=60, k7=127, k4=25, amp_a=100, amp_s=127, amp_r=120, rsend=80),
      mod(1, LFO1, CUTOFF, 80, 1), mod(2, LFO2, TA, 100, 2), lfo1_rate=15, lfo2_rate=10, lfo2_shape=SMOOTH),
    P("DRONE Buchla Bells",
      m(1, WC, level=80, k1=40, k2=50, k3=30, k5=30, k7=127, amp_a=90, amp_s=127, amp_r=120, rsend=70, dsend=30),
      m(2, FM, fm_algo=6, k1=50, k8=84, level=35, tune=12, amp_a=100, amp_s=127, amp_r=120, rsend=70),
      mod(1, LFO1, TB, 90, 1), mod(2, LFO2, TA, 80, 1), lfo1_rate=20, lfo2_rate=10, lfo2_shape=SMOOTH,
      fx_rsize=110),
    P("DRONE Vowel Choir",
      m(1, WT, table="Vocal/Choir Ah to Oo", k7=127, k4=30, k8=127, wt_umode=1, amp_a=110, amp_s=127, amp_r=120,
        rsend=100),
      m(2, WT, table="Vocal/Vowels Male", level=70, tune=-12, amp_a=110, amp_s=127, amp_r=120, rsend=100),
      mod(1, LFO1, TA, 100), lfo1_rate=8, fx_rsize=110),
]

if __name__ == "__main__":
    lines = ["# NorthWest factory sounds: Name|TBLR2;key=value;...  Written by tools/make_presets.py -- edit that.",
             "# Kinds: BASS, MELODY, DRUM, EFX, DRONE. Wavetables are the NorthWest pack shipped with the plugin."]
    for name, d in PRESETS:
        if "m1_level" not in d and name != "Init": d["m1_level"] = 100
        lines.append(name + "|TBLR2;" + "".join("%s=%s;" % (k, v) for k, v in d.items()))
    open(os.path.join(ROOT, "presets/northwest.tbl"), "w").write("\n".join(lines) + "\n")
    print("presets/northwest.tbl: %d presets" % len(PRESETS))
