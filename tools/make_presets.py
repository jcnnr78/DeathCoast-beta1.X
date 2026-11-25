#!/usr/bin/env python3
"""NorthWest factory presets (presets/northwest.tbl), written from the readable list below.

Test sounds, not the final library. Every name starts with its kind: BASS, MELODY, DRUM, EFX, DRONE, and STRESS
(sounds built to load the CPU as far as the plugin goes: four machines, the heaviest settings, every effect).
A preset is the defaults plus what it lists (unlisted keys keep their default). Knob numbers (kN) mean what the
machine's MODEL makes them mean (src/dsp/machines.h knobName). Machines 2-4 are silent (level 0) unless given one.
Run:  python3 tools/make_presets.py"""
import math, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WT, FM, WC, AN = 0, 1, 2, 3                                  # MODEL
SINE, TRI, SAW, PULSE = 0, 1, 2, 3                           # osc_w1 / osc_w2
MULTI, LP4, COMB_NEG, COMB_POS, EQ, LEG_LP, LEG_HP = range(7)  # ftype
# matrix: sources and destinations (tools/gen_params.py MOD_SRC / MOD_DST)
EG1, EG2, LFO1, LFO2, VEL, KEY, MW, AT, PB, RND, AMPENV, FLTENV = range(1, 13)
PITCH, LEVEL, PAN, CUTOFF, RESO, TA, TB, TC, TD = range(1, 10)
SINE_L, TRI_L, SAWUP, SAWDN, SQUARE, SH, SMOOTH = range(7)    # LFO shapes
UNI_SHIMMER, UNI_NOISE, UNI_POS = 1, 2, 4                     # WAVETABLE unison modes
LPG_VCA, LPG_COMBO, LPG_LP = 0, 43, 127                      # WEST COAST K11 (LPG MODE) values


def table(path): return "NorthWest/" + path + " 2048.wav"   # relative to the plugin's wavetables folder

# knob values for the ratio lists (src/dsp/machines.h fmRatio / wcRatio)
FM_HI = [1.0, 1.41, 1.5, 2.0, 2.5, 2.76, 3.0, 3.5, 4.0, 4.5, 5.0, 5.4, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0,
         15.0, 16.0]
def fm_ratio(r):
    lo = [0.25, 0.333, 0.5, 0.75]
    if r < 1.0: return int(lo.index(r) * 16 + 8)
    return 64 + math.ceil(FM_HI.index(r) * 64 / 23 + 0.01)
WC_R = [0.25, 0.333, 0.5, 0.75] + FM_HI
def wc_ratio(r):
    i = WC_R.index(r)
    return int(i * 43 / 4 + 5) if i < 4 else min(127, 43 + math.ceil((i - 4) * 84.01 / 22.99 + 0.01))
def semis(st): return int(round(64 + st * 63 / 24))           # ANALOG OSC FREQ: +-24 st, 64 = 0


def m(n, model, **kv):
    """machine n's settings: m(2, FM, level=80, k1=60) -> m2_model=1, m2_level=80, m2_k1=60"""
    d = {"m%d_model" % n: model}
    if "level" not in kv: kv["level"] = 100
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


DST8 = (TA, TB, TC, TD, CUTOFF, PITCH, PAN, RESO)

PRESETS = [
    P("Init", {"m1_model": WT}),

    # ---------------------------------------------------------------- BASS
    P("BASS Acid Ladder",
      m(1, AN, osc_w1=SAW, k3=60, k7=127, k9=0, tune=-12, ftype=LP4, cutoff=40, reso=105, fshape=70, fenv=112,
        fdec=58, amp_d=70, amp_s=40, amp_r=30, dsend=25),
      glide=40, glide_mode=3),
    P("BASS DFAM Pulse",
      m(1, AN, osc_w1=PULSE, osc_w2=SAW, k1=30, k4=43, k5=50, k6=40, k7=127, k9=80, k10=semis(7), tune=-12,
        ftype=LP4, cutoff=58, reso=70, fenv=100, fdec=55, amp_d=75, amp_s=0, amp_r=45)),
    P("BASS FM Growl",
      m(1, FM, fm_algo=0, k1=90, k2=60, k3=40, k4=30, k7=60, k8=fm_ratio(1.0), k9=fm_ratio(2.0), k10=fm_ratio(1.0),
        tune=-12, amp_s=90, ftype=LP4, cutoff=90, reso=30, xsend=30),
      m(2, AN, osc_w1=SINE, k7=127, k9=0, tune=-24, level=80, amp_s=110),
      fx_xdrive=70, fx_xchar=50),
    P("BASS West Rubber",
      m(1, WC, k1=50, k2=60, k3=20, k5=110, k6=45, k11=LPG_COMBO, tune=-12, amp_d=60, amp_s=0, amp_r=50),
      glide=30, glide_mode=3),
    P("BASS Wavetable Reese",
      m(1, WT, table="Basics/Sine to Saw", k1=90, k4=30, k7=85, k8=40, k5=80, k11=60, k12=70, tune=-12,
        ftype=MULTI, cutoff=70, reso=30, fenv=85, fdec=70, amp_s=110)),
    P("BASS Comb Bass",
      m(1, FM, fm_algo=6, k1=70, k8=fm_ratio(2.0), k7=55, tune=-12, ftype=COMB_POS, cutoff=60, fkey=127, reso=90,
        fshape=40, amp_d=70, amp_s=30, amp_r=40),
      m(2, AN, osc_w1=SINE, k7=127, k9=0, tune=-24, level=70, amp_d=70, amp_s=50)),
    P("BASS Low Gate Thump",
      m(1, WC, osc_w2=TRI, k1=30, k7=wc_ratio(0.5), k2=50, k3=30, k5=110, k6=60, k11=LPG_LP, tune=-24,
        amp_d=55, amp_s=0, amp_r=40),
      glide=20, glide_mode=2),
    P("BASS West Pluck",
      m(1, WC, k1=45, k7=wc_ratio(1.0), k2=40, k5=127, k6=30, k12=60, tune=-12, amp_d=45, amp_s=0, amp_r=40),
      m(2, AN, osc_w1=TRI, k7=127, k9=0, tune=-24, level=75, amp_d=60, amp_s=0, amp_r=40, ftype=LP4, cutoff=60)),

    # ---------------------------------------------------------------- MELODY
    P("MELODY Glass Keys",
      m(1, FM, fm_algo=6, k1=70, k8=fm_ratio(3.5), k7=75, amp_d=85, amp_s=0, amp_r=85, dsend=40, rsend=70),
      m(2, WT, table="Instruments/E-Piano Tine", level=60, amp_a=5, amp_d=90, amp_s=30, amp_r=85, rsend=60)),
    P("MELODY Sync Lead",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, osc_sync=1, k7=127, k9=127, k10=semis(12), k5=70, k6=70, k11=127,
        cutoff=100, reso=20, amp_s=110, dsend=50),
      glide=30, glide_mode=2),
    P("MELODY Vocal Pluck",
      m(1, WT, table="Vocal/Vowels Female", k1=40, amp_d=70, amp_s=20, amp_r=60, cutoff=100, rsend=50),
      mod(1, LFO1, TA, 90), lfo1_rate=40),
    P("MELODY Four Machines",
      m(1, WT, table="Basics/Triangle to Saw", k4=20, amp_d=80, amp_s=40),
      m(2, FM, fm_algo=6, k1=60, k8=fm_ratio(2.0), level=50, tune=12, amp_d=70, amp_s=0, amp_r=70),
      m(3, AN, osc_w1=PULSE, k7=127, k9=0, level=60, tune=-12, amp_d=70, amp_s=30, cutoff=80),
      m(4, WC, k1=40, k5=100, k6=40, level=40, tune=7, amp_d=60, amp_s=0, pan=90, dsend=60)),
    P("MELODY Comb Pluck",
      m(1, AN, osc_w1=SINE, k2=127, k3=50, k7=0, k9=0, ftype=COMB_POS, cutoff=64, fkey=127, reso=118, fshape=45,
        level=127, amp_d=85, amp_s=0, amp_r=85, rsend=40),
      mod(1, VEL, RESO, 80)),
    # West Coast: FM into the folder, the low pass gate as the voice
    P("MELODY Buchla Marimba",
      m(1, WC, k1=45, k7=wc_ratio(2.0), k2=35, k5=127, k6=38, k11=LPG_COMBO, k12=70, amp_d=50, amp_s=0, amp_r=50,
        rsend=40)),
    P("MELODY Bongo Ripple",
      m(1, WC, k1=55, k7=wc_ratio(1.5), k2=45, k5=127, k6=30, k12=60, pan=40, amp_d=45, amp_s=0, dsend=35),
      m(2, WC, k1=40, k7=wc_ratio(2.76), k2=30, k5=127, k6=40, tune=7, level=70, pan=88, amp_d=50, amp_s=0, dsend=35),
      mod(1, LFO1, TB, 85), lfo1_rate=25, fx_dtime=80),
    P("MELODY Fold Brass",
      m(1, WC, osc_w1=SAW, k1=60, k7=wc_ratio(1.0), k2=80, k3=40, k5=90, k6=70, amp_a=25, amp_d=90, amp_s=80,
        amp_r=60, rsend=30),
      mod(1, EG1, TB, 100, 1), me1_a=30, me1_d=80, me1_s=30),
    P("MELODY Gamelan",
      m(1, WC, k1=70, k7=wc_ratio(3.5), k2=40, k3=20, k5=127, k6=70, amp_d=90, amp_s=0, amp_r=90, rsend=60),
      m(2, FM, fm_algo=6, k1=60, k8=fm_ratio(2.76), k7=70, level=55, tune=12, amp_d=80, amp_s=0, amp_r=80,
        ftype=COMB_POS, cutoff=88, fkey=127, reso=70)),
    P("MELODY Easel Lead",
      m(1, WC, osc_sync=1, k1=50, k7=wc_ratio(2.0), k2=55, k3=25, k5=80, k6=60, amp_s=110, amp_r=60, dsend=40),
      mod(1, LFO1, PITCH, 66), mod(2, EG2, TB, 95), lfo1_rate=70, me2_d=70, glide=30, glide_mode=2),

    # ---------------------------------------------------------------- DRUM
    P("DRUM FM Kick",
      m(1, FM, fm_mode=1, fm_algo=7, k5=55, k11=38, k6=18, amp_d=68, tune=-24, vel=60)),
    P("DRUM Analog Kick",
      m(1, AN, osc_w1=SINE, k3=40, k5=90, k6=35, k7=127, k9=0, tune=-24, amp_d=70, amp_s=0, amp_r=60)),
    P("DRUM Dirt Kick",
      m(1, AN, osc_w1=SINE, k3=60, k5=100, k6=30, k7=127, k9=0, tune=-24, amp_d=72, amp_s=0, amp_r=60, xsend=110),
      fx_xdrive=100, fx_xchar=60, fx_xtone=50, fx_xgrit=20),
    P("DRUM Snare",
      m(1, FM, fm_mode=1, fm_algo=6, k1=75, k8=fm_ratio(1.41), k7=40, k5=25, k11=30, k6=95, amp_d=55, cutoff=110),
      m(2, AN, osc_w1=TRI, k2=127, k7=0, k9=0, ftype=LEG_HP, cutoff=80, amp_d=55, amp_s=0, amp_r=45, level=90)),
    P("DRUM Metal Hat",
      m(1, FM, fm_mode=1, fm_algo=0, k1=110, k2=100, k3=90, k4=40, k8=fm_ratio(5.4), k9=fm_ratio(9.0),
        k10=fm_ratio(13.0), k7=30, k6=60, amp_d=35, ftype=LEG_HP, fshape=127, cutoff=100, tune=12)),
    P("DRUM West Tom",
      m(1, WC, k1=50, k7=wc_ratio(2.0), k2=40, k5=127, k6=20, tune=-5, amp_d=45, amp_s=0, amp_r=40, rsend=30)),
    P("DRUM Woodblock",
      m(1, WC, k1=35, k7=wc_ratio(1.41), k2=25, k5=110, k6=18, tune=12, amp_d=42, amp_s=0, amp_r=30,
        ftype=LEG_HP, cutoff=35)),
    P("DRUM Perc Clank",
      m(1, FM, fm_mode=1, fm_algo=4, k1=80, k2=70, k3=60, k8=fm_ratio(1.41), k9=fm_ratio(2.76), k10=fm_ratio(7.0),
        k7=35, k11=25, k5=20, amp_d=50, rsend=40)),

    # ---------------------------------------------------------------- EFX
    P("EFX Laser Zap",
      m(1, AN, osc_w1=SAW, k5=127, k6=55, k7=127, k9=0, amp_d=60, amp_s=0, amp_r=50, cutoff=90, reso=70, dsend=70),
      fx_dfb=80),
    P("EFX Noise Riser",
      m(1, AN, osc_w1=SINE, k2=127, k7=0, k9=0, ftype=MULTI, fshape=64, cutoff=40, reso=100, amp_a=110, amp_s=127,
        amp_r=90, rsend=90),
      mod(1, EG1, CUTOFF, 127), me1_a=115, me1_s=127),
    P("EFX Ring Robot",
      m(1, WC, k1=127, k7=wc_ratio(2.76), k2=90, k5=0, k11=LPG_VCA, amp_s=127, dsend=40),
      mod(1, LFO1, TB, 110), lfo1_shape=SH, lfo1_rate=90),
    P("EFX Glitch Steps",
      m(1, WT, table="Complex/Digital Steps", wt_fx=2, k2=90, amp_d=50, amp_s=0, dsend=60, xsend=60),
      mod(1, LFO2, TA, 127), lfo2_shape=SH, lfo2_rate=95, fx_xgrit=100),
    P("EFX Pinged Chaos",
      m(1, WC, k1=100, k7=wc_ratio(2.76), k2=100, k3=60, k5=110, k6=45, amp_d=55, amp_s=0, dsend=80),
      mod(1, LFO1, TA, 120), mod(2, LFO2, TC, 100), lfo1_shape=SH, lfo1_rate=95, lfo2_shape=SMOOTH, lfo2_rate=70,
      fx_dfb=95, fx_dtime=75),

    # ---------------------------------------------------------------- DRONE
    P("DRONE Dark Fifths",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=100, k10=semis(7), k12=70, tune=-24, amp_a=100, amp_s=127,
        amp_r=120, ftype=LP4, cutoff=50, reso=60, rsend=80),
      m(2, WT, table="Sound Design/Hollow Pad", level=60, k7=127, k4=25, amp_a=100, amp_s=127, amp_r=120, rsend=80),
      mod(1, LFO1, CUTOFF, 80, 1), mod(2, LFO2, TA, 100, 2), lfo1_rate=15, lfo2_rate=10, lfo2_shape=SMOOTH),
    P("DRONE Buchla Bells",
      m(1, WC, level=80, k1=40, k2=50, k3=30, k5=30, k6=127, amp_a=90, amp_s=127, amp_r=120, rsend=70, dsend=30),
      m(2, FM, fm_algo=6, k1=50, k8=fm_ratio(3.5), level=35, tune=12, amp_a=100, amp_s=127, amp_r=120, rsend=70),
      mod(1, LFO1, TB, 90, 1), mod(2, LFO2, TA, 80, 1), lfo1_rate=20, lfo2_rate=10, lfo2_shape=SMOOTH,
      fx_rsize=110),
    P("DRONE Vowel Choir",
      m(1, WT, table="Vocal/Choir Ah to Oo", k7=127, k4=30, k8=127, wt_umode=UNI_SHIMMER, amp_a=110, amp_s=127,
        amp_r=120, rsend=100),
      m(2, WT, table="Vocal/Vowels Male", level=70, tune=-12, amp_a=110, amp_s=127, amp_r=120, rsend=100),
      mod(1, LFO1, TA, 100), lfo1_rate=8, fx_rsize=110),
    P("DRONE Serge Sea",
      m(1, WC, k1=50, k7=wc_ratio(1.5), k2=60, k3=40, k5=60, k6=127, amp_a=100, amp_s=127, amp_r=127, pan=30,
        rsend=100, dsend=50),
      m(2, WC, k1=45, k7=wc_ratio(2.0), k2=50, k3=60, k5=60, k6=127, tune=-12, level=85, amp_a=110, amp_s=127,
        amp_r=127, pan=98, rsend=100, dsend=50),
      mod(1, LFO2, TB, 110), mod(2, LFO1, TA, 90), mod(3, RND, TC, 100), lfo1_rate=12, lfo2_shape=SMOOTH,
      lfo2_rate=18, fx_rsize=120, fx_dtime=95, fx_dfb=70, drift=60),

    # ---------------------------------------------------------------- STRESS: the heaviest the plugin can be
    P("STRESS Unison Wall",
      *[m(n, WT, table=t, k7=127, k4=50, k8=127, k1=70, wt_fx=1, k2=90, k3=60, wt_umode=UNI_SHIMMER, k5=80, k6=80,
          ftype=LP4, cutoff=80, reso=80, fshape=100, amp_s=127, amp_r=127, dsend=80, rsend=80, xsend=60,
          pan=20 + 28 * n) for n, t in ((1, "Basics/Sine to Saw"), (2, "Complex/Metallic"),
                                       (3, "Vocal/Choir Ah to Oo"), (4, "Sound Design/Growl"))],
      *[mod(r, LFO1 if r % 2 else LFO2, DST8[r - 1], 100) for r in range(1, 9)],
      lfo1_rate=110, lfo2_rate=100, lfo2_shape=SH, drift=127),
    P("STRESS West Fold",
      *[m(n, WC, k1=127, k7=wc_ratio((1.41, 2.76, 3.5, 5.4)[n - 1]), k2=127, k3=80, k5=127, k6=127, osc_sync=1,
          osc_w1=SAW, osc_w2=SAW, ftype=COMB_POS, cutoff=90, fkey=127, reso=110, amp_s=127, amp_r=127,
          dsend=90, rsend=90, xsend=90) for n in range(1, 5)],
      *[mod(r, (LFO1, LFO2, RND, EG1)[r % 4], DST8[r - 1], 110) for r in range(1, 9)],
      lfo1_rate=120, lfo2_rate=115, oversample=1, fx_xgrit=127, fx_dfb=100),
    P("STRESS FM Feedback",
      *[m(n, FM, fm_algo=n - 1, k1=127, k2=127, k3=127, k4=127, k7=127, k8=fm_ratio(3.5), k9=fm_ratio(7.0),
          k10=fm_ratio(13.0), k6=60, ftype=MULTI, fshape=64, cutoff=90, reso=110, amp_s=127, amp_r=127,
          dsend=80, rsend=80, xsend=80) for n in range(1, 5)],
      *[mod(r, LFO1, DST8[r - 1], 120) for r in range(1, 9)],
      lfo1_rate=127, oversample=1),
    P("STRESS Analog Sync",
      *[m(n, AN, osc_w1=SAW, osc_w2=PULSE, osc_sync=1, k1=127, k2=100, k3=127, k4=80, k5=127, k6=90, k7=127, k9=127,
          k10=semis(19), ftype=LP4, cutoff=70, reso=127, fshape=127, amp_s=127, amp_r=127, dsend=80, rsend=80,
          xsend=80) for n in range(1, 5)],
      *[mod(r, LFO2, DST8[r - 1], 110) for r in range(1, 9)],
      lfo2_rate=120, lfo2_shape=SH, oversample=1, drift=127),
    P("STRESS Everything",
      m(1, WT, table="Complex/Spectral Fold", k7=127, k4=60, k8=127, wt_fx=3, k2=100, k3=100, k5=100, k6=100,
        ftype=COMB_POS, cutoff=80, fkey=127, reso=110, amp_s=127, amp_r=127, dsend=100, rsend=100, xsend=100),
      m(2, FM, fm_algo=0, k1=127, k2=127, k3=127, k4=127, k7=127, k8=fm_ratio(2.76), k9=fm_ratio(5.4),
        k10=fm_ratio(11.0), ftype=LP4, cutoff=80, reso=120, fshape=127, amp_s=127, amp_r=127, dsend=100,
        rsend=100, xsend=100),
      m(3, WC, k1=127, k7=wc_ratio(3.5), k2=127, k3=90, k5=127, k6=127, osc_sync=1, ftype=MULTI, fshape=64,
        cutoff=80, reso=110, amp_s=127, amp_r=127, dsend=100, rsend=100, xsend=100),
      m(4, AN, osc_w1=SAW, osc_w2=SAW, osc_sync=1, k1=127, k2=127, k3=127, k5=127, k6=100, k7=127, k9=127,
        k10=semis(12), ftype=EQ, fshape=127, cutoff=80, reso=100, bwlo=20, bwhi=110, amp_s=127, amp_r=127,
        dsend=100, rsend=100, xsend=100),
      *[mod(r, (LFO1, LFO2, RND, EG1, EG2, AMPENV, FLTENV, VEL)[r - 1], DST8[r - 1], 120) for r in range(1, 9)],
      lfo1_rate=127, lfo2_rate=120, lfo2_shape=SH, oversample=1, drift=127, fx_xdrive=127, fx_xgrit=127,
      fx_dfb=110, fx_rsize=127),
]

if __name__ == "__main__":
    lines = ["# NorthWest factory sounds: Name|NW1;key=value;...  Written by tools/make_presets.py -- edit that.",
             "# Kinds: BASS, MELODY, DRUM, EFX, DRONE, STRESS (CPU tests). Wavetables: the plugin's NorthWest pack."]
    for name, d in PRESETS:
        lines.append(name + "|NW1;" + "".join("%s=%s;" % (k, v) for k, v in d.items()))
    open(os.path.join(ROOT, "presets/northwest.tbl"), "w").write("\n".join(lines) + "\n")
    print("presets/northwest.tbl: %d presets" % len(PRESETS))
