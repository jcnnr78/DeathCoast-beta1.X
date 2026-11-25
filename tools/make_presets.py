#!/usr/bin/env python3
"""DeathCoast factory presets (presets/deathcoast.tbl), written from the readable list below.

The 1.7 library. Every name starts with its kind: INIT, BASS, LEAD, KEYS, PAD, DRUM, EFX, DRONE, BUCHLA (west coast
at full stretch), MOOG (ladder and fat oscillators), and STRESS (sounds built to load the CPU as far as the plugin
goes: four machines, the heaviest settings, every effect). All four machines answer the track's notes unless a
preset gives them a MIDI channel (the KIT presets: one machine per channel).
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


def table(path): return "DeathCoast/" + path + " 2048.wav"   # relative to the plugin's wavetables folder

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


# delay SYNC note values (src/dsp/fx.h delayDivName) and LFO SYNC divisions (src/dsp/lfo.h, 12 steps)
DIVS = ["1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1/1"]
def div(name): return round(DIVS.index(name) * 127 / 13)
LDIVS = ["2 BAR", "1 BAR", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"]
def ldiv(name): return round(LDIVS.index(name) * 127 / 11)
OS_OFF, OS_AUTO, OS_ON = 0, 1, 2                              # oversample
STEREO, PINGPONG = 0, 1                                      # fx_dmode
# the effects, as sets to start from (each preset changes what it wants on top)
def dly(dtime=None, sync=True, note="1/8D", fb=50, tone=50, drive=20, mod=10, duck=0, mode=PINGPONG, level=100, rev=0):
    d = dict(fx_dsync=int(sync), fx_dtime=div(note) if sync else dtime, fx_dfb=fb, fx_dtone=tone, fx_ddrive=drive,
             fx_dmod=mod, fx_dduck=duck, fx_dmode=mode, fx_dlevel=level, fx_drev=rev)
    return d
def rev(decay=64, size=70, pre=0, tone=64, mod=40, level=100):
    return dict(fx_rdecay=decay, fx_rsize=size, fx_rpre=pre, fx_rdamp=tone, fx_rmod=mod, fx_rlevel=level)
def dirt(drive=64, char=40, tone=64, grit=0): return dict(fx_xdrive=drive, fx_xchar=char, fx_xtone=tone, fx_xgrit=grit)
def fx(*parts):
    d = {}
    for p in parts: d.update(p)
    return d

PRESETS = [
    # ================================================================ INIT: a clean start per machine, and all four
    P("INIT Wavetable", m(1, WT, table="Basics/Sine to Saw", k7=0)),
    P("INIT FM", m(1, FM, fm_algo=0, k1=60, k8=fm_ratio(1.0))),
    P("INIT West Coast", m(1, WC, k1=40, k2=40, k5=100, k6=50, amp_s=0, amp_d=70, amp_r=50)),
    P("INIT Analog", m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=0, ftype=LP4, cutoff=100)),
    P("INIT Four Machines",
      m(1, WT, table="Basics/Sine to Saw", k7=0, level=70, pan=40),
      m(2, FM, k1=50, level=70, pan=88),
      m(3, WC, k1=30, k2=30, k5=90, k6=50, level=70, pan=52, amp_s=0, amp_d=70),
      m(4, AN, osc_w1=SAW, k7=127, k9=0, level=70, pan=76, ftype=LP4, cutoff=90)),

    # ================================================================ BASS
    P("BASS Rubber Gate",                        # west coast bass, the LPG plucks it, analog sub under it
      m(1, WC, osc_w2=TRI, k1=55, k7=wc_ratio(1.0), k2=55, k3=30, k5=120, k6=40, k11=LPG_COMBO, k12=50,
        tune=-12, amp_d=60, amp_s=0, amp_r=45, xsend=20),
      m(2, AN, osc_w1=SINE, k7=127, k9=0, tune=-24, level=80, amp_d=70, amp_s=40, amp_r=40),
      fx(dirt(55, 20, 55, 0), rev(40, 30, 0, 50, 20, 60)), glide=25, glide_mode=3),
    P("BASS Acid Squelch",                       # 303-ish: saw into a screaming LP 4, legato slides
      m(1, AN, osc_w1=SAW, k3=50, k7=127, k9=0, tune=-12, ftype=LP4, cutoff=35, reso=110, fshape=80, fenv=118,
        fdec=55, amp_d=72, amp_s=55, amp_r=30, dsend=30, xsend=35),
      fx(dirt(70, 30, 60, 0), dly(note="1/8D", fb=45, tone=40, drive=40, duck=70)), glide=35, glide_mode=3),
    P("BASS FM Sub Punch",
      m(1, FM, fm_algo=0, k1=85, k2=55, k3=30, k4=20, k7=55, k8=fm_ratio(1.0), k9=fm_ratio(2.0),
        k10=fm_ratio(1.0), tune=-12, amp_d=70, amp_s=80, ftype=LP4, cutoff=80, reso=20),
      m(2, WT, table="Basics/Sine to Square", k1=10, k7=0, k5=100, k11=0, k12=64, tune=-12, level=70,
        amp_s=100),
      fx(dirt(60, 10, 50, 0))),
    P("BASS Reese Drift",                        # wavetable detune + slow cutoff, a little dirt
      m(1, WT, table="Basics/Sine to Saw", k1=100, k4=35, k7=85, k8=50, k5=70, k11=60, tune=-12, ftype=MULTI,
        cutoff=60, reso=40, fshape=0, amp_s=115, xsend=30),
      mod(1, LFO1, CUTOFF, 82, 1), lfo1_sync=1, lfo1_rate=ldiv("1 BAR"), drift=40,
      **fx(dirt(70, 50, 45, 10))),
    P("BASS Comb Pluck",
      m(1, FM, fm_algo=6, k1=70, k8=fm_ratio(2.0), k7=50, tune=-12, ftype=COMB_POS, cutoff=60, fkey=127, reso=95,
        fshape=40, amp_d=65, amp_s=20, amp_r=40),
      m(2, AN, osc_w1=SINE, k7=127, k9=0, tune=-24, level=75, amp_d=70, amp_s=40),
      fx(rev(50, 40, 10, 50, 30, 70))),
    P("BASS Growl Table",
      m(1, WT, table="Sound Design/Growl", k1=30, k7=43, k8=30, tune=-12, ftype=LP4, cutoff=75, reso=50,
        fshape=60, amp_s=110, xsend=60),
      mod(1, LFO1, TA, 110, 1), lfo1_sync=1, lfo1_rate=ldiv("1/8T"), lfo1_shape=TRI_L,
      **fx(dirt(90, 70, 55, 20))),
    P("BASS Dub Sine",                           # deep sine, tape delay in sync, ducked
      m(1, AN, osc_w1=SINE, k3=30, k5=30, k6=30, k7=127, k9=0, tune=-24, amp_d=80, amp_s=60, amp_r=60,
        dsend=55),
      m(2, WC, osc_w2=SINE, k1=20, k7=wc_ratio(2.0), k2=25, k5=127, k6=30, tune=-12, level=45, amp_d=50,
        amp_s=0, dsend=70),
      fx(dly(note="1/4D", fb=70, tone=35, drive=55, mod=35, duck=80, mode=STEREO), rev(70, 60, 20, 40, 50, 60))),
    P("BASS Sync Snarl",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, osc_sync=1, k7=0, k9=127, k10=semis(7), k5=90, k6=40, k11=127, tune=-12,
        ftype=LP4, cutoff=80, reso=40, fshape=90, amp_s=100, xsend=50),
      m(3, FM, fm_algo=6, k1=40, k8=fm_ratio(0.5), tune=-12, level=60, amp_s=100),
      fx(dirt(80, 64, 60, 0)), glide=20, glide_mode=3),

    # ================================================================ LEAD
    P("LEAD Sync Scream",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, osc_sync=1, k7=127, k9=127, k10=semis(12), k5=80, k6=60, k11=127,
        cutoff=110, reso=25, amp_s=110, dsend=50, rsend=25),
      mod(1, LFO1, PITCH, 67), lfo1_rate=72, lfo1_fade=60,
      **fx(dly(note="1/8D", fb=55, tone=55, mod=20), rev(70, 70, 25, 60, 40, 70))),
    P("LEAD Easel Whistle",                      # Buchla Easel-style: sync into the folder, vibrato
      m(1, WC, osc_sync=1, k1=50, k7=wc_ratio(2.0), k2=60, k3=25, k5=80, k6=60, amp_s=110, amp_r=60, dsend=45,
        rsend=35),
      mod(1, LFO1, PITCH, 66), mod(2, EG2, TB, 95), lfo1_rate=70, me2_d=70,
      glide=30, glide_mode=2, **fx(dly(note="1/4T", fb=50, tone=60, drive=25, mod=30), rev(75, 80, 30))),
    P("LEAD FM Brass",
      m(1, FM, fm_algo=0, k1=70, k2=40, k3=0, k8=fm_ratio(1.0), k9=fm_ratio(1.0), k7=70, k12=80, amp_a=12,
        amp_s=100, amp_r=55, ftype=LP4, cutoff=95, rsend=35),
      m(2, WT, table="Instruments/Brass Swell", k1=50, k7=43, k8=40, level=55, amp_a=20, amp_s=100),
      mod(1, EG1, TA, 100, 1), me1_a=20, me1_d=70, me1_s=60, **fx(rev(70, 70, 20, 55, 30, 80))),
    P("LEAD Glass Mono",
      m(1, WT, table="Harmonic/Bell Partials", k1=60, wt_fx=1, k2=40, k3=fm_ratio(2.0), k7=0, amp_s=100, dsend=60),
      m(2, FM, fm_algo=6, k1=45, k8=fm_ratio(3.5), tune=12, level=40, amp_d=70, amp_s=30),
      mod(1, LFO2, TA, 90, 1), lfo2_rate=30, lfo2_shape=SMOOTH,
      glide=20, glide_mode=2, **fx(dly(note="1/16", fb=60, tone=70, mod=15), rev(80, 90, 40, 70, 60, 70))),
    P("LEAD Dirty Square",
      m(1, AN, osc_w1=PULSE, osc_w2=PULSE, k4=40, k7=127, k9=90, k12=75, cutoff=90, reso=30, amp_s=110, xsend=80,
        dsend=30),
      mod(1, LFO1, TD, 85, 1), lfo1_rate=45, **fx(dirt(95, 64, 60, 30), dly(note="1/8", fb=40))),

    # ================================================================ KEYS
    P("KEYS Glass Piano",
      m(1, FM, fm_algo=6, k1=70, k8=fm_ratio(3.5), k7=75, k12=70, amp_d=85, amp_s=0, amp_r=85, dsend=30,
        rsend=60),
      m(2, WT, table="Instruments/E-Piano Tine", level=60, k7=0, amp_a=3, amp_d=90, amp_s=25, amp_r=85, rsend=60),
      fx(dly(note="1/4", fb=35, tone=40, mod=25), rev(80, 75, 20, 55, 50, 80))),
    P("KEYS Wood Mallets",
      m(1, WC, k1=45, k7=wc_ratio(2.0), k2=35, k5=127, k6=35, k11=LPG_COMBO, k12=70, amp_d=50, amp_s=0,
        amp_r=50, rsend=40, pan=50, level=127),
      m(2, FM, fm_algo=6, k1=55, k8=fm_ratio(4.0), k7=35, tune=12, level=60, amp_d=50, amp_s=0, pan=78),
      fx(rev(55, 50, 10, 60, 30, 80))),
    P("KEYS Organ Drift",
      m(1, WT, table="Harmonic/Organ Drawbars", k1=40, k7=43, k8=60, k4=10, amp_s=127, amp_r=40, rsend=50),
      m(2, AN, osc_w1=SINE, k7=127, k9=0, tune=-12, level=55, amp_s=127),
      mod(1, LFO1, TA, 76, 1), lfo1_rate=18, drift=50, **fx(rev(70, 60, 10, 45, 60, 70), dirt(40, 0, 50, 0))),
    P("KEYS Clav Funk",
      m(1, WT, table="Instruments/Clav", k1=30, k7=0, level=127, ftype=MULTI, fshape=30, cutoff=85, reso=60, fenv=105,
        fdec=40, amp_d=60, amp_s=30, amp_r=30, xsend=40),
      fx(dirt(60, 30, 70, 0), dly(note="1/16D", fb=30, tone=70, level=70))),
    P("KEYS Tine Chorus",
      m(1, FM, fm_algo=3, k1=60, k3=50, k8=fm_ratio(1.0), k10=fm_ratio(14.0), k7=60, amp_d=90, amp_s=30,
        amp_r=80, pan=30, rsend=45),
      m(2, FM, fm_algo=6, k1=50, k8=fm_ratio(1.0), op1=100, k14=fm_ratio(1.0), tune=0, level=60, amp_d=90,
        amp_s=30, amp_r=80, pan=98, rsend=45),
      mod(1, LFO1, PITCH, 65, 2), lfo1_rate=35, **fx(rev(70, 60, 15, 60, 80, 70))),

    # ================================================================ PAD
    P("PAD Choir Cathedral",
      m(1, WT, table="Vocal/Choir Ah to Oo", k7=127, k4=30, k8=127, wt_umode=UNI_SHIMMER, amp_a=100, amp_s=127,
        amp_r=110, rsend=100),
      m(2, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=110, k12=72, tune=-12, level=50, ftype=LP4, cutoff=55,
        amp_a=110, amp_s=127, amp_r=110, rsend=90),
      mod(1, LFO1, TA, 100, 1), lfo1_rate=8, **fx(rev(108, 120, 40, 50, 70, 90))),
    P("PAD Frozen Glass",                        # a 16 s tail: turn DECAY to the top (FREEZE) and it holds
      m(1, FM, fm_algo=6, k1=55, k8=fm_ratio(3.5), k7=80, amp_a=40, amp_d=90, amp_s=50, amp_r=90, rsend=110),
      m(2, WT, table="Harmonic/Spectral Walk", k1=50, k7=85, k8=90, level=60, amp_a=80, amp_s=110, amp_r=100,
        rsend=110),
      mod(1, LFO2, TA, 95, 2), lfo2_rate=12, lfo2_shape=SMOOTH, **fx(rev(122, 110, 20, 60, 90, 90))),
    P("PAD Warm Strings",
      m(1, WT, table="Instruments/Bowed String", k1=60, k7=127, k4=25, k8=110, amp_a=85, amp_s=120, amp_r=100, level=125,
        ftype=LP4, cutoff=85, rsend=70),
      m(3, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=120, k12=70, level=45, ftype=LP4, cutoff=70, amp_a=95,
        amp_s=120, amp_r=100, rsend=70),
      mod(1, LFO1, TA, 85, 1), lfo1_rate=10, drift=60, **fx(rev(95, 100, 30, 50, 60, 80))),
    P("PAD West Shimmer",
      m(1, WC, osc_w2=TRI, k1=35, k7=wc_ratio(2.0), k2=45, k3=40, k5=50, k6=127, amp_a=100, amp_s=127,
        amp_r=120, rsend=100, dsend=40, pan=35),
      m(2, WT, table="Harmonic/Moving Peak", k7=127, k8=127, wt_umode=UNI_SHIMMER, tune=12, level=45, amp_a=110,
        amp_s=127, amp_r=120, rsend=100, pan=92),
      mod(1, LFO1, TB, 90, 1), mod(2, LFO2, TA, 100, 2), lfo1_rate=14, lfo2_rate=9, lfo2_shape=SMOOTH,
      **fx(dly(note="1/2", fb=60, tone=60, mod=50, rev=70), rev(110, 110, 30, 65, 100, 90))),

    # ================================================================ DRUM (one sound a preset) and KIT
    P("DRUM Deep Kick",
      m(1, FM, fm_mode=1, fm_algo=7, k5=60, k11=38, k6=15, amp_d=70, tune=-24, vel=60),
      m(2, AN, osc_w1=SINE, k3=50, k5=80, k6=30, k7=127, k9=0, tune=-24, level=55, amp_d=72, amp_s=0, xsend=60),
      fx(dirt(70, 15, 45, 0))),
    P("DRUM Gated Snare",
      m(1, FM, fm_mode=1, fm_algo=6, k1=75, k8=fm_ratio(1.41), k7=40, k5=25, k11=30, k6=95, amp_d=55,
        cutoff=110, rsend=70),
      m(2, AN, osc_w1=TRI, k2=127, k7=0, k9=0, ftype=LEG_HP, cutoff=80, amp_d=55, amp_s=0, amp_r=45, level=85,
        rsend=80),
      fx(rev(40, 60, 0, 70, 20, 100))),
    P("DRUM Metal Hat",
      m(1, FM, fm_mode=1, fm_algo=0, k1=110, k2=100, k3=90, k4=40, k8=fm_ratio(5.4), k9=fm_ratio(9.0),
        k10=fm_ratio(13.0), k7=30, k6=60, amp_d=35, ftype=LEG_HP, fshape=127, cutoff=100, tune=12, dsend=25),
      fx(dly(note="1/16", fb=30, tone=90, level=60))),
    P("DRUM Buchla Tom",
      m(1, WC, k1=50, k7=wc_ratio(2.0), k2=40, k5=127, k6=20, tune=-5, amp_d=45, amp_s=0, amp_r=40, rsend=35),
      fx(rev(45, 50, 0, 50, 30, 80))),
    P("DRUM Clap Room",
      m(1, AN, osc_w1=SINE, k2=127, k3=60, k7=0, k9=0, ftype=MULTI, fshape=56, cutoff=100, reso=25, amp_d=48, amp_s=0,
        amp_r=40, level=127, rsend=90),
      **fx(rev(45, 40, 5, 60, 20, 110))),
    P("DRUM Dirt Rim",
      m(1, WC, k1=35, k7=wc_ratio(1.41), k2=25, k5=110, k6=18, tune=12, amp_d=42, amp_s=0, amp_r=30,
        ftype=LEG_HP, cutoff=35, xsend=90),
      fx(dirt(100, 90, 70, 40))),
    P("KIT Four Pads",                           # one machine per MIDI channel: sequence each from its own track
      m(1, FM, chan=1, fm_mode=1, fm_algo=7, k5=60, k11=38, k6=15, amp_d=70, tune=-24, vel=60),
      m(2, AN, chan=2, osc_w1=TRI, k2=127, k7=0, k9=0, ftype=LEG_HP, cutoff=80, amp_d=55, amp_s=0, amp_r=45,
        level=85, rsend=50),
      m(3, FM, chan=3, fm_mode=1, fm_algo=0, k1=110, k2=100, k3=90, k4=40, k8=fm_ratio(5.4), k9=fm_ratio(9.0),
        k10=fm_ratio(13.0), k7=30, k6=60, amp_d=35, ftype=LEG_HP, fshape=127, cutoff=100, tune=12, level=70),
      m(4, WC, chan=4, k1=50, k7=wc_ratio(2.0), k2=40, k5=127, k6=20, tune=-5, amp_d=45, amp_s=0, amp_r=40,
        rsend=40),
      fx(rev(45, 50, 0, 60, 20, 80))),

    # ================================================================ EFX
    P("EFX Laser Rain",
      m(1, AN, osc_w1=SAW, k5=127, k6=55, k7=127, k9=0, amp_d=60, amp_s=0, amp_r=50, cutoff=90, reso=70, level=127,
        dsend=80),
      fx(dly(note="1/16T", fb=80, tone=70, drive=40, mod=30, rev=60), rev(80, 90, 0, 70, 60, 70))),
    P("EFX Noise Riser",
      m(1, AN, osc_w1=SINE, k2=127, k7=0, k9=0, ftype=MULTI, fshape=64, cutoff=55, reso=90, amp_a=80, level=127,
        amp_s=127, amp_r=90, rsend=90),
      mod(1, EG1, CUTOFF, 127), me1_a=100, me1_s=127, **fx(rev(100, 110, 60, 75, 70, 90))),
    P("EFX Robot Ring",
      m(1, WC, k1=127, k7=wc_ratio(2.76), k2=90, k5=0, k11=LPG_VCA, amp_s=127, dsend=40),
      mod(1, LFO1, TB, 110), lfo1_shape=SH, lfo1_sync=1, lfo1_rate=ldiv("1/16"),
      **fx(dly(note="1/8T", fb=50, tone=70))),
    P("EFX Glitch Steps",
      m(1, WT, table="Complex/Digital Steps", wt_fx=2, k2=90, k7=0, amp_d=50, amp_s=0, dsend=60, xsend=70),
      mod(1, LFO2, TA, 127), lfo2_shape=SH, lfo2_sync=1, lfo2_rate=ldiv("1/16"),
      **fx(dirt(80, 100, 70, 110), dly(note="1/16D", fb=60, tone=75, drive=60))),
    P("EFX Tape Feedback",                       # FEEDBACK past 100 %: the delay plays itself, DUCK lets you in
      m(1, FM, fm_algo=6, k1=60, k8=fm_ratio(2.76), k7=30, amp_d=40, amp_s=0, dsend=110),
      fx(dly(note="1/4D", fb=122, tone=40, drive=80, mod=60, duck=60, rev=50), rev(90, 90, 10, 45, 60, 70))),
    P("EFX Space Station",
      m(1, WT, table="Vocal/Alien Formants", k1=60, k7=85, k8=100, wt_fx=4, k2=70, amp_a=50, amp_s=110,
        amp_r=100, rsend=110, dsend=60),
      m(2, AN, osc_w1=SINE, k2=90, k7=60, k9=0, ftype=COMB_NEG, cutoff=70, reso=100, fkey=100, level=50,
        amp_a=60, amp_s=110, amp_r=100, rsend=100),
      mod(1, LFO1, TA, 120, 1), mod(2, RND, CUTOFF, 100, 2), lfo1_rate=20, lfo1_shape=SMOOTH,
      **fx(dly(note="1/2T", fb=75, mod=80, rev=90), rev(118, 120, 50, 60, 127, 90))),

    # ================================================================ DRONE
    P("DRONE Dark Fifths",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=100, k10=semis(7), k12=70, tune=-24, level=127, amp_a=100, amp_s=127,
        amp_r=120, ftype=LP4, cutoff=50, reso=60, rsend=80),
      m(2, WT, table="Sound Design/Hollow Pad", level=60, k7=127, k4=25, amp_a=100, amp_s=127, amp_r=120,
        rsend=80),
      mod(1, LFO1, CUTOFF, 80, 1), mod(2, LFO2, TA, 100, 2), lfo1_rate=15, lfo2_rate=10, lfo2_shape=SMOOTH,
      drift=70, **fx(rev(110, 110, 30, 45, 70, 80))),
    P("DRONE Serge Sea",
      m(1, WC, k1=50, k7=wc_ratio(1.5), k2=60, k3=40, k5=60, k6=127, amp_a=100, amp_s=127, amp_r=127, pan=30,
        rsend=100, dsend=50),
      m(2, FM, fm_algo=4, k1=40, k2=30, k3=35, k8=fm_ratio(1.5), k9=fm_ratio(2.0), k10=fm_ratio(3.0), k7=127,
        tune=-12, level=60, amp_a=110, amp_s=127, amp_r=127, pan=98, rsend=100, dsend=50),
      mod(1, LFO2, TB, 110, 1), mod(2, LFO1, TA, 90, 2), mod(3, RND, TC, 100), lfo1_rate=12,
      lfo2_shape=SMOOTH, lfo2_rate=18, drift=60,
      **fx(dly(note="1/2D", fb=70, tone=40, mod=70, rev=80), rev(115, 120, 40, 55, 100, 90))),
    P("DRONE Iron Bells",
      m(1, FM, fm_algo=6, k1=50, k8=fm_ratio(5.4), k14=fm_ratio(0.5), k7=127, amp_a=90, amp_s=127, amp_r=120,
        rsend=90),
      m(2, WC, k1=30, k7=wc_ratio(2.76), k2=50, k3=30, k5=30, k6=127, tune=12, level=50, amp_a=100, amp_s=127,
        amp_r=120, rsend=90),
      mod(1, LFO1, TA, 90, 1), mod(2, LFO2, TB, 85, 2), lfo1_rate=20, lfo2_rate=8, lfo2_shape=SMOOTH,
      **fx(rev(127, 110, 20, 55, 90, 80))),
    P("DRONE Rust Engine",
      m(1, AN, osc_w1=PULSE, osc_w2=SAW, k1=60, k2=30, k3=90, k4=60, k7=127, k9=110, k10=semis(-12), tune=-24,
        amp_a=90, amp_s=127, amp_r=120, ftype=LP4, cutoff=60, reso=70, fshape=100, xsend=80, rsend=60),
      m(3, WT, table="Complex/Noisy Cycles", k1=60, k7=85, k8=100, level=40, amp_a=100, amp_s=127, amp_r=120,
        xsend=70),
      mod(1, LFO1, TA, 100, 1), mod(2, RND, CUTOFF, 90, 1), lfo1_rate=6, drift=100,
      **fx(dirt(90, 75, 40, 60), rev(105, 100, 20, 35, 60, 80))),

    # ================================================================ BUCHLA: west coast at full stretch
    P("BUCHLA Bongo Ripple",
      m(1, WC, k1=55, k7=wc_ratio(1.5), k2=45, k5=127, k6=30, k12=60, pan=40, amp_d=45, amp_s=0, dsend=40),
      m(2, WC, k1=40, k7=wc_ratio(2.76), k2=30, k5=127, k6=40, tune=7, level=70, pan=88, amp_d=50, amp_s=0,
        dsend=40),
      mod(1, LFO1, TB, 85), lfo1_sync=1, lfo1_rate=ldiv("1/8T"),
      **fx(dly(note="1/8D", fb=55, tone=55, mod=20), rev(60, 60, 10, 60, 40, 70))),
    P("BUCHLA Fold Brass",
      m(1, WC, osc_w1=SAW, k1=60, k7=wc_ratio(1.0), k2=80, k3=40, k5=90, k6=70, amp_a=25, amp_d=90, amp_s=80,
        amp_r=60, rsend=30),
      m(2, FM, fm_algo=0, k1=40, k8=fm_ratio(1.0), tune=-12, level=40, amp_a=20, amp_s=80),
      mod(1, EG1, TB, 100, 1), me1_a=30, me1_d=80, me1_s=30, **fx(rev(65, 70, 15, 55, 30, 70))),
    P("BUCHLA Gamelan",
      m(1, WC, k1=70, k7=wc_ratio(3.5), k2=40, k3=20, k5=127, k6=70, amp_d=90, amp_s=0, amp_r=90, rsend=60),
      m(2, FM, fm_algo=6, k1=60, k8=fm_ratio(2.76), k7=70, level=55, tune=12, amp_d=80, amp_s=0, amp_r=80,
        ftype=COMB_POS, cutoff=88, fkey=127, reso=70),
      fx(rev(90, 90, 20, 60, 60, 80))),
    P("BUCHLA Complex Oscillator",               # the 259: FM index on the matrix, timbre on an LFO, LPG voice
      m(1, WC, osc_w1=TRI, osc_w2=SINE, k1=70, k7=wc_ratio(1.41), k8=70, k2=85, k3=60, k4=30, k5=110, k6=55,
        k11=LPG_COMBO, k12=90, amp_d=70, amp_s=0, amp_r=60, dsend=35, rsend=40),
      mod(1, VEL, TA, 110), mod(2, LFO1, TC, 100), mod(3, RND, TB, 85), lfo1_rate=28, lfo1_shape=TRI_L,
      **fx(dly(note="1/4T", fb=55, tone=60, mod=40), rev(75, 80, 20, 60, 60, 70))),
    P("BUCHLA Source of Uncertainty",            # random everywhere: every note a different strike
      m(1, WC, k1=60, k7=wc_ratio(2.0), k2=60, k3=50, k5=127, k6=35, k11=LPG_COMBO, amp_d=50, amp_s=0, pan=36,
        dsend=50, rsend=50),
      m(2, WC, osc_w1=SAW, k1=45, k7=wc_ratio(3.0), k2=70, k3=30, k5=127, k6=45, tune=12, level=65, amp_d=55,
        amp_s=0, pan=92, dsend=50, rsend=50),
      mod(1, RND, TA, 120), mod(2, RND, TB, 110), mod(3, RND, PAN, 100), mod(4, RND, PITCH, 66),
      **fx(dly(note="1/16D", fb=60, tone=60, mod=30, rev=50), rev(85, 90, 15, 60, 70, 80))),
    P("BUCHLA Plonk Sequence",
      m(1, WC, osc_w2=TRI, k1=40, k7=wc_ratio(0.5), k2=60, k3=50, k5=127, k6=25, k11=LPG_LP, k12=100, amp_d=45,
        amp_s=0, amp_r=35, dsend=60),
      m(4, AN, osc_w1=SINE, k7=127, k9=0, k5=60, k6=20, tune=-12, level=50, amp_d=45, amp_s=0),
      mod(1, LFO1, TB, 100, 1), lfo1_sync=1, lfo1_rate=ldiv("1/4T"), lfo1_shape=SH,
      **fx(dly(note="1/8", fb=65, tone=45, drive=50, mod=25, duck=40), rev(50, 50, 10, 50, 30, 60))),
    P("BUCHLA Wild Folds",
      m(1, WC, osc_sync=1, osc_w1=SAW, k1=100, k7=wc_ratio(2.76), k2=120, k3=80, k5=100, k6=60, k12=110,
        amp_d=80, amp_s=40, amp_r=60, xsend=40, rsend=40),
      m(2, WC, k1=80, k7=wc_ratio(5.4), k2=100, k3=20, k5=100, k6=60, tune=-12, level=60, amp_d=80, amp_s=40),
      mod(1, LFO1, TB, 115, 1), mod(2, LFO2, TC, 110, 2), lfo1_rate=60, lfo2_rate=33, lfo2_shape=SMOOTH,
      **fx(dirt(60, 110, 60, 20), rev(70, 70, 10, 60, 50, 70))),

    # ================================================================ MOOG: ladder and fat oscillators
    P("MOOG Fat Bass",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=127, k10=semis(-12), k12=70, k3=40, tune=-12, ftype=LP4,
        cutoff=45, reso=40, fshape=60, fenv=105, fdec=60, amp_d=70, amp_s=90, amp_r=40),
      m(3, WT, table="Basics/Sine to Square", k1=80, k7=0, tune=-24, level=50, amp_s=110),
      glide=15, glide_mode=3),
    P("MOOG Taurus Pedal",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=120, k12=75, tune=-24, ftype=LP4, cutoff=40, reso=50,
        fshape=40, fenv=90, fdec=80, amp_a=10, amp_d=90, amp_s=100, amp_r=80, rsend=30),
      glide=40, glide_mode=2, **fx(rev(60, 70, 20, 40, 30, 60))),
    P("MOOG Lead Glide",
      m(1, AN, osc_w1=SAW, osc_w2=PULSE, k4=50, k7=127, k9=110, k10=semis(7), k12=68, ftype=LP4, cutoff=70,
        reso=60, fshape=50, fenv=100, fdec=70, amp_s=110, amp_r=50, dsend=40),
      mod(1, LFO1, PITCH, 66), lfo1_rate=70, lfo1_fade=70,
      glide=45, glide_mode=3, **fx(dly(note="1/8D", fb=45, tone=45, drive=30), rev(70, 70, 20, 50, 40, 60))),
    P("MOOG Ladder Sweep",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=127, k12=74, k14=56, level=127, ftype=LP4, cutoff=30, reso=95, fshape=90,
        amp_s=120, amp_r=60, rsend=40),
      mod(1, LFO1, CUTOFF, 120, 1), lfo1_sync=1, lfo1_rate=ldiv("2 BAR"), lfo1_shape=TRI_L,
      **fx(rev(80, 80, 30, 50, 50, 70))),
    P("MOOG Poly Strings",
      m(1, AN, osc_w1=SAW, osc_w2=SAW, k7=127, k9=127, k12=72, pan=30, ftype=LP4, cutoff=75, reso=20,
        amp_a=80, amp_s=120, amp_r=95, rsend=70),
      m(2, AN, osc_w1=SAW, osc_w2=PULSE, k4=45, k7=127, k9=110, k12=58, tune=12, level=55, pan=98, ftype=LP4,
        cutoff=70, amp_a=85, amp_s=120, amp_r=95, rsend=70),
      mod(1, LFO1, PAN, 90, 1), mod(2, LFO2, PAN, 38, 2), lfo1_rate=20, lfo2_rate=26, drift=80,
      **fx(rev(90, 100, 25, 55, 80, 80))),

    # ================================================================ STRESS: the heaviest the plugin can be
    P("STRESS Unison Wall",
      *[m(n, WT, table=t, k7=127, k4=50, k8=127, k1=70, wt_fx=1, k2=90, k3=60, wt_umode=UNI_SHIMMER, k5=80, k6=80,
          ftype=LP4, cutoff=80, reso=80, fshape=100, amp_s=127, amp_r=127, dsend=80, rsend=80, xsend=60,
          pan=20 + 28 * n) for n, t in ((1, "Basics/Sine to Saw"), (2, "Complex/Metallic"),
                                       (3, "Vocal/Choir Ah to Oo"), (4, "Sound Design/Growl"))],
      *[mod(r, LFO1 if r % 2 else LFO2, DST8[r - 1], 100) for r in range(1, 9)],
      lfo1_rate=110, lfo2_rate=100, lfo2_shape=SH, drift=127,
      **fx(dly(note="1/16", fb=90, drive=80, mod=60, duck=30, rev=80), rev(115, 127, 60, 64, 127, 80))),
    P("STRESS West Fold",
      *[m(n, WC, k1=127, k7=wc_ratio((1.41, 2.76, 3.5, 5.4)[n - 1]), k2=127, k3=80, k5=127, k6=127, osc_sync=1,
          osc_w1=SAW, osc_w2=SAW, ftype=COMB_POS, cutoff=90, fkey=127, reso=110, amp_s=127, amp_r=127,
          dsend=90, rsend=90, xsend=90) for n in range(1, 5)],
      *[mod(r, (LFO1, LFO2, RND, EG1)[r % 4], DST8[r - 1], 110) for r in range(1, 9)],
      lfo1_rate=120, lfo2_rate=115, oversample=OS_ON,
      **fx(dirt(100, 100, 64, 127), dly(note="1/32", fb=100, drive=100, mod=100, rev=100), rev(120, 127, 80, 64, 127))),
    P("STRESS FM Feedback",
      *[m(n, FM, fm_algo=n - 1, k1=127, k2=127, k3=127, k4=127, k7=127, k8=fm_ratio(3.5), k9=fm_ratio(7.0),
          k10=fm_ratio(13.0), k14=fm_ratio(1.5), k6=60, ftype=MULTI, fshape=64, cutoff=90, reso=110, amp_s=127,
          amp_r=127, dsend=80, rsend=80, xsend=80) for n in range(1, 5)],
      *[mod(r, LFO1, DST8[r - 1], 120) for r in range(1, 9)],
      lfo1_rate=127, oversample=OS_ON, **fx(dirt(90, 64, 64, 80), dly(note="1/16T", fb=95, drive=90, mod=90),
                                            rev(110, 120, 40, 64, 120))),
    P("STRESS Analog Sync",
      *[m(n, AN, osc_w1=SAW, osc_w2=PULSE, osc_sync=1, k1=127, k2=100, k3=127, k4=80, k5=127, k6=90, k7=127, k9=127,
          k10=semis(19), ftype=LP4, cutoff=70, reso=127, fshape=127, amp_s=127, amp_r=127, dsend=80, rsend=80,
          xsend=80) for n in range(1, 5)],
      *[mod(r, LFO2, DST8[r - 1], 110) for r in range(1, 9)],
      lfo2_rate=120, lfo2_shape=SH, oversample=OS_ON, drift=127,
      **fx(dirt(110, 40, 50, 60), dly(note="1/8T", fb=105, drive=110, mod=80, duck=50), rev(100, 110, 30, 50, 110))),
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
      lfo1_rate=127, lfo2_rate=120, lfo2_shape=SH, oversample=OS_ON, drift=127,
      **fx(dirt(127, 90, 64, 127), dly(note="1/32", fb=110, drive=127, mod=127, duck=60, rev=127),
           rev(126, 127, 100, 64, 127))),
]

if __name__ == "__main__":
    lines = ["# DeathCoast factory sounds: Name|DC1;key=value;...  Written by tools/make_presets.py -- edit that.",
             "# Kinds: BASS, MELODY, DRUM, EFX, DRONE, STRESS (CPU tests). Wavetables: the plugin's DeathCoast pack."]
    for name, d in PRESETS:
        lines.append(name + "|DC1;" + "".join("%s=%s;" % (k, v) for k, v in d.items()))
    open(os.path.join(ROOT, "presets/deathcoast.tbl"), "w").write("\n".join(lines) + "\n")
    print("presets/deathcoast.tbl: %d presets" % len(PRESETS))
