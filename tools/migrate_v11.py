#!/usr/bin/env python3
"""NorthWest v1.1: rewrite TBLR2 preset lines saved before the cleanup.

    python3 tools/migrate_v11.py presets/wavetablor.tbl      (rewrites the file in place)

What changes:
  * voice_mode / voices dropped (one monophonic voice); legato=1 -> glide_mode=3 (LEGATO);
  * me1_dst/me1_amt, me2_dst/me2_amt -> a free matrix slot  EG1/EG2 -> destination x amount;
  * matrix destinations renumbered (the four Bend/Formant entries are gone);
  * wtN_bend / wtN_formant -> effect mode WARP (FX1 bend, FX2 formant) when the oscillator has no
    effect yet; only the upper half of the old knobs survives (bend and formant are 0..1 now)."""
import sys

OLD_DST = ["None", "WT1 Pos", "WT2 Pos", "WT1 Level", "WT2 Level", "WT1 Tune", "WT2 Tune", "WT1 Bend", "WT2 Bend",
           "WT1 Formant", "WT2 Formant", "WT1 Pan", "WT2 Pan", "Filter Freq", "Filter Res", "Sub Level",
           "Noise Level", "Amp", "Pitch", "WT1 FX1", "WT1 FX2", "WT2 FX1", "WT2 FX2", "Uni Detune",
           "Filter2 Freq", "Filter2 Res"]
GONE = {"WT1 Bend": "WT1 FX1", "WT2 Bend": "WT2 FX1", "WT1 Formant": "WT1 FX2", "WT2 Formant": "WT2 FX2"}
NEW_DST = [d for d in OLD_DST if d not in GONE]

def dst(old):
    name = OLD_DST[int(float(old))]
    return NEW_DST.index(GONE.get(name, name))

def migrate(blob):
    tag, _, body = blob.partition(";")
    kv = {}
    for item in body.split(";"):
        if "=" in item:
            k, v = item.split("=", 1); kv[k] = v
    out = dict(kv)
    for k in ("voice_mode", "voices", "legato"): out.pop(k, None)
    if float(kv.get("legato", 0)) >= 0.5: out["glide_mode"] = "3"
    for m in range(1, 9):
        if "mm%d_dst" % m in out: out["mm%d_dst" % m] = str(dst(out["mm%d_dst" % m]))
    for e in (1, 2):
        d, a = out.pop("me%d_dst" % e, None), out.pop("me%d_amt" % e, None)
        if d is None or int(float(d)) == 0: continue
        free = next((m for m in range(1, 9) if int(float(out.get("mm%d_src" % m, 0))) == 0), None)
        if free is None:
            print("  no free matrix slot for EG%d" % e, file=sys.stderr); continue
        out["mm%d_src" % free], out["mm%d_dst" % free] = str(e), str(dst(d))
        if a is not None: out["mm%d_amt" % free] = a
    for o in (1, 2):
        b, f = out.pop("wt%d_bend" % o, None), out.pop("wt%d_formant" % o, None)
        if (b is None and f is None) or int(float(out.get("wt%d_fx_mode" % o, 0))) != 0: continue
        up = lambda v: 0 if v is None else max(0, round((float(v) - 64) / 63 * 127))
        if up(b) or up(f):
            out["wt%d_fx_mode" % o] = "4"
            if up(b): out["wt%d_fx1" % o] = str(up(b))
            if up(f): out["wt%d_fx2" % o] = str(up(f))
    return tag + ";" + "".join("%s=%s;" % kv for kv in out.items())

if __name__ == "__main__":
    fn = sys.argv[1]
    lines = open(fn).read().split("\n")
    res = []
    for ln in lines:
        if "|TBLR" in ln and not ln.startswith("#"):
            name, blob = ln.split("|", 1)
            ln = name + "|" + migrate(blob)
        elif ln.startswith("# WaveTablor factory"):
            ln = ln.replace("# WaveTablor factory", "# NorthWest factory")
        res.append(ln)
    open(fn, "w").write("\n".join(res))
    print("migrated", fn)
