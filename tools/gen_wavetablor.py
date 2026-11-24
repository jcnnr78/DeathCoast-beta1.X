#!/usr/bin/env python3
"""WaveTablor parameter generator.

Takes the upstream Tablor parameter files (tools/baseline/) and adds the
WaveTablor parameters on top, writing src/dsp/params.h, params.json,
params.base.json and params.lock. Run from the project root:

    python3 tools/gen_wavetablor.py

Rules that keep old presets and projects working:
  * existing parameters keep their keys, order and defaults;
  * existing option lists are only ever extended at the end;
  * new parameters are appended (VST parameter index = order in params.json).
"""
import json, re, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "tools", "baseline")

# NorthWest v1.1: Classic/Modern renamed for what they do; BEND + FORMANT became the WARP mode.
FX_MODES  = ["None", "FM", "Sync", "Fold", "Warp"]
UNI_MODES = ["Classic", "Shimmer", "Noise", "Phase Sync", "Pos Spread", "Random"]
FLT2_TYPES = ["Off", "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch 12", "Notch 24"]
ROUTES = ["Serial", "Parallel", "Split"]
LFO_SHAPES = ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd"]
LFO_SYNC = ["Free", "Sync"]
LFO_RETRIG = ["Key", "Free"]
OLD_DST = ["None", "WT1 Pos", "WT2 Pos", "WT1 Level", "WT2 Level", "WT1 Tune", "WT2 Tune", "WT1 Bend", "WT2 Bend",
           "WT1 Formant", "WT2 Formant", "WT1 Pan", "WT2 Pan", "Filter Freq", "Filter Res", "Sub Level", "Noise Level", "Amp"]
DST = OLD_DST + ["Pitch", "WT1 FX1", "WT1 FX2", "WT2 FX1", "WT2 FX2", "Uni Detune", "Filter2 Freq", "Filter2 Res"]
SRC = ["None", "EG1", "EG2", "LFO1", "LFO2", "Velocity", "Key", "Mod Wheel", "Aftertouch", "Pitch Bend",
       "Random", "Amp Env", "Filter Env"]

# (key, kind, min, max, default, options, vst name)
NEW = []
def I(k, name, lo, hi, d): NEW.append((k, "int", lo, hi, d, None, name))
def E(k, name, opts, d=0): NEW.append((k, "enum", 0, len(opts) - 1, d, opts, name))

for o in (1, 2):
    E(f"wt{o}_fx_mode", f"WT{o} FX MODE", FX_MODES)
    I(f"wt{o}_fx1", f"WT{o} FX1", 0, 127, 0)
    I(f"wt{o}_fx2", f"WT{o} FX2", 0, 127, 0)
    E(f"wt{o}_uni_mode", f"WT{o} UNI MODE", UNI_MODES)
E("flt2_type", "FILTER2 TYPE", FLT2_TYPES, 0)
I("flt2_freq", "FILTER2 FREQ", 0, 127, 100)
I("flt2_res", "FILTER2 RES", 0, 127, 0)
I("flt2_env", "FILTER2 ENV", 0, 127, 64)
I("flt2_key", "FILTER2 KEY", 0, 127, 0)
E("flt_route", "FILTER ROUTE", ROUTES)
for n in (1, 2):
    E(f"lfo{n}_shape", f"LFO{n} SHAPE", LFO_SHAPES)
    E(f"lfo{n}_sync", f"LFO{n} SYNC", LFO_SYNC)
    I(f"lfo{n}_rate", f"LFO{n} RATE", 0, 127, 64)
    I(f"lfo{n}_fade", f"LFO{n} FADE", 0, 127, 0)
    E(f"lfo{n}_retrig", f"LFO{n} RETRIG", LFO_RETRIG)
for m in range(1, 9):
    E(f"mm{m}_src", f"MOD{m} SOURCE", SRC)
    E(f"mm{m}_dst", f"MOD{m} DEST", DST)
    I(f"mm{m}_amt", f"MOD{m} AMOUNT", 0, 127, 64)

def cname(s): return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")

# ---------------------------------------------------------------- params.h
h = open(os.path.join(B, "params.h")).read()
# 1. counts
old_n = int(re.search(r"#define TB_PARAM_COUNT (\d+)", h).group(1))
new_n = old_n + len(NEW)
h = re.sub(r"#define TB_PARAM_COUNT \d+", f"#define TB_PARAM_COUNT {new_n}", h)
h = h.replace('#define TB_VERSION "1.3.3"', '#define TB_VERSION "1.3.3-wt1"')
# 2. enum
enum_add = "".join(f"    TB_P_{k.upper()} = {old_n + i},\n" for i, (k, *_r) in enumerate(NEW))
h = h.replace("    TB_P_VOLUME = 58,\n};", "    TB_P_VOLUME = 58,\n" + enum_add + "};")
# 3. option arrays: extend dst list (opts_3), append new arrays
dst_c = ", ".join(f'"{x}"' for x in DST)
h = re.sub(r"static const char \*const tb_opts_3\[\] = \{[^}]*\};", f"static const char *const tb_opts_3[] = {{ {dst_c} }};", h)
arrs = {}
def optname(opts):
    key = tuple(opts)
    if key not in arrs: arrs[key] = f"tb_opts_wt{len(arrs)}"
    return arrs[key]
rows = []
for k, kind, lo, hi, d, opts, _n in NEW:
    if kind == "enum":
        rows.append(f'    {{ "{k}", TB_ENUM, {lo}, {hi}, {d}, {len(opts)}, {optname(opts)} }},')
    else:
        rows.append(f'    {{ "{k}", TB_INT, {lo}, {hi}, {d}, 0, 0 }},')
arr_c = "".join("static const char *const %s[] = { %s };\n" % (v, ", ".join(f'"{x}"' for x in k)) for k, v in arrs.items())
h = h.replace("\nstatic const tb_param_t tb_params[TB_PARAM_COUNT] = {", "\n" + arr_c + "\nstatic const tb_param_t tb_params[TB_PARAM_COUNT] = {")
# me dst enum count (18 -> len(DST)); unison range 1..4 -> 1..6
h = h.replace('{ "me1_dst", TB_ENUM, 0, 17, 0, 18, tb_opts_3 }', f'{{ "me1_dst", TB_ENUM, 0, {len(DST)-1}, 0, {len(DST)}, tb_opts_3 }}')
h = h.replace('{ "me2_dst", TB_ENUM, 0, 17, 0, 18, tb_opts_3 }', f'{{ "me2_dst", TB_ENUM, 0, {len(DST)-1}, 0, {len(DST)}, tb_opts_3 }}')
for o in (1, 2):
    h = h.replace(f'{{ "wt{o}_uni", TB_INT, 1, 4, 1, 0, 0 }}', f'{{ "wt{o}_uni", TB_INT, 1, 6, 1, 0, 0 }}')
h = h.replace('    { "volume", TB_INT, 0, 127, 100, 0, 0 },\n};', '    { "volume", TB_INT, 0, 127, 100, 0, 0 },\n' + "\n".join(rows) + "\n};")
# 4. chain_params json string
m = re.search(r'(tb_chain_params_json =\n    ")(.*?)(";\n)', h, re.S)
cp = json.loads(bytes(m.group(2), "utf-8").decode("unicode_escape"))
for e in cp:
    if e["key"] in ("me1_dst", "me2_dst"): e["options"] = DST
    if e["key"] in ("wt1_uni", "wt2_uni"): e["max"] = 6
for k, kind, lo, hi, d, opts, nm in NEW:
    if kind == "enum": cp.append({"key": k, "name": nm.title(), "type": "enum", "options": opts, "default": opts[d]})
    else: cp.append({"key": k, "name": nm.title(), "type": "int", "min": lo, "max": hi, "default": d})
js = json.dumps(cp, separators=(",", ":")).replace("\\", "\\\\").replace('"', '\\"')
h = h[:m.start(2)] + js + h[m.end(2):]
open(os.path.join(ROOT, "src/dsp/params.h"), "w").write(h)

# ------------------------------------------------------------ params.json
def vst_entry(k, kind, lo, hi, d, opts, nm):
    if kind == "enum": return {"key": k, "name": nm, "options": [o.upper() for o in opts], "default": d}
    return {"key": k, "name": nm, "min": lo, "max": hi, "default": d, "display": "int"}
for fn in ("params.json", "params.base.json"):
    j = json.load(open(os.path.join(B, fn)))
    for p in j["params"]:
        if p["key"] in ("me1_dst", "me2_dst"): p["options"] = [o.upper() for o in DST] if fn == "params.json" else DST
        if p["key"] in ("wt1_uni", "wt2_uni"): p["max"] = 6
    for e in NEW:
        ent = vst_entry(*e)
        if fn == "params.base.json":
            ent["name"] = e[6].title()
        j["params"].append(ent)
    json.dump(j, open(os.path.join(ROOT, fn), "w"), indent=1)
lock = open(os.path.join(B, "params.lock")).read().rstrip("\n").split("\n")
lock += [e[0] for e in NEW]
open(os.path.join(ROOT, "params.lock"), "w").write("\n".join(lock) + "\n")
print("params:", old_n, "->", new_n, "| VST params:", len(lock))

# ===================================================================== NorthWest v1.1 cleanup
# Each slot is monophonic from now on and the envelopes are routed only through the matrix, so the
# parameters below go away. NorthWest is a new plugin (uid NWst): no project or preset of it predates
# this, and the state blob is key=value, so an older blob simply skips the keys it no longer finds.
REMOVE = ["voice_mode", "voices", "legato",                       # mono slot: GLIDE MODE covers legato
          "me1_dst", "me1_amt", "me2_dst", "me2_amt",             # EG1/EG2 only through the matrix
          "wt1_bend", "wt1_formant", "wt2_bend", "wt2_formant"]   # -> effect mode WARP (FX1 bend, FX2 formant)
DST_GONE = ["WT1 Bend", "WT2 Bend", "WT1 Formant", "WT2 Formant"]
DST2 = [d for d in DST if d not in DST_GONE]
GLIDE_MODES = ["Off", "Glissando", "Portamento", "Legato"]

h = open(os.path.join(ROOT, "src/dsp/params.h")).read()
# rows of tb_params, in order
body = re.search(r"(static const tb_param_t tb_params\[TB_PARAM_COUNT\] = \{\n)(.*?)(\n\};)", h, re.S)
rows = [r for r in body.group(2).split("\n") if r.strip()]
rows = [r for r in rows if re.search(r'\{ "([a-z0-9_]+)"', r).group(1) not in REMOVE]
keys = [re.search(r'\{ "([a-z0-9_]+)"', r).group(1) for r in rows]
dst_arr = re.search(r'\{ "mm1_dst", TB_ENUM, 0, \d+, 0, \d+, (\w+) \}', h).group(1)
rows = [re.sub(r'(\{ "mm\d_dst", TB_ENUM, 0, )\d+(, 0, )\d+', r"\g<1>%d\g<2>%d" % (len(DST2) - 1, len(DST2)), r) for r in rows]
rows = [re.sub(r'(\{ "glide_mode", TB_ENUM, 0, )\d+(, 0, )\d+', r"\g<1>%d\g<2>%d" % (len(GLIDE_MODES) - 1, len(GLIDE_MODES)), r) for r in rows]
h = h[:body.start(2)] + "\n".join(rows) + h[body.end(2):]
h = re.sub(r"static const char \*const %s\[\] = \{[^}]*\};" % dst_arr,
           "static const char *const %s[] = { %s };" % (dst_arr, ", ".join('"%s"' % x for x in DST2)), h)
h = re.sub(r"static const char \*const tb_opts_5\[\] = \{[^}]*\};",
           "static const char *const tb_opts_5[] = { %s };" % ", ".join('"%s"' % x for x in GLIDE_MODES), h)
# the me*_dst list (tb_opts_3) is now unused; keep the array out so the compiler doesn't warn
h = re.sub(r"static const char \*const tb_opts_3\[\] = \{[^}]*\};\n", "", h)
h = re.sub(r"static const char \*const tb_opts_4\[\] = \{[^}]*\};\n", "", h)
h = re.sub(r"static const char \*const tb_opts_6\[\] = \{[^}]*\};\n", "", h)
# enum, renumbered
en = re.search(r"enum \{\n(    TB_P_.*?)\n\};", h, re.S)
h = h[:en.start(1)] + "\n".join("    TB_P_%s = %d," % (k.upper(), i) for i, k in enumerate(keys)) + h[en.end(1):]
h = re.sub(r"#define TB_PARAM_COUNT \d+", "#define TB_PARAM_COUNT %d" % len(keys), h)
h = re.sub(r'#define TB_VERSION "[^"]*"', '#define TB_VERSION "northwest-1.1"', h)
# the two JSON strings (chain_params for hosts, ui_hierarchy for Move's stock UI)
def edit_json(h, var, fn):
    m = re.search(r'(%s =\n    ")(.*?)(";\n)' % var, h, re.S)
    j = json.loads(bytes(m.group(2), "utf-8").decode("unicode_escape"))
    j = fn(j)
    js = json.dumps(j, separators=(",", ":")).replace("\\", "\\\\").replace('"', '\\"')
    return h[:m.start(2)] + js + h[m.end(2):]
def fix_entry(e):
    if e.get("key", "").endswith("_dst") and e["key"].startswith("mm"): e["options"] = DST2
    if e.get("key") in ("wt1_fx_mode", "wt2_fx_mode"): e["options"] = FX_MODES
    if e.get("key") == "glide_mode": e["options"] = GLIDE_MODES
    if e.get("key") in ("wt1_uni", "wt2_uni"): e["max"] = 6
    return e
h = edit_json(h, "tb_chain_params_json", lambda j: [fix_entry(e) for e in j if e["key"] not in REMOVE])
def hier(j):
    for lv in j["levels"].values():
        lv["params"] = [fix_entry(p) for p in lv["params"] if p.get("key") not in REMOVE]
        lv["knobs"] = [k for k in lv.get("knobs", []) if k not in REMOVE]
    return j
h = edit_json(h, "tb_ui_hierarchy_json", hier)
open(os.path.join(ROOT, "src/dsp/params.h"), "w").write(h)

for fn in ("params.json", "params.base.json"):
    j = json.load(open(os.path.join(ROOT, fn)))
    up = fn == "params.json"
    j["params"] = [p for p in j["params"] if p["key"] not in REMOVE]
    for p in j["params"]:
        if p["key"].startswith("mm") and p["key"].endswith("_dst"): p["options"] = [o.upper() for o in DST2] if up else DST2
        if p["key"] in ("wt1_fx_mode", "wt2_fx_mode"): p["options"] = [o.upper() for o in FX_MODES] if up else FX_MODES
        if p["key"] == "glide_mode": p["options"] = [o.upper() for o in GLIDE_MODES] if up else GLIDE_MODES
    json.dump(j, open(os.path.join(ROOT, fn), "w"), indent=1)
lock = [k for k in open(os.path.join(ROOT, "params.lock")).read().split() if k not in REMOVE]
open(os.path.join(ROOT, "params.lock"), "w").write("\n".join(lock) + "\n")
print("NorthWest v1.1: removed", len(REMOVE), "-> engine params", len(keys), "| VST params", len(lock))
