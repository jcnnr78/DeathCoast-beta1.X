#!/usr/bin/env python3
"""NorthWest v2.0: builds layout.conf (run tools/gen_art.py first: it draws the pictures used here).

Pages (8, one per Fn key):  MIX   SLOT 1   SLOT 2   SLOT 3   SLOT 4   MOD   MATRIX   GLOBAL

A slot page shows the controls of the machine chosen in that slot: the lines that differ carry
when=sN_machine:<i> and MPC swaps them as the machine changes. Its machine knobs K1..K12 sit in
the same places for every machine (row 1 = K1..K6, row 2 = K7..K12), so a Q-Link always turns the
same knob; their names and values come from the DSP (dynamic_name / dynamic_display).
The theme lines at the top come from layout.stitch6.conf.

Run:  python3 tools/gen_layout.py"""
import importlib.util, os, re
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
head = re.split(r"(?m)^\[tab ", open(os.path.join(root, "layout.stitch6.conf")).read())[0]
_spec = importlib.util.spec_from_file_location("gen_art", os.path.join(here, "gen_art.py"))
ART = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(ART)

STRIP = "strip=images/nw/knob.png frames=128"
def W(s, when): return s + (" when=" + when if when else "")
def knob(cx, cy, label, key, when="", bw=0, r=26):
    return W('knob cx=%d cy=%d r=%d label="%s" key=%s %s%s' % (cx, cy, r, label, key, STRIP, (" bw=%d" % bw) if bw else ""), when)
def popup(cx, cy, w, label, key, when=""): return W('popup cx=%d cy=%d w=%d h=44 label="%s" key=%s' % (cx, cy, w, label, key), when)
def enumh(cx, cy, sw, label, key, when=""): return W('enum_h cx=%d cy=%d sw=%d sh=34 label="%s" key=%s' % (cx, cy, sw, label, key), when)
def enumv(cx, cy, sw, label, key): return 'enum_v cx=%d cy=%d label="%s" key=%s sw=%d sh=22' % (cx, cy, label, key, sw)
def frame(x, y, w, h, title, when=""): return W('frame x=%d y=%d w=%d h=%d title="%s"' % (x, y, w, h, title), when)
def text(cx, cy, label, size=1.5, when=""): return W('text cx=%d cy=%d label="%s" size=%s' % (cx, cy, label, size), when)
def meter(cx, cy, size, key, img, when=""):
    return W('meter cx=%d cy=%d w=%d h=%d key=%s strip=images/nw/%s frames=64' % (cx, cy, size[0], size[1], key, img), when)
def stepper(cx, cy, w, key, get="", when=""):
    return W('stepper cx=%d cy=%d w=%d h=44 label="" key=%s%s style=dotmatrix' % (cx, cy, w, key, (" get=" + get) if get else ""), when)

MACHINES = ["WAVETABLE", "FM", "WEST COAST", "ANALOG"]
# K1..K12 labels per machine ("" = the machine doesn't use it: no knob shown)
K_LABELS = {
    0: ["POSITION", "FX 1", "FX 2", "DETUNE", "SPREAD", "", "UNISON", "", "", "", "", ""],
    1: ["OP2 LEVEL", "OP3 LEVEL", "OP4 LEVEL", "FEEDBACK", "PITCH ENV", "NOISE",
        "MOD DECAY", "OP2 RATIO", "OP3 RATIO", "OP4 RATIO", "PITCH DECAY", "VEL > FM"],
}
KX = [120 + i * 205 for i in range(6)]      # knob columns of the machine area
ROW1, ROW2 = 272, 398

# ---- MIX: machine, level, pan, tune, mute for each slot -----------------------------------------------------
L = ["[tab MIX]"]
q = []
for n in range(1, 5):
    s = "s%d_" % n; x0 = 10 + (n - 1) * 316; cx = x0 + 155
    L += [frame(x0, 92, 310, 616, "SLOT %d" % n),
          popup(cx, 196, 250, "MACHINE", s + "machine"),
          knob(cx - 70, 350, "LEVEL", s + "level", bw=120), knob(cx + 70, 350, "PAN", s + "pan", bw=120),
          knob(cx - 70, 520, "TUNE", s + "tune", bw=120),
          'toggle cx=%d cy=%d label="MUTE" key=%smute' % (cx + 70, 515, s)]
    q += [s + "level", s + "pan", s + "tune", s + "mute"]
L.append('qlinks "MIX" = ' + ",".join(q))
MIX = "\n".join(L) + "\n"

# ---- SLOT n ----------------------------------------------------------------------------------------------------
def slot_page(n):
    s = "s%d_" % n; m = s + "machine"
    L = ["[tab SLOT %d]" % n,
         frame(10, 92, 1258, 108, ""),
         text(80, 150, "SLOT %d" % n, 1.6),
         stepper(330, 152, 300, m),
         popup(560, 154, 130, "", m)]
    # machine-specific selectors in the top band
    L += [stepper(880, 152, 360, s + "select", s + "tname", m + ":0"),
          popup(1160, 158, 170, "EFFECT", s + "wt_fx", m + ":0"),
          popup(800, 158, 230, "ALGORITHM", s + "fm_algo", m + ":1"),
          enumh(1100, 162, 110, "MODE", s + "fm_mode", m + ":1"),
          text(900, 150, "WEST COAST: COMING IN v2.1", 1.3, m + ":2"),
          text(900, 150, "ANALOG: COMING IN v2.2", 1.3, m + ":3")]
    # machine area
    for i, name in enumerate(MACHINES):
        L.append(frame(10, 206, 1258, 270, name, "%s:%d" % (m, i)))
    for mi, labels in K_LABELS.items():
        for k, lab in enumerate(labels):
            if lab:
                L.append(knob(KX[k % 6], ROW1 if k < 6 else ROW2, lab, s + "k%d" % (k + 1), "%s:%d" % (m, mi), bw=150))
    L += [meter(KX[1] + 102 + 178, ROW2 + 6, ART.WAVE, s + "k1", "wave_%dx%d.png" % ART.WAVE, m + ":0"),
          popup(KX[5], ROW2 + 6, 200, "UNISON MODE", s + "wt_umode", m + ":0")]
    # filter
    FS, ES = ART.FLT, ART.ENV
    L += [frame(10, 482, 690, 226, "FILTER"), popup(95, 632, 150, "TYPE", s + "ftype")]
    for i, t in enumerate(ART.FLT_TYPES):
        L.append(meter(335, 538, FS, s + "cutoff", "flt_%s_%dx%d.png" % ((t,) + FS), s + "ftype:%d" % i))
    L.append(meter(535, 538, ES, s + "fdec", "env_d_%dx%d.png" % ES))
    for i, (lab, k) in enumerate((("CUTOFF", "cutoff"), ("RESONANCE", "reso"), ("ENV", "fenv"), ("DECAY", "fdec"), ("KEY", "fkey"))):
        L.append(knob(235 + i * 100, 628, lab, s + k, bw=96))
    # amp
    L.append(frame(706, 482, 562, 226, "AMP"))
    for i, (lab, k) in enumerate((("ATTACK", "amp_a"), ("DECAY", "amp_d"), ("SUSTAIN", "amp_s"), ("RELEASE", "amp_r"), ("VELOCITY", "vel"))):
        cx = 770 + i * 108
        if i < 4: L.append(meter(cx, 538, ES, s + k, "env_%s_%dx%d.png" % (("adsr"[i],) + ES)))
        L.append(knob(cx, 628, lab, s + k, bw=104))
    # Q-Link columns of 4: row 1 left, row 2 left, the right 2x2, the filter
    L += ['qlinks "MACHINE" = ' + ",".join(s + "k%d" % k for k in (1, 2, 3, 4, 7, 8, 9, 10, 5, 6, 11, 12)) + "," +
          ",".join(s + x for x in ("cutoff", "reso", "fenv", "fdec")),
          'qlinks "FILTER AMP" = ' + ",".join((s + x if x != "-" else x) for x in ("cutoff", "reso", "fenv", "fdec", "fkey", "ftype", "machine", "-",
                                                               "amp_a", "amp_d", "amp_s", "amp_r", "vel")) + ",-,-,-"]
    return "\n".join(L) + "\n"

# ---- MOD: EG1/EG2 + LFOs (4 rows, two Q-Link sub-pages) --------------------------------------------------------
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

# ---- MATRIX (8 rows: source -> destination x amount, on one slot or all) ---------------------------------------
L = ["[tab MATRIX]", frame(10, 92, 1258, 616, "MODULATION MATRIX"),
     text(185, 160, "SOURCE", 1.2), text(470, 160, "DESTINATION", 1.2), text(705, 160, "SLOT", 1.2),
     text(1010, 160, "AMOUNT", 1.2)]
for i in range(8):
    y = 200 + i * 62; n = i + 1
    L += [text(40, y, str(n), 1.4), popup(185, y, 240, "", "mm%d_src" % n), popup(470, y, 230, "", "mm%d_dst" % n),
          popup(705, y, 130, "", "mm%d_slot" % n),
          'slider_h cx=1010 cy=%d w=400 h=34 label="" ns=0 key=mm%d_amt' % (y, n)]
L.append('qlinks "MATRIX" = ' + ",".join(["mm%d_amt" % n for n in range(1, 9)] + ["mm%d_dst" % n for n in range(1, 9)]))
MATRIX = "\n".join(L) + "\n"

# ---- GLOBAL: glide, bend range, volume, preset, logo -----------------------------------------------------------
L = ["[tab GLOBAL]",
     frame(10, 92, 820, 304, "GLIDE"), knob(200, 237, "GLIDE TIME", "glide"),
     enumv(560, 250, 150, "GLIDE MODE", "glide_mode"),
     frame(836, 92, 432, 304, "MASTER"), knob(950, 237, "BEND RANGE", "pb_range"), knob(1150, 237, "VOLUME", "volume"),
     frame(10, 404, 1258, 304, "PRESET"),
     'stepper cx=500 cy=560 w=640 h=56 label="" key=preset get=preset_name style=dotmatrix',
     'art file=images/nw/logo.png x=%d y=%d w=316 h=60' % (1100 - 158, 560 - 30),
     'qlinks "GLOBAL" = glide,glide_mode,-,-,pb_range,volume,-,-,-,-,-,-,preset']
GLOBAL = "\n".join(L) + "\n"

order = [MIX] + [slot_page(n) for n in range(1, 5)] + [MOD, MATRIX, GLOBAL]
out = head.rstrip("\n") + "\n\n" + "\n".join(x.rstrip("\n") + "\n" for x in order)
open(os.path.join(root, "layout.conf"), "w").write(out)
print("layout.conf written:", len(order), "tabs")
