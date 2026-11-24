#!/usr/bin/env python3
"""NorthWest v2.0: rewrite a v1.x preset line (TBLR2;key=value;...) for the 4-slot surface.

    python3 tools/migrate_v20.py presets/wavetablor.tbl      (rewrites the file in place)

Oscillator 1 becomes slot 1 (WAVETABLE), oscillator 2 becomes slot 2 (WAVETABLE) when its level was up; the
amp envelope and filter 1 are copied to each slot used. SUB, NOISE, FILTER 2 and the filter envelope's
attack/sustain have no place in v2.0 and are dropped. Matrix destinations are translated (with a target slot)."""
import sys

OLD_DST = ["None", "WT1 Pos", "WT2 Pos", "WT1 Level", "WT2 Level", "WT1 Tune", "WT2 Tune", "WT1 Pan", "WT2 Pan",
           "Filter Freq", "Filter Res", "Sub Level", "Noise Level", "Amp", "Pitch", "WT1 FX1", "WT1 FX2", "WT2 FX1",
           "WT2 FX2", "Uni Detune", "Filter2 Freq", "Filter2 Res"]
NEW_DST = ["NONE", "PITCH", "LEVEL", "PAN", "CUTOFF", "RESONANCE", "TIMBRE A", "TIMBRE B", "TIMBRE C", "TIMBRE D"]
MAP = {"WT1 Pos": ("TIMBRE A", 1), "WT2 Pos": ("TIMBRE A", 2), "WT1 Level": ("LEVEL", 1), "WT2 Level": ("LEVEL", 2),
       "WT1 Tune": ("PITCH", 1), "WT2 Tune": ("PITCH", 2), "WT1 Pan": ("PAN", 1), "WT2 Pan": ("PAN", 2),
       "Filter Freq": ("CUTOFF", 0), "Filter Res": ("RESONANCE", 0), "Amp": ("LEVEL", 0), "Pitch": ("PITCH", 0),
       "WT1 FX1": ("TIMBRE B", 1), "WT1 FX2": ("TIMBRE C", 1), "WT2 FX1": ("TIMBRE B", 2), "WT2 FX2": ("TIMBRE C", 2),
       "Uni Detune": ("TIMBRE D", 0)}
OSC = {"level": "level", "pos": "k1", "tune": "tune", "pan": "pan", "detune": "k4", "spread": "k5",
       "fx_mode": "wt_fx", "fx1": "k2", "fx2": "k3", "uni_mode": "wt_umode", "table": "table"}
COMMON = {"vca_a": "amp_a", "vca_d": "amp_d", "vca_s": "amp_s", "vca_r": "amp_r", "vca_vel": "vel",
          "flt_freq": "cutoff", "flt_res": "reso", "flt_type": "ftype", "flt_env": "fenv", "flt_d": "fdec", "flt_key": "fkey"}
OLD_DEF = {"vca_a": "51", "vca_d": "64", "vca_s": "102", "vca_r": "64", "vca_vel": "100", "flt_freq": "127",
           "flt_res": "0", "flt_type": "1", "flt_env": "64", "flt_d": "64", "flt_key": "0"}   # v1 defaults
KEEP = ("me1_", "me2_", "lfo1_", "lfo2_", "glide", "pb_range", "volume")

def migrate(blob):
    tag, _, body = blob.partition(";")
    kv = dict(OLD_DEF)
    kv.update(dict(x.split("=", 1) for x in body.split(";") if "=" in x))
    out = {}
    slots = [1] + ([2] if float(kv.get("wt2_level", 0)) > 0 else [])
    for o in slots:
        out["s%d_machine" % o] = "0"
        if o == 2: out["s2_level"] = kv["wt2_level"]
        for k, v in kv.items():
            if k.startswith("wt%d_" % o):
                rest = k[4:]
                if rest == "uni":
                    out["s%d_k7" % o] = str(round((min(4, max(1, int(float(v)))) - 1) / 3 * 127))
                elif rest in OSC and not (o == 2 and rest == "level"):
                    out["s%d_%s" % (o, OSC[rest])] = v
            if k in COMMON:
                out["s%d_%s" % (o, COMMON[k])] = v
        out.setdefault("s%d_k7" % o, "0")          # v1 default was 1 voice
    for k, v in kv.items():
        if k.startswith(KEEP): out[k] = v
    for m in range(1, 9):
        src = kv.get("mm%d_src" % m)
        if not src or int(float(src)) == 0: continue
        name = OLD_DST[int(float(kv.get("mm%d_dst" % m, 0)))]
        if name not in MAP: continue
        d, sl = MAP[name]
        out["mm%d_src" % m] = src
        out["mm%d_dst" % m] = str(NEW_DST.index(d))
        out["mm%d_slot" % m] = str(sl)
        if "mm%d_amt" % m in kv: out["mm%d_amt" % m] = kv["mm%d_amt" % m]
    return "TBLR2;" + "".join("%s=%s;" % x for x in out.items())

if __name__ == "__main__":
    fn = sys.argv[1]
    res = []
    for ln in open(fn).read().split("\n"):
        if "|TBLR" in ln and not ln.startswith("#"):
            name, blob = ln.split("|", 1)
            ln = name + "|" + migrate(blob)
        res.append(ln)
    open(fn, "w").write("\n".join(res))
    print("migrated", fn)
