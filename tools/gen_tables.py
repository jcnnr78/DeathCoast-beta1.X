#!/usr/bin/env python3
"""DeathCoast: the WAVETABLE picture of every table the plugin ships, so the screen shows the table that is loaded.

    python3 tools/gen_tables.py [<tablor library wavetables folder>]

Reads this repo's wavetables/ and Tablor's library (the release's release/fetch-library.py fetches it; default
../tablor-src/src/wavetables or library/wavetables) and writes
    images/dc/tables_WxH.png   one frame per table: 0 = a table not in this list (the user's own), 1 = Init, then the
                               files by name; each frame draws a stack of the table's own waves
    src/dsp/table_view.h       the matching names (file names without extension), for the plugin to find the frame
Needs Pillow, numpy and (for the FLAC .wt2048 files) ffmpeg."""
import glob, math, os, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_art as A

SIZE = (440, 112)
DEPTH, DX, DY = 7, 4.0, 5.0     # the stack: frames drawn back to front, each this far up and right
FRAME = 2048

def load(path):
    if path.endswith(".wav"):
        w = wave.open(path)
        raw = w.readframes(w.getnframes()); ch = w.getnchannels(); sw = w.getsampwidth()
        a = np.frombuffer(raw, dtype={1: np.int8, 2: np.int16, 4: np.int32}[sw]).astype(np.float32)
        a = a.reshape(-1, ch)[:, 0]
    else:   # .wtNNNN: FLAC
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-"],
                             capture_output=True, check=True).stdout
        a = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
    n = len(a) // FRAME
    a = a[: n * FRAME].reshape(n, FRAME)
    return a / max(1e-9, np.abs(a).max())

def init_table():
    """src/dsp/engine.cpp makeInitTable(): 64 frames, pulse 50% -> 6% with a saw underlay"""
    F = 64; ph = np.arange(FRAME) / FRAME; out = []
    for f in range(F):
        m = f / (F - 1); pw = 0.5 - 0.44 * m
        out.append(0.8 * ((1 - m * 0.5) * np.where(ph < pw, 1.0, -1.0) * 0.6 + m * 0.4 * (2 * ph - 1)))
    a = np.array(out); return a / np.abs(a).max()

def draw(table, w, h):
    im = A.display(w, h, 8); d = ImageDraw.Draw(im)
    if table is None:                          # a table that isn't in the list: say so
        d.text((w * A.SS // 2, h * A.SS // 2), "USER TABLE", fill=A.INK_FAINT, anchor="mm",
               font=__import__("PIL.ImageFont", fromlist=["x"]).load_default(size=13 * A.SS))
        return A.down(im, w, h)
    depth, dx, dy = DEPTH, DX, DY
    pw = w - 20 - depth * dx; ph = (h - 14 - depth * dy) / 2
    n = len(table)
    for k in range(depth, -1, -1):             # the table's own frames, back (last) to front (first)
        fr = table[min(n - 1, round(k / depth * (n - 1)))]
        xs = np.linspace(0, FRAME - 1, 110).astype(int)
        ox, oy = 10 + k * dx, h / 2 + depth * dy / 2 - k * dy
        pts = [((ox + pw * i / 109) * A.SS, (oy - ph * fr[x]) * A.SS) for i, x in enumerate(xs)]
        a = int(110 + 90 * (1 - k / depth))                # even: the POSITION overlay marks the current one
        A.polyline(d, pts, A.hexc("c3c8d0", a), int(1.4 * A.SS))
    return A.down(im, w, h)

def posmark(w, h, n=64):
    """the POSITION overlay, drawn over the table picture (transparent): a bright cursor along the frame of the stack
    POSITION is on, with a dot where the wave starts. 64 frames, follows K1."""
    frames = []
    pw = w - 20 - DEPTH * DX; ph = (h - 14 - DEPTH * DY) / 2
    for i in range(n):
        k = i / (n - 1) * DEPTH
        ox, oy = 10 + k * DX, h / 2 + DEPTH * DY / 2 - k * DY
        im = A.canvas(w, h); d = ImageDraw.Draw(im)
        glow = A.canvas(w, h); gd = ImageDraw.Draw(glow)
        gd.line(((ox * A.SS, oy * A.SS), ((ox + pw) * A.SS, oy * A.SS)), fill=A.hexc("6fd3c7", 150), width=int(5 * A.SS))
        im.alpha_composite(glow.filter(__import__("PIL.ImageFilter", fromlist=["x"]).GaussianBlur(3 * A.SS)))
        d.line(((ox * A.SS, oy * A.SS), ((ox + pw) * A.SS, oy * A.SS)), fill=A.ACC_HI, width=int(1.6 * A.SS))
        r = 3.2 * A.SS
        d.ellipse((ox * A.SS - r, oy * A.SS - r, ox * A.SS + r, oy * A.SS + r), fill=A.INK)
        frames.append(A.down(im, w, h))
    A.strip(frames, "posmark_%dx%d.png" % (w, h))

def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else next(
        (p for p in (os.path.join(ROOT, "..", "tablor-src", "src", "wavetables"), os.path.join(ROOT, "library", "wavetables"))
         if os.path.isdir(p)), None)
    files = sorted(glob.glob(os.path.join(ROOT, "wavetables", "**", "*.wav"), recursive=True))
    if lib: files += sorted(glob.glob(os.path.join(lib, "**", "*.wt*"), recursive=True))
    else: print("warning: no Tablor library found; only this repo's tables get a picture")
    names, frames = [], [draw(None, *SIZE), draw(init_table(), *SIZE)]
    seen = set()
    for f in sorted(files, key=lambda p: os.path.basename(p).lower()):
        stem = os.path.splitext(os.path.basename(f))[0]
        if stem in seen: continue
        seen.add(stem); names.append(stem); frames.append(draw(load(f), *SIZE))
    posmark(*SIZE)
    # two strips (a strip may not pass 12288 px): A = blank + the first SPLIT pictures, B = blank + the rest; the plugin
    # shows a picture in one and the blank frame in the other (m<n>_wview, m<n>_wviewb)
    split = (len(frames) + 1) // 2
    blank = A.canvas(*SIZE).resize(SIZE)
    for name, part in (("a", frames[:split]), ("b", frames[split:])):
        if (len(part) + 1) * SIZE[1] > 12288: raise SystemExit("too many tables for a strip: %d" % len(part))
        A.strip([blank] + part, "tables%s_%dx%d.png" % ((name,) + SIZE))
    hdr = ["/* Generated by tools/gen_tables.py -- DO NOT EDIT. The tables drawn in images/dc/tablesa_* and tablesb_*:",
           " * picture 0 = a table not listed here, 1 = Init, 2 + i = kTableViewNames[i]; pictures 0..SPLIT-1 are",
           " * frames 1.. of strip a, the others frames 1.. of strip b; frame 0 of each strip is blank. */",
           "#pragma once", "", "#define DC_TABLE_VIEW_FRAMES %d" % len(frames), "#define DC_TABLE_VIEW_SPLIT %d" % split,
           "", "static const char *const kTableViewNames[] = {"]
    hdr += ['    "%s",' % n.replace("\\", "\\\\").replace('"', '\\"') for n in names] + ["};", ""]
    open(os.path.join(ROOT, "src", "dsp", "table_view.h"), "w").write("\n".join(hdr))
    print("tables: %d (+ user, Init)" % len(names))

if __name__ == "__main__":
    main()
