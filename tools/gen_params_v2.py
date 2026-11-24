#!/usr/bin/env python3
"""NorthWest v1.5: the whole parameter surface, from this one list.

Writes src/dsp/params.h (engine), params.json (VST parameters, in this order = the VST index) and params.lock.
4 monophonic MACHINES, each running a MODEL (WAVETABLE, FM, WEST COAST, ANALOG), its filter and amp envelope, and
12 knobs K1..K12 whose meaning (name and value text) depends on the model: the DSP answers
get_param("mN_kM_name") / ("mN_kM_display") (params.json "dynamic_name"/"dynamic_display"). Modulation (EG1/EG2,
LFO1/LFO2, an 8-row matrix with a target machine per row) is shared. Run:  python3 tools/gen_params_v2.py"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "northwest-1.5"
SLOTS = 4

MODELS     = ["WAVETABLE", "FM", "WEST COAST", "ANALOG"]
FLT_TYPES  = ["LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "NOTCH 12", "NOTCH 24"]
WT_FX      = ["NONE", "FM", "SYNC", "FOLD", "WARP"]
UNI_MODES  = ["CLASSIC", "SHIMMER", "NOISE", "PHASE SYNC", "POS SPREAD", "RANDOM"]
FM_ALGOS   = ["4>3>2>1", "3+4>2>1", "4>3>1<2", "4>3 2>1", "4>1+2+3", "4>3>2 +1", "2>1 +3 +4", "1+2+3+4"]
FM_MODES   = ["TONE", "DRUM"]
OSC_WAVES  = ["SINE", "TRI", "SAW", "PULSE"]
OFF_ON     = ["OFF", "ON"]
LFO_SHAPES = ["SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "SMOOTH RND"]
MOD_SRC    = ["NONE", "EG1", "EG2", "LFO1", "LFO2", "VELOCITY", "KEY", "MOD WHEEL", "AFTERTOUCH", "PITCH BEND",
              "RANDOM", "AMP ENV", "FILTER ENV"]
# TIMBRE A..D = K1..K4 of whatever model the machine runs
MOD_DST    = ["NONE", "PITCH", "LEVEL", "PAN", "CUTOFF", "RESONANCE", "TIMBRE A", "TIMBRE B", "TIMBRE C", "TIMBRE D"]
MOD_SLOT   = ["ALL", "M1", "M2", "M3", "M4"]
GLIDE_MODES = ["OFF", "GLISSANDO", "PORTAMENTO", "LEGATO"]
K_DEFAULTS = [0, 0, 0, 0, 0, 0, 43, 64, 64, 64, 43, 64]     # K1..K12 (see src/dsp/machines.h)

P = []   # (key, name, kind, min, max, default, options, extra-json)
def I(key, name, lo, hi, d, **x): P.append((key, name, "int", lo, hi, d, None, x))
def E(key, name, opts, d=0, **x):  P.append((key, name, "enum", 0, len(opts) - 1, d, opts, x))
def PATH(key):                     P.append((key, key, "path", 0, 0, 0, None, {}))

for n in range(1, SLOTS + 1):
    s = "m%d_" % n; S = "M%d " % n
    E(s + "model", S + "MODEL", MODELS, 0, refresh_display=True)
    I(s + "level", S + "LEVEL", 0, 127, 100 if n == 1 else 0)
    I(s + "pan", S + "PAN", 0, 127, 64)
    E(s + "mute", S + "MUTE", OFF_ON, 0)
    I(s + "tune", S + "TUNE", -24, 24, 0)
    I(s + "amp_a", S + "ATTACK", 0, 127, 0)
    I(s + "amp_d", S + "DECAY", 0, 127, 64)
    I(s + "amp_s", S + "SUSTAIN", 0, 127, 102)
    I(s + "amp_r", S + "RELEASE", 0, 127, 50)
    I(s + "vel", S + "VELOCITY", 0, 127, 100)
    I(s + "cutoff", S + "CUTOFF", 0, 127, 127)
    I(s + "reso", S + "RESONANCE", 0, 127, 0)
    E(s + "ftype", S + "FILTER TYPE", FLT_TYPES, 1)
    I(s + "fenv", S + "FILTER ENV", 0, 127, 64)
    I(s + "fdec", S + "FILTER DECAY", 0, 127, 64)
    I(s + "fkey", S + "KEY TRACK", 0, 127, 0)
    for k in range(12):
        I(s + "k%d" % (k + 1), S + "K%d" % (k + 1), 0, 127, K_DEFAULTS[k], dynamic_name=True, dynamic_display=True)
    E(s + "wt_fx", S + "WT EFFECT", WT_FX, 0)
    E(s + "wt_umode", S + "WT UNI MODE", UNI_MODES, 0)
    E(s + "fm_algo", S + "FM ALGORITHM", FM_ALGOS, 0)
    E(s + "fm_mode", S + "FM MODE", FM_MODES, 0)
    E(s + "osc_w1", S + "OSC1 WAVE", OSC_WAVES, 0)
    E(s + "osc_w2", S + "OSC2 WAVE", OSC_WAVES, 0)
    E(s + "osc_sync", S + "OSC SYNC", OFF_ON, 0)
    I(s + "dsend", S + "DELAY SEND", 0, 127, 0)
    I(s + "rsend", S + "REVERB SEND", 0, 127, 0)
    PATH(s + "table")
for i in (1, 2):
    I("me%d_a" % i, "EG%d A" % i, 0, 127, 0); I("me%d_d" % i, "EG%d D" % i, 0, 127, 64)
    I("me%d_s" % i, "EG%d S" % i, 0, 127, 0); I("me%d_r" % i, "EG%d R" % i, 0, 127, 64)
for i in (1, 2):
    E("lfo%d_shape" % i, "LFO%d SHAPE" % i, LFO_SHAPES, 0)
    E("lfo%d_sync" % i, "LFO%d SYNC" % i, ["FREE", "SYNC"], 0)
    I("lfo%d_rate" % i, "LFO%d RATE" % i, 0, 127, 64)
    I("lfo%d_fade" % i, "LFO%d FADE" % i, 0, 127, 0)
    E("lfo%d_retrig" % i, "LFO%d RETRIG" % i, ["KEY", "FREE"], 0)
for m in range(1, 9):
    E("mm%d_src" % m, "MOD%d SOURCE" % m, MOD_SRC, 0)
    E("mm%d_dst" % m, "MOD%d DEST" % m, MOD_DST, 0)
    I("mm%d_amt" % m, "MOD%d AMOUNT" % m, 0, 127, 64)
    E("mm%d_slot" % m, "MOD%d MACHINE" % m, MOD_SLOT, 0)
I("glide", "GLIDE", 0, 127, 0)
E("glide_mode", "GLIDE MODE", GLIDE_MODES, 0)
I("pb_range", "BEND RANGE", 0, 24, 2)
I("volume", "VOLUME", 0, 127, 100)
# FX: one delay and one reverb, fed by each machine's sends (mN_dsend / mN_rsend)
I("fx_dtime", "DELAY TIME", 0, 127, 70)
I("fx_dfb", "DELAY FEEDBACK", 0, 127, 50)
I("fx_dtone", "DELAY TONE", 0, 127, 80)
E("fx_dmode", "DELAY MODE", ["STEREO", "PING PONG"], 1)
I("fx_dlevel", "DELAY LEVEL", 0, 127, 100)
I("fx_rsize", "REVERB SIZE", 0, 127, 70)
I("fx_rdamp", "REVERB DAMP", 0, 127, 60)
I("fx_rlevel", "REVERB LEVEL", 0, 127, 100)

# ------------------------------------------------------------------------------------------------ params.h
opt_names, opt_lines = {}, []
def opts_var(opts):
    t = tuple(opts)
    if t not in opt_names:
        opt_names[t] = "tb_opts_%d" % len(opt_names)
        opt_lines.append("static const char *const %s[] = { %s };" % (opt_names[t], ", ".join('"%s"' % o for o in opts)))
    return opt_names[t]
rows = []
for key, name, kind, lo, hi, d, opts, x in P:
    if kind == "enum":
        rows.append('    { "%s", TB_ENUM, %d, %d, %d, %d, %s },' % (key, lo, hi, d, len(opts), opts_var(opts)))
    elif kind == "path":
        rows.append('    { "%s", TB_PATH, 0, 0, 0, 0, 0 },' % key)
    else:
        rows.append('    { "%s", TB_INT, %d, %d, %d, 0, 0 },' % (key, lo, hi, d))
chain = []
for key, name, kind, lo, hi, d, opts, x in P:
    if kind == "enum":  chain.append({"key": key, "name": name, "type": "enum", "options": opts, "default": opts[d]})
    elif kind == "int": chain.append({"key": key, "name": name, "type": "int", "min": lo, "max": hi, "default": d})
cj = json.dumps(chain, separators=(",", ":")).replace("\\", "\\\\").replace('"', '\\"')
h = ["/* Generated by tools/gen_params_v2.py -- DO NOT EDIT. */",
     "#ifndef TABLOR_PARAMS_H", "#define TABLOR_PARAMS_H", "",
     '#define TB_VERSION "%s"' % VERSION, "#define TB_PARAM_COUNT %d" % len(P), "#define TB_SLOTS %d" % SLOTS,
     "#define TB_KNOBS 12", "",
     "typedef enum { TB_INT = 0, TB_ENUM = 1, TB_PATH = 2 } tb_param_type_t;", "",
     "typedef struct {", "    const char *key;", "    tb_param_type_t type;",
     "    float min, max, def;              /* enums: def is the option index */",
     "    int n_options;                    /* 0 for non-enums                */",
     "    const char *const *options;       /* NULL for non-enums             */", "} tb_param_t;", "",
     "/* TB_PATH params (each machine's wavetable file) store a string, not a float. */",
     "#define TB_WT_SLOTS %d" % SLOTS, "",
     "enum {"] + ["    TB_P_%s = %d," % (p[0].upper(), i) for i, p in enumerate(P)] + ["};", ""]
h += ["/* per-machine block: machine n's parameter = TB_P_M1_<name> + n * TB_SLOT_STRIDE (n = 0..3) */",
      "#define TB_SLOT_STRIDE (TB_P_M2_MODEL - TB_P_M1_MODEL)",
      "/* matrix row m: TB_P_MM1_SRC + m * 4 (+0 src, +1 dst, +2 amount, +3 slot) */", ""]
h += opt_lines + ["", "static const tb_param_t tb_params[TB_PARAM_COUNT] = {"] + rows + ["};", "",
      "static const char *tb_chain_params_json =", '    "%s";' % cj, "",
      "/* Move's stock hierarchy UI is not used by NorthWest */",
      'static const char *tb_ui_hierarchy_json = "{\\"levels\\":{}}";', "",
      "#endif /* TABLOR_PARAMS_H */", ""]
open(os.path.join(ROOT, "src/dsp/params.h"), "w").write("\n".join(h))

# ------------------------------------------------------------------------------------------------ params.json
out = []
for key, name, kind, lo, hi, d, opts, x in P:
    if kind == "path":
        n = key[1]
        out += [{"key": key[:3] + "select", "refresh_display": True, "name": "M%s TABLE" % n, "min": 0, "max": 127,
                 "default": 0, "display": "int", "type": "stepper"},
                {"key": key[:3] + "select_prev", "name": "M%s TABLE PREV" % n, "momentary": True,
                 "step_of": key[:3] + "select", "step_delta": -1},
                {"key": key[:3] + "select_next", "name": "M%s TABLE NEXT" % n, "momentary": True,
                 "step_of": key[:3] + "select", "step_delta": 1},
                {"key": key[:3] + "tname", "name": "M%s TABLE NAME" % n, "min": 0, "max": 0, "display": "string",
                 "type": "readout"}]
        continue
    e = {"key": key, "name": name}
    if kind == "enum": e.update(options=opts, default=d)
    else: e.update(min=lo, max=hi, default=d, display="int")
    e.update(x)
    out.append(e)
    # arrows for the selectors drawn as steppers (arrows + a box the MPC data wheel turns once touched)
    if key.endswith(("_model", "_wt_fx", "_wt_umode", "_fm_algo", "_fm_mode", "_osc_w1", "_osc_w2", "_osc_sync", "fx_dmode", "glide_mode")):
        out += [{"key": key + "_prev", "name": name + " PREV", "momentary": True, "step_of": key, "step_delta": -1},
                {"key": key + "_next", "name": name + " NEXT", "momentary": True, "step_of": key, "step_delta": 1}]
out += [{"key": "preset", "refresh_display": True, "name": "PRESET", "min": 0, "max": 127, "default": 0,
         "display": "int", "type": "stepper"},
        {"key": "preset_prev", "name": "PREV PRESET", "momentary": True, "step_of": "preset", "step_delta": -1},
        {"key": "preset_next", "name": "NEXT PRESET", "momentary": True, "step_of": "preset", "step_delta": 1},
        {"key": "preset_name", "name": "PRESET NAME", "min": 0, "max": 0, "display": "string", "type": "readout"}]
# the filter picture of each machine: a value the DSP works out from CUTOFF and RESONANCE (32 x 4 steps), shown by
# a meter; HAS_DISPLAY_REV (vst.json) makes the wrapper report it when it changes ("display_rev")
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_fview" % n, "name": "M%d FILTER VIEW" % n, "min": 0, "max": 127, "default": 124,
                "display": "int", "clamped": True})
# the WEST COAST picture: FOLD (16 steps) x SYMMETRY (8) as one value, worked out by the plugin
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_cview" % n, "name": "M%d FOLD VIEW" % n, "min": 0, "max": 127, "default": 0,
                "display": "int", "clamped": True})
# the WAVETABLE picture: which frame of images/nw/tables_*.png (the loaded table), worked out by the plugin
import re as _re
_nf = int(_re.search(r"TB_TABLE_VIEW_FRAMES (\d+)", open(os.path.join(ROOT, "src/dsp/table_view.h")).read()).group(1))
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_wview" % n, "name": "M%d TABLE VIEW" % n, "min": 0, "max": _nf - 1, "default": 1,
                "display": "int", "clamped": True})
json.dump({"name": "NorthWest", "params": out}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
open(os.path.join(ROOT, "params.lock"), "w").write("\n".join(p["key"] for p in out) + "\n")
print("engine params %d | VST params %d" % (len(P), len(out)))
