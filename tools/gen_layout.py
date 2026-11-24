#!/usr/bin/env python3
"""NorthWest: builds layout.conf.

MAIN and ENVELOPES keep Tablor's Stitch pages (layout.stitch6.conf, recoloured), with MAIN's unison knobs
swapped for PAN; every other page is drawn here with framework frames. v1.1 page order:

    MAIN   OSC   FILTER   UNISON   ENVELOPES   MOD   MATRIX   GLOBAL

Run:  python3 tools/gen_layout.py   (writes layout.conf next to it)"""
import os, re
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
src = open(os.path.join(root, "layout.stitch6.conf")).read()
head, *tabs = re.split(r"(?m)^\[tab ", src)
old = {t.split("]", 1)[0]: "[tab " + t for t in tabs}
STRIP = "strip=images/stitch/knob_1.png frames=64"

def knob(cx, cy, label, key, when="", bw=0):
    return 'knob cx=%d cy=%d r=26 label="%s" key=%s %s%s%s' % (cx, cy, label, key, STRIP,
            (" bw=%d" % bw) if bw else "", (" when=" + when) if when else "")
def popup(cx, cy, w, label, key): return 'popup cx=%d cy=%d w=%d h=44 label="%s" key=%s' % (cx, cy, w, label, key)
def enumh(cx, cy, sw, label, key): return 'enum_h cx=%d cy=%d sw=%d sh=34 label="%s" key=%s' % (cx, cy, sw, label, key)
def enumv(cx, cy, sw, label, key): return 'enum_v cx=%d cy=%d label="%s" key=%s sw=%d sh=22' % (cx, cy, label, key, sw)
def frame(x, y, w, h, title): return 'frame x=%d y=%d w=%d h=%d title="%s"' % (x, y, w, h, title)
def text(cx, cy, label, size=1.5): return 'text cx=%d cy=%d label="%s" size=%s' % (cx, cy, label, size)

# ---- MAIN: table, position, level, tune, PAN (unison moved to its own page) -----------------------------
MAIN = old["MAIN"]
for o in (1, 2):
    MAIN = MAIN.replace('label="WT%d UNISON" key=wt%d_uni ' % (o, o), 'label="WT%d PAN" key=wt%d_pan ' % (o, o))
MAIN = re.sub(r'(?m)^qlinks .*$', 'qlinks "MAIN" = wt1_select,-,-,-,wt1_pos,wt1_level,wt1_tune,wt1_pan,'
              'wt2_select,-,-,-,wt2_pos,wt2_level,wt2_tune,wt2_pan', MAIN)
assert "wt1_uni" not in MAIN and "wt2_pan" in MAIN

# ---- OSC: effect mode per oscillator, sub, noise ---------------------------------------------------------
L = ["[tab OSC]"]
FXN = {"fm": ("FM AMOUNT", "FM RATIO"), "sync": ("SYNC", "PULSE WIDTH"), "fold": ("ASYMMETRY", "FOLD"),
       "warp": ("BEND", "FORMANT")}
for o, y0 in ((1, 92), (2, 404)):
    p = "wt%d" % o
    # five options x 80 px = 400 px, centred at 230: 30..430, inside the frame
    L += [frame(10, y0, 880, 304, "OSC %d  EFFECT" % o), enumh(230, y0 + 100, 80, "EFFECT", p + "_fx_mode")]
    for m, (a, b) in FXN.items():
        L += [knob(580, y0 + 150, a, p + "_fx1", "%s_fx_mode:%s" % (p, m), 130),
              knob(760, y0 + 150, b, p + "_fx2", "%s_fx_mode:%s" % (p, m), 130)]
L += [frame(896, 92, 372, 304, "SUB"),
      knob(950, 237, "SUB LEVEL", "sub_level", bw=100), knob(1058, 237, "SUB TUNE", "sub_tune", bw=100),
      popup(1190, 242, 120, "SUB WAVE", "sub_wave"),
      frame(896, 404, 372, 304, "NOISE"),
      knob(990, 549, "NOISE LEVEL", "noise_level"),
      'enum_v cx=1170 cy=554 label="NOISE TYPE" key=noise_type sw=64 sh=22',
      'qlinks "OSC" = wt1_fx_mode,wt1_fx1,wt1_fx2,-,wt2_fx_mode,wt2_fx1,wt2_fx2,-,'
      'sub_level,sub_wave,sub_tune,-,noise_level,noise_type,-,-']
OSC = "\n".join(L) + "\n"

# ---- FILTER (two filters + routing) --------------------------------------------------------------------
L = ["[tab FILTER]", frame(10, 92, 1258, 304, "FILTER 1"),
     popup(120, 242, 150, "TYPE", "flt_type")]
for i, (lab, key) in enumerate((("CUTOFF", "flt_freq"), ("RESONANCE", "flt_res"), ("FILTER ENV", "flt_env"),
                                ("KEY TRACK", "flt_key"), ("VEL TRACK", "flt_vel"))):
    L.append(knob(330 + i * 190, 237, lab, key))
L += [frame(10, 404, 900, 304, "FILTER 2"), popup(120, 554, 150, "TYPE", "flt2_type")]
for i, (lab, key) in enumerate((("FREQ", "flt2_freq"), ("RESONANCE", "flt2_res"), ("ENV", "flt2_env"),
                                ("KEY TRACK", "flt2_key"))):
    L.append(knob(330 + i * 140, 549, lab, key, bw=120))
L += [frame(916, 404, 352, 304, "ROUTING"),
      'enum_v cx=1092 cy=560 label="" key=flt_route sw=130 sh=34',
      'qlinks "FILTER" = flt_freq,flt_res,flt_type,-,flt_env,flt_key,flt_vel,-,flt2_freq,flt2_res,flt2_type,-,flt2_env,flt2_key,flt_route,-']
FILTER = "\n".join(L) + "\n"

# ---- UNISON: the whole stack of each oscillator in one block (was split over MAIN, SHAPE and OSC FX) ----
L = ["[tab UNISON]"]
for o, y0 in ((1, 92), (2, 404)):
    p = "wt%d" % o
    L += [frame(10, y0, 1258, 304, "OSC %d  UNISON" % o),
          knob(160, y0 + 145, "VOICES", p + "_uni"),
          knob(400, y0 + 145, "DETUNE", p + "_detune"),
          knob(640, y0 + 145, "SPREAD", p + "_spread"),
          popup(950, y0 + 150, 220, "MODE", p + "_uni_mode")]
L += ['qlinks "UNISON" = wt1_uni,wt1_detune,wt1_spread,wt1_uni_mode,-,-,-,-,'
      'wt2_uni,wt2_detune,wt2_spread,wt2_uni_mode,-,-,-,-']
UNISON = "\n".join(L) + "\n"

# ---- MOD: envelopes + LFOs (4 rows, two Q-Link sub-pages) --------------------------------------------------
def row(y0): return 'art file=images/row_panel.png x=10 y=%d w=1258 h=154' % y0
L = ["[tab MOD]"]
rows = [88 + i * 156 for i in range(4)]
for n, y0 in zip((1, 2), rows[:2]):
    L += [row(y0), text(75, y0 + 77, "ENV %d" % n)]
    for i, k in enumerate("adsr"):
        L.append(knob(200 + i * 150, y0 + 52, "ATK DEC SUS REL".split()[i], "me%d_%s" % (n, k), bw=110))
    L += [text(960, y0 + 77, "ROUTE IN MATRIX (EG%d)" % n, 1.2)]
for n, y0 in zip((1, 2), rows[2:]):
    p = "lfo%d" % n
    L += [row(y0), text(75, y0 + 77, "LFO %d" % n), popup(230, y0 + 66, 190, "SHAPE", p + "_shape"),
          enumh(500, y0 + 66, 100, "RATE MODE", p + "_sync"), knob(720, y0 + 52, "RATE", p + "_rate"),
          knob(870, y0 + 52, "FADE IN", p + "_fade"), enumh(1090, y0 + 66, 100, "RESTART", p + "_retrig")]
L += ['qlinks "ENVS" = me1_a,me1_d,me1_s,me1_r,-,-,-,-,me2_a,me2_d,me2_s,me2_r,-,-,-,-',
      'qlinks "LFOS" = lfo1_shape,lfo1_rate,lfo1_fade,lfo1_sync,lfo1_retrig,-,-,-,lfo2_shape,lfo2_rate,lfo2_fade,lfo2_sync,lfo2_retrig,-,-,-']
MOD = "\n".join(L) + "\n"

# ---- MATRIX (8 slots) ---------------------------------------------------------------------------------------
L = ["[tab MATRIX]", frame(10, 92, 1258, 616, "MODULATION MATRIX"),
     text(190, 160, "SOURCE", 1.2), text(620, 160, "DESTINATION", 1.2), text(1000, 160, "AMOUNT", 1.2)]
for i in range(8):
    y = 200 + i * 62; n = i + 1
    L += [text(45, y, str(n), 1.4), popup(190, y, 260, "", "mm%d_src" % n), popup(620, y, 220, "", "mm%d_dst" % n),
          'slider_h cx=1000 cy=%d w=420 h=34 label="" ns=0 key=mm%d_amt' % (y, n)]
L.append('qlinks "MATRIX" = ' + ",".join(["mm%d_amt" % n for n in range(1, 9)] + ["mm%d_dst" % n for n in range(1, 9)]))
MATRIX = "\n".join(L) + "\n"

# ---- GLOBAL: glide (monophonic voice), bend range, volume, preset ----------------------------------------------
L = ["[tab GLOBAL]",
     frame(10, 92, 820, 304, "GLIDE"),
     knob(200, 237, "GLIDE TIME", "glide"),
     enumv(560, 250, 150, "GLIDE MODE", "glide_mode"),
     frame(836, 92, 432, 304, "MASTER"),
     knob(950, 237, "BEND RANGE", "pb_range"),
     knob(1150, 237, "VOLUME", "volume"),
     frame(10, 404, 1258, 304, "PRESET"),
     'stepper cx=639 cy=560 w=640 h=56 label="" key=preset get=preset_name style=dotmatrix',
     'qlinks "GLOBAL" = glide,glide_mode,-,-,pb_range,volume,-,-,-,-,-,-,preset']
GLOBAL = "\n".join(L) + "\n"

order = [MAIN, OSC, FILTER, UNISON, old["ENVELOPES"], MOD, MATRIX, GLOBAL]
out = head.rstrip("\n") + "\n\n" + "\n".join(s.rstrip("\n") + "\n" for s in order)
open(os.path.join(root, "layout.conf"), "w").write(out)
print("layout.conf written:", len(order), "tabs")
