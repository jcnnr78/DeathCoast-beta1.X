#!/usr/bin/env python3
"""DeathCoast: draws the skin's own images into images/dc/ (run before gen_layout.py; needs Pillow + numpy).

    knob.png, knob_main.png  the knobs (tools/knob_art.py): pointer for most, fluted for the main controls
    bar_WxH.png           a slider: a long box that fills with the accent colour (128 frames)
    wavebox_WxH.png       the WAVETABLE window's box
    flt_<type>_WxH.png    FILTER: the response of each type, following CUTOFF, RESONANCE and SHAPE as the plugin
                          encodes them (src/dsp/filters.h filterView)
    env_<a|d|s|r>_WxH.png AMP / FILTER: one small picture per envelope stage, each following its own knob (64 frames)
    fold_WxH.png          WEST COAST: a cycle through the wavefolder (FOLD x SYMMETRY)
    panel_<name>.png      the panels that set a group of controls apart (SUB, PITCH, LPG)
    logo.png              "DeathCoast" in thin letters

Run:  python3 tools/gen_art.py"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "images", "dc")
SS = 4                                    # supersampling

def hexc(h, a=255): return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)
ACC, ACC_HI, INK, INK_FAINT = hexc("c3c8d0"), hexc("6fd3c7"), hexc("e8e6e1"), hexc("8c8a85")
LINE, DISP_BG, DISC, DISC_EDGE = hexc("3d3d42"), hexc("121214"), hexc("2f2f33"), hexc("4a4a50")

def canvas(w, h, bg=(0, 0, 0, 0)): return Image.new("RGBA", (w * SS, h * SS), bg)
def down(im, w, h): return im.resize((w, h), Image.LANCZOS)

def strip(frames, path):
    w, h = frames[0].size
    s = Image.new("RGBA", (w, h * len(frames)))
    for i, f in enumerate(frames): s.paste(f, (0, i * h))
    s.save(os.path.join(OUT, path), optimize=True)
    print("  %-26s %dx%d x %d frames (%d px)" % (path, w, h, len(frames), h * len(frames)))

def polyline(d, pts, fill, width):
    d.line(pts, fill=fill, width=width, joint="curve")
    r = width / 2
    for x, y in (pts[0], pts[-1]): d.ellipse((x - r, y - r, x + r, y + r), fill=fill)

def display(w, h, radius=10):
    im = canvas(w, h)
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius * SS, fill=DISP_BG, outline=LINE, width=SS)
    return im

# ---------------------------------------------------------------------------------------------------- knob
def knob(n=128, size=72):
    """the two knobs (tools/knob_art.py): knob.png the pointer one, knob_main.png the fluted one for the main
    controls; 128 frames each"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import knob_art
    strip([knob_art.pointer(i / (n - 1), size) for i in range(n)], "knob.png")
    strip([knob_art.fluted(i / (n - 1), size) for i in range(n)], "knob_main.png")

def panel(name, label, w, h):
    """a panel that sets a group of controls apart (one Q-Link column), its name in the accent colour"""
    from PIL import ImageFont
    im = canvas(w, h); d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), 10 * SS, fill=hexc("2f2f33"), outline=hexc("4a4a50"), width=SS)
    try: f = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-Regular.otf", 15 * SS)
    except OSError: f = ImageFont.load_default(size=15 * SS)
    d.text((12 * SS, 8 * SS), label, font=f, fill=ACC)
    im = down(im, w, h); im.save(os.path.join(OUT, "panel_%s.png" % name), optimize=True)
    print("  %-26s %dx%d" % ("panel_%s.png" % name, w, h))

def bar(w, h, n=128):
    """a slider as a box that fills from the left with the accent colour (the MPC's own sliders)"""
    frames = []
    for i in range(n):
        v = i / (n - 1)
        im = canvas(w, h); d = ImageDraw.Draw(im)
        d.rectangle((0, 0, w * SS - 1, h * SS - 1), fill=hexc("121214"), outline=hexc("4a4a50"), width=SS)   # square corners
        if v > 0.003:
            x1 = max(2 * SS, int((w * SS - 2 * SS) * v))
            d.rectangle((SS, SS, x1, h * SS - 1 - SS), fill=ACC)
        frames.append(down(im, w, h))
    strip(frames, "bar_%dx%d.png" % (w, h))

def bar_bipolar(w, h, n=128):
    """a two-way slider (the matrix AMOUNT): a box with a centre mark, filled from the centre to the value"""
    frames = []
    for i in range(n):
        v = i / (n - 1)
        im = canvas(w, h); d = ImageDraw.Draw(im)
        d.rectangle((0, 0, w * SS - 1, h * SS - 1), fill=hexc("121214"), outline=hexc("4a4a50"), width=SS)
        c = w * SS // 2
        x = SS + int((w * SS - 2 * SS) * v)
        if abs(i - 64) >= 1: d.rectangle((min(c, x), SS, max(c, x), h * SS - 1 - SS), fill=ACC)
        d.line(((c, SS), (c, h * SS - 1 - SS)), fill=hexc("6fd3c7"), width=SS)
        frames.append(down(im, w, h))
    strip(frames, "barbi_%dx%d.png" % (w, h))

def arrows():
    """the arrows of the steppers shown for one model only (gen_layout.py fix_arrows draws them on the page), as the
    skin draws a stepper's own: an accent triangle on the panel; and the patch that hides one"""
    for side in ("prev", "next"):
        im = canvas(44, 44); d = ImageDraw.Draw(im)
        c, hw, hh = 22 * SS, 6 * SS, 10 * SS
        pts = [(c + hw, c - hh), (c + hw, c + hh), (c - hw, c)] if side == "prev" else [(c - hw, c - hh), (c - hw, c + hh), (c + hw, c)]
        d.polygon(pts, fill=hexc("6fd3c7"))
        im = down(im, 44, 44); im.save(os.path.join(OUT, "arrow_%s.png" % side), optimize=True)
    Image.new("RGBA", (44, 44), hexc("28282b")).save(os.path.join(OUT, "patch.png"))
    print("  arrow_prev.png, arrow_next.png, patch.png")

def wavebox(w, h):
    im = display(w, h, 10); d = ImageDraw.Draw(im)
    d.line(((10 * SS, h / 2 * SS), ((w - 10) * SS, h / 2 * SS)), fill=hexc("3d3d42", 200), width=SS)
    for q in (0.25, 0.75):
        d.line(((10 * SS, (8 + (h - 16) * q) * SS), ((w - 10) * SS, (8 + (h - 16) * q) * SS)), fill=hexc("3d3d42", 90), width=SS)
    im = down(im, w, h); im.save(os.path.join(OUT, "wavebox_%dx%d.png" % (w, h)), optimize=True)
    print("  %-26s %dx%d" % ("wavebox_%dx%d.png" % (w, h), w, h))

def fold(w, h):
    """WEST COAST: one cycle of the carrier through the wavefolder. 128 frames = FOLD in 16 steps x SYMMETRY in 8
    (frame = fold * 8 + sym), the value the plugin reports as mN_cview; the same law as WcMachine."""
    frames = []
    ph = np.linspace(0, 1, 200)
    for i in range(128):
        fo, sy = (i // 8) / 15, (i % 8) / 7
        x = np.sin(2 * np.pi * ph) * (1 + fo * 7) + sy * 0.8
        y = np.sin(np.pi / 2 * x) if (fo > 0 or sy > 0) else x
        im = display(w, h, 8); d = ImageDraw.Draw(im)
        d.line(((10 * SS, h / 2 * SS), ((w - 10) * SS, h / 2 * SS)), fill=hexc("3d3d42", 160), width=SS)
        pts = [((10 + (w - 20) * p) * SS, (h / 2 - (h / 2 - 9) * v) * SS) for p, v in zip(ph, y)]
        glow = canvas(w, h); gd = ImageDraw.Draw(glow)
        polyline(gd, pts, hexc("c3c8d0", 130), int(6 * SS))
        im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(3 * SS)))
        polyline(d, pts, ACC_HI, int(2.2 * SS))
        frames.append(down(im, w, h))
    strip(frames, "fold_%dx%d.png" % (w, h))

# ---------------------------------------------------------------------------------------------- filters
# the plugin's filter types, in its order (tools/gen_params.py FLT_TYPES); a strip each
FLT_TYPES = ["multi", "lp4", "combneg", "combpos", "eq", "legacylp", "legacyhp"]

def _svf(f, fc, q):
    s = f / fc
    lp = 1 / np.sqrt((1 - s ** 2) ** 2 + (s / q) ** 2)
    return lp, (s / q) * lp, s ** 2 * lp                       # low, band (unity peak), high

def _curve(kind, f, cut, res, shape, fs=44100.0):
    """the magnitude, same laws as src/dsp/filters.h (cut, res, shape on 0..1)"""
    fc = 440.0 * 2 ** ((8 + cut * 127 - 69) / 12)
    if kind == "multi":
        q = 0.6 * 40 ** res; lp, bp, hp = _svf(f, fc, q)
        wl = 1 - 2 * shape if shape < 0.5 else 0.0; wb = 2 * shape if shape < 0.5 else 2 - 2 * shape
        wh = 0.0 if shape < 0.5 else 2 * shape - 1
        return np.abs(wl * lp + wb * bp + wh * hp)
    if kind == "lp4":
        k = res * 4.3
        s = 1j * f / fc
        h = 1 / ((1 + s) ** 4 + k)
        return np.abs(h) * (1 + 0.45 * k) * (1 + k) / (1 + k)
    if kind in ("combneg", "combpos"):
        fb = (1 if kind == "combpos" else -1) * 0.985 * res ** 0.6
        D = fs / max(fc, 12.0)
        w = 2 * np.pi * f / fs
        h = 1 / (1 - fb * np.exp(-1j * w * D))
        return np.abs(h) * np.sqrt(1 - abs(fb))
    if kind == "eq":
        A = 10 ** (((shape * 127 - 64) / 63 * 18) / 40); q = 0.5 * 16 ** res
        s = 1j * f / fc
        return np.abs((s ** 2 + s * (A / q) + 1) / (s ** 2 + s / (A * q) + 1))
    q = min(8.0, 0.7071 / (1 - res * 0.99)); lp, bp, hp = _svf(f, fc, q)
    return lp if kind == "legacylp" else hp

def filt(kind, w, h):
    """frames as src/dsp/filters.h filterView encodes them, 192 for every type so the frame is the FILTER VIEW value
    itself (a meter spreads its parameter's 0..191 over the strip): MULTI (CUTOFF 16 x MORPH 4 x RESONANCE 3), EQ
    (CUTOFF 24 x GAIN 8), the others (CUTOFF 48 x RESONANCE 4)"""
    frames = []
    f = np.geomspace(20, 20000, 260)
    if kind == "multi": combos = [(c / 15, m / 3, r / 2) for c in range(16) for m in range(4) for r in range(3)]
    elif kind == "eq": combos = [(c / 23, 0.5, g / 7) for c in range(24) for g in range(8)]
    else: combos = [(c / 47, r / 3, 0.0) for c in range(48) for r in range(4)]
    for cut, res, shape in combos:
        if kind == "eq": res, shape = 0.4, shape
        r = _curve(kind, f, cut, min(res, 0.97 if kind.startswith("comb") else 1.0), shape)
        db = 20 * np.log10(np.maximum(r, 1e-6))
        top, bot = 18.0, -42.0
        im = display(w, h); d = ImageDraw.Draw(im)
        for g in (100, 1000, 10000):             # faint decade lines
            x = (10 + (w - 20) * math.log(g / 20) / math.log(1000)) * SS
            d.line(((x, 6 * SS), (x, (h - 6) * SS)), fill=hexc("3d3d42", 120), width=SS)
        y0 = (6 + (h - 12) * top / (top - bot)) * SS
        d.line(((10 * SS, y0), ((w - 10) * SS, y0)), fill=hexc("3d3d42", 160), width=SS)
        xs = 10 + (w - 20) * np.log(f / 20) / math.log(1000)
        ys = 6 + (h - 12) * (top - np.clip(db, bot, top)) / (top - bot)
        pts = [(x * SS, y * SS) for x, y in zip(xs, ys)]
        fill = canvas(w, h); fd = ImageDraw.Draw(fill)
        fd.polygon(pts + [(pts[-1][0], (h - 6) * SS), (pts[0][0], (h - 6) * SS)], fill=hexc("c3c8d0", 55))
        im.alpha_composite(fill)
        polyline(d, pts, ACC_HI, int(2.2 * SS))
        frames.append(down(im, w, h))
    strip(frames, "flt_%s_%dx%d.png" % (kind, w, h))

# ---------------------------------------------------------------------------------------------- envelope stages
def env(stage, w, h, n=64):
    frames = []
    L, R, T, B = 8, w - 8, 7, h - 7
    mid = 0.5                                     # the generic sustain level drawn by DECAY and RELEASE
    for i in range(n):
        v = i / (n - 1)
        im = display(w, h, 8); d = ImageDraw.Draw(im)
        X = lambda u: (L + (R - L) * u) * SS
        Y = lambda lvl: (B - (B - T) * lvl) * SS
        span = 0.06 + 0.94 * v                    # longer time = the change takes more of the width
        u = np.linspace(0, 1, 90)
        if stage == "a":
            lvl = np.where(u < span, 1 - np.clip(1 - u / span, 0, 1) ** 1.6, 1.0)
        elif stage == "d":
            lvl = np.where(u < span, mid + (1 - mid) * np.exp(-4.5 * u / span), mid)
        elif stage == "s":
            lvl = np.full_like(u, v)
        else:
            lvl = np.where(u < span, mid * np.exp(-4.5 * u / span), 0.0)
        pts = [(X(a), Y(b)) for a, b in zip(u, lvl)]
        if stage in ("d", "r"):                   # the generic level, dashed
            for a in np.arange(0, 1, 0.08):
                d.line(((X(a), Y(mid)), (X(min(1, a + 0.04)), Y(mid))), fill=hexc("8c8a85", 110), width=SS)
        fill = canvas(w, h); fd = ImageDraw.Draw(fill)
        fd.polygon(pts + [(X(1), Y(0)), (X(0), Y(0))], fill=hexc("c3c8d0", 50))
        im.alpha_composite(fill)
        polyline(d, pts, ACC_HI, int(2.4 * SS))
        frames.append(down(im, w, h))
    strip(frames, "env_%s_%dx%d.png" % (stage, w, h))

# ---------------------------------------------------------------------------------------------- logo
LOGO_FONT = "/usr/share/fonts/opentype/inter/Inter-ExtraLight.otf"   # Inter (OFL); only the PNG ships

def logo(size=64):
    """'DeathCoast' in thin letters, the D and the C in the accent colour. Needs the font above; without it the committed
    images/dc/logo.png is kept as it is."""
    from PIL import ImageFont
    if not os.path.exists(LOGO_FONT):
        print("  logo.png                   kept (font not found)"); return
    font = ImageFont.truetype(LOGO_FONT, size * SS)
    parts = [("D", ACC), ("eath", INK), ("C", ACC), ("oast", INK)]
    w = sum(font.getlength(t) for t, _ in parts) + 20 * SS
    im = Image.new("RGBA", (int(w), int(size * 1.4 * SS)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    x = 10 * SS
    for t, c in parts:
        d.text((x, 4 * SS), t, font=font, fill=c); x += font.getlength(t)
    im = im.resize((im.width // SS, im.height // SS), Image.LANCZOS)
    bb = im.getbbox(); im = im.crop((bb[0] - 4, bb[1] - 4, bb[2] + 4, bb[3] + 4))
    im.save(os.path.join(OUT, "logo.png"), optimize=True)
    print("  logo.png                   %dx%d" % im.size)

# sizes used by tools/gen_layout.py
FOLD = (360, 96)
FLT = (220, 56)
ENV = (96, 50)
BARS = [(186, 22), (130, 20), (92, 18)]       # MAIN sends; SUB / PITCH panels; FILTER base-width
WAVEBOX = (460, 128)                          # the WAVETABLE window round the table picture
PANELS = {"sub": ("SUB", 472, 128), "pitch": ("PITCH", 472, 240), "lpg": ("LPG", 472, 240)}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    knob()
    for b in BARS: bar(*b)
    bar_bipolar(400, 30)
    wavebox(*WAVEBOX)
    arrows()
    fold(*FOLD)
    for k in FLT_TYPES: filt(k, *FLT)
    for s in "adsr": env(s, *ENV)
    for name, (label, w, h) in PANELS.items(): panel(name, label, w, h)
    logo()
