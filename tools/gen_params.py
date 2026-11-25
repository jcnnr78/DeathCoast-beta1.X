#!/usr/bin/env python3
"""NorthWest: the whole parameter surface, from this one list.

Writes src/dsp/params.h (engine), params.json (VST parameters; this order = the VST index) and params.lock.
4 monophonic MACHINES, each running a MODEL (WAVETABLE, FM, WEST COAST, ANALOG) with its filter, amp envelope, MIDI
channel and three effect sends, and 14 knobs K1..K14 whose meaning (name and value text) depends on the model: the
plugin answers get_param("mN_kM_name") / ("mN_kM_display") (params.json "dynamic_name" / "dynamic_display").
Modulation (EG1/EG2, LFO1/LFO2, an 8-row matrix with a target machine per row) and the effects are shared.
Run:  python3 tools/gen_params.py"""
import json, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "1.6.0"
SLOTS = 4
KNOBS = 14

MODELS     = ["WAVETABLE", "FM", "WEST COAST", "ANALOG"]
CHANNELS   = ["TRACK"] + ["CH %d" % c for c in range(1, 17)]
# the filter types (Digitone II style): src/dsp/filters.h
FLT_TYPES  = ["MULTI", "LP 4", "COMB -", "COMB +", "EQ", "LEGACY LP", "LEGACY HP"]
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
K_DEFAULTS = [0, 0, 0, 0, 0, 0, 43, 64, 64, 64, 43, 64, 0, 64]     # K1..K14 (src/dsp/machines.h)
# selectors drawn as steppers: arrows (_prev/_next) and a box the data wheel turns (_st, see below)
STEPPERS = ("_model", "_chan", "_ftype", "_wt_fx", "_wt_umode", "_fm_algo", "_fm_mode", "_osc_w1", "_osc_w2",
            "_osc_sync", "fx_dmode", "glide_mode", "oversample")

P = []   # (key, name, kind, min, max, default, options, extra-json)
def I(key, name, lo, hi, d, **x): P.append((key, name, "int", lo, hi, d, None, x))
def E(key, name, opts, d=0, **x):  P.append((key, name, "enum", 0, len(opts) - 1, d, opts, x))
def PATH(key):                     P.append((key, key, "path", 0, 0, 0, None, {}))

for n in range(1, SLOTS + 1):
    s = "m%d_" % n; S = "M%d " % n
    E(s + "model", S + "MODEL", MODELS, 0, refresh_display=True)
    E(s + "chan", S + "MIDI CHANNEL", CHANNELS, 0)
    E(s + "mute", S + "MUTE", OFF_ON, 0)
    I(s + "level", S + "LEVEL", 0, 127, 100 if n == 1 else 0)
    I(s + "pan", S + "PAN", 0, 127, 64)
    I(s + "tune", S + "TUNE", -24, 24, 0)
    I(s + "dsend", S + "DELAY SEND", 0, 127, 0)
    I(s + "rsend", S + "REVERB SEND", 0, 127, 0)
    I(s + "xsend", S + "DIRT SEND", 0, 127, 0)
    I(s + "amp_a", S + "ATTACK", 0, 127, 0)
    I(s + "amp_d", S + "DECAY", 0, 127, 64)
    I(s + "amp_s", S + "SUSTAIN", 0, 127, 102)
    I(s + "amp_r", S + "RELEASE", 0, 127, 50)
    I(s + "vel", S + "VELOCITY", 0, 127, 100)
    E(s + "ftype", S + "FILTER TYPE", FLT_TYPES, 1, refresh_display=True)
    I(s + "cutoff", S + "CUTOFF", 0, 127, 127)
    I(s + "reso", S + "RESONANCE", 0, 127, 0)
    # one knob whose meaning follows the type: MULTI morph, LP 4 drive, COMB damping, EQ gain, LEGACY slope
    I(s + "fshape", S + "FILTER SHAPE", 0, 127, 0, dynamic_name=True, dynamic_display=True)
    I(s + "fenv", S + "FILTER ENV", 0, 127, 64)
    I(s + "fdec", S + "FILTER DECAY", 0, 127, 64)
    I(s + "fkey", S + "KEY TRACK", 0, 127, 0)
    I(s + "bwlo", S + "BASE", 0, 127, 0, dynamic_display=True)      # base-width: high pass (0 = off)
    I(s + "bwhi", S + "WIDTH", 0, 127, 127, dynamic_display=True)   # base-width: low pass (127 = off)
    for k in range(KNOBS):
        I(s + "k%d" % (k + 1), S + "K%d" % (k + 1), 0, 127, K_DEFAULTS[k], dynamic_name=True, dynamic_display=True)
    E(s + "wt_fx", S + "WT EFFECT", WT_FX, 0)
    E(s + "wt_umode", S + "WT UNI MODE", UNI_MODES, 0)
    E(s + "fm_algo", S + "FM ALGORITHM", FM_ALGOS, 0)
    E(s + "fm_mode", S + "FM MODE", FM_MODES, 0)
    E(s + "osc_w1", S + "OSC1 WAVE", OSC_WAVES, 0)
    E(s + "osc_w2", S + "OSC2 WAVE", OSC_WAVES, 0)
    E(s + "osc_sync", S + "OSC SYNC", OFF_ON, 0)
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
I("drift", "DRIFT", 0, 127, 0, dynamic_display=True)          # analog drift of pitch and cutoff, every machine
E("oversample", "OVERSAMPLING", OFF_ON, 1)                     # 2x in WEST COAST, ANALOG and FM
# FX: a delay, a reverb and a distortion (DIRT), fed by each machine's sends
I("fx_dtime", "DELAY TIME", 0, 127, 70, dynamic_display=True)
I("fx_dfb", "DELAY FEEDBACK", 0, 127, 50)
I("fx_dtone", "DELAY TONE", 0, 127, 80)
E("fx_dmode", "DELAY MODE", ["STEREO", "PING PONG"], 1)
I("fx_dlevel", "DELAY LEVEL", 0, 127, 100)
I("fx_rsize", "REVERB SIZE", 0, 127, 70)
I("fx_rdamp", "REVERB DAMP", 0, 127, 60)
I("fx_rlevel", "REVERB LEVEL", 0, 127, 100)
I("fx_xdrive", "DIRT DRIVE", 0, 127, 64)
I("fx_xchar", "DIRT CHARACTER", 0, 127, 40, dynamic_display=True)
I("fx_xtone", "DIRT TONE", 0, 127, 64, dynamic_display=True)
I("fx_xgrit", "DIRT GRIT", 0, 127, 0)

# ------------------------------------------------------------------------------------------------ params.h
opt_names, opt_lines = {}, []
def opts_var(opts):
    t = tuple(opts)
    if t not in opt_names:
        opt_names[t] = "nw_opts_%d" % len(opt_names)
        opt_lines.append("static const char *const %s[] = { %s };" % (opt_names[t], ", ".join('"%s"' % o for o in opts)))
    return opt_names[t]
rows = []
for key, name, kind, lo, hi, d, opts, x in P:
    if kind == "enum":
        rows.append('    { "%s", NW_ENUM, %d, %d, %d, %d, %s },' % (key, lo, hi, d, len(opts), opts_var(opts)))
    elif kind == "path":
        rows.append('    { "%s", NW_PATH, 0, 0, 0, 0, 0 },' % key)
    else:
        rows.append('    { "%s", NW_INT, %d, %d, %d, 0, 0 },' % (key, lo, hi, d))
h = ["/* Generated by tools/gen_params.py -- DO NOT EDIT. */",
     "#ifndef NORTHWEST_PARAMS_H", "#define NORTHWEST_PARAMS_H", "",
     '#define NW_VERSION "%s"' % VERSION, "#define NW_PARAM_COUNT %d" % len(P), "#define NW_SLOTS %d" % SLOTS,
     "#define NW_KNOBS %d" % KNOBS, "#define NW_FILTER_TYPES %d" % len(FLT_TYPES), "",
     "typedef enum { NW_INT = 0, NW_ENUM = 1, NW_PATH = 2 } nw_param_type_t;", "",
     "typedef struct {", "    const char *key;", "    nw_param_type_t type;",
     "    float min, max, def;              /* enums: def is the option index */",
     "    int n_options;                    /* 0 for non-enums                */",
     "    const char *const *options;       /* NULL for non-enums             */", "} nw_param_t;", "",
     "enum {"] + ["    NW_P_%s = %d," % (p[0].upper(), i) for i, p in enumerate(P)] + ["};", ""]
h += ["/* per-machine block: machine n's parameter = NW_P_M1_<name> + n * NW_SLOT_STRIDE (n = 0..3) */",
      "#define NW_SLOT_STRIDE (NW_P_M2_MODEL - NW_P_M1_MODEL)",
      "/* matrix row m: NW_P_MM1_SRC + m * 4 (+0 src, +1 dst, +2 amount, +3 slot) */", ""]
h += opt_lines + ["", "static const nw_param_t nw_params[NW_PARAM_COUNT] = {"] + rows + ["};", "",
      "#endif /* NORTHWEST_PARAMS_H */", ""]
open(os.path.join(ROOT, "src/dsp/params.h"), "w").write("\n".join(h))

# ------------------------------------------------------------------------------------------------ params.json
_nf = int(re.search(r"NW_TABLE_VIEW_FRAMES (\d+)", open(os.path.join(ROOT, "src/dsp/table_view.h")).read()).group(1))
out = []
for key, name, kind, lo, hi, d, opts, x in P:
    if kind == "path":
        n = key[1]
        # the table list: Init + every table the plugin finds (the shipped ones are the picture's frames 1..)
        out += [{"key": key[:3] + "select", "refresh_display": True, "name": "M%s TABLE" % n, "min": 0,
                 "max": max(127, _nf - 2), "default": 0, "display": "int", "type": "stepper", "nudge_pct": 50,
                 "clamped": True},   # the list holds only the tables found: the host test can't land past them
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
    if key.endswith(STEPPERS):
        out += [{"key": key + "_prev", "name": name + " PREV", "momentary": True, "step_of": key, "step_delta": -1},
                {"key": key + "_next", "name": name + " NEXT", "momentary": True, "step_of": key, "step_delta": 1},
                # the stepper's box: the same selector as a whole number (the plugin answers "<key>_st"), so the data
                # wheel and the -/+ keys move it one option a click ("nudge_pct"); an option list needs half an
                # option's width of wheel travel per step under QLINK_TRAVEL (50 clicks on a 2-option list)
                {"key": key + "_st", "name": name, "min": 0, "max": len(opts) - 1, "default": d, "display": "int",
                 "dynamic_display": True, "nudge_pct": 50, "refresh_display": True,
                 "clamped": True}]   # it follows the option list (the host test steps it on a range of 6+)
out += [{"key": "preset", "refresh_display": True, "name": "PRESET", "min": 0, "max": 127, "default": 0,
         "display": "int", "type": "stepper", "nudge_pct": 50, "clamped": True},   # as many as the file holds
        {"key": "preset_prev", "name": "PREV PRESET", "momentary": True, "step_of": "preset", "step_delta": -1},
        {"key": "preset_next", "name": "NEXT PRESET", "momentary": True, "step_of": "preset", "step_delta": 1},
        {"key": "preset_name", "name": "PRESET NAME", "min": 0, "max": 0, "display": "string", "type": "readout"},
        # beta: the share of each audio block the engine takes (worked out by the plugin, shown on MAIN)
        {"key": "cpu", "name": "CPU", "min": 0, "max": 0, "display": "string", "type": "readout"}]
# pictures the plugin works out (HAS_DISPLAY_REV makes the wrapper report them when they change: "display_rev"):
# the filter curve (frame of the type's strip), the WEST COAST fold, the WAVETABLE table and its POSITION
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_fview" % n, "name": "M%d FILTER VIEW" % n, "min": 0, "max": 191, "default": 0,
                "display": "int", "clamped": True})
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_cview" % n, "name": "M%d FOLD VIEW" % n, "min": 0, "max": 127, "default": 0,
                "display": "int", "clamped": True})
for n in range(1, SLOTS + 1):
    out.append({"key": "m%d_wview" % n, "name": "M%d TABLE VIEW" % n, "min": 0, "max": _nf - 1, "default": 1,
                "display": "int", "clamped": True})
# the WAVETABLE scope: the frame POSITION is on, drawn live over the table's picture as SCOPE_COLS columns, each a
# value 0..SCOPE_LEVELS-1 (the sample at that point of the cycle) the plugin reads off the loaded table
SCOPE_COLS, SCOPE_LEVELS = 32, 24
for n in range(1, SLOTS + 1):
    for c in range(SCOPE_COLS):
        out.append({"key": "m%d_ws%d" % (n, c), "name": "M%d SCOPE %d" % (n, c + 1), "min": 0,
                    "max": SCOPE_LEVELS - 1, "default": SCOPE_LEVELS // 2, "display": "int", "clamped": True})
h2 = open(os.path.join(ROOT, "src/dsp/params.h")).read().replace(
    "#endif /* NORTHWEST_PARAMS_H */",
    "#define NW_SCOPE_COLS %d\n#define NW_SCOPE_LEVELS %d\n\n#endif /* NORTHWEST_PARAMS_H */" % (SCOPE_COLS, SCOPE_LEVELS))
open(os.path.join(ROOT, "src/dsp/params.h"), "w").write(h2)
json.dump({"name": "NorthWest", "params": out}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
open(os.path.join(ROOT, "params.lock"), "w").write("\n".join(p["key"] for p in out) + "\n")
print("engine params %d | VST params %d" % (len(P), len(out)))
