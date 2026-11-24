#!/usr/bin/env python3
"""NorthWest v1.2: draws the skin's own images into images/nw/ (run before gen_layout.py; needs Pillow + numpy).

    knob.png            the knob filmstrip: flat round disc, thin value ring, short pointer (128 frames, 72 px)
    wave_WxH.png        MAIN: the wavetable view that follows POSITION (64 frames; a generic morph, not the
                        selected table's real shape: MPC can only show pre-drawn pictures)
    flt_<type>_WxH.png  FILTER: the response of each filter type, its corner following CUTOFF / FREQ (64 frames;
                        resonance not drawn)
    env_<a|d|s|r>_WxH.png  ENVELOPES: one small picture per stage, each following its own knob (64 frames)
    logo.png            "NorthWest" in thin letters

Run:  python3 tools/gen_art.py"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "images", "nw")
SS = 4                                    # supersampling

def hexc(h, a=255): return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)
ACC, ACC_HI, INK, INK_FAINT = hexc("e8871e"), hexc("ffb347"), hexc("f2f0ec"), hexc("8c8a85")
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
    frames = []
    c = size * SS / 2
    for i in range(n):
        v = i / (n - 1)
        im = canvas(size, size); d = ImageDraw.Draw(im)
        r_ring, wr = 32 * SS, 3 * SS                           # thin ring: track + value arc, 270 degrees
        box = (c - r_ring, c - r_ring, c + r_ring, c + r_ring)
        d.arc(box, 135, 405, fill=LINE, width=wr)
        if v > 0.004: d.arc(box, 135, 135 + 270 * v, fill=ACC, width=wr)
        r = 24 * SS                                             # flat disc, no shadow
        d.ellipse((c - r, c - r, c + r, c + r), fill=DISC, outline=DISC_EDGE, width=SS)
        a = math.radians(135 + 270 * v)                          # short pointer, rounded ends
        p0 = (c + math.cos(a) * 9 * SS, c + math.sin(a) * 9 * SS)
        p1 = (c + math.cos(a) * 19 * SS, c + math.sin(a) * 19 * SS)
        polyline(d, [p0, p1], INK, int(3 * SS))
        frames.append(down(im, size, size))
    strip(frames, "knob.png")

# ---------------------------------------------------------------------------------------------- wavetable
def wave_shape(t, x):
    """A generic table: sine -> triangle-ish -> saw -> narrowing pulse with formant ripple, as t goes 0..1."""
    ph = 2 * math.pi * x
    h = [1.0]
    out = math.sin(ph)
    saw = sum(math.sin(k * ph) / k for k in range(1, 12)) * 0.6
    tri = sum(((-1) ** ((k - 1) // 2)) * math.sin(k * ph) / (k * k) for k in range(1, 12, 2)) * 0.85
    pul = sum(math.sin(k * ph) * math.sin(k * math.pi * (0.5 - 0.4 * t)) / k for k in range(1, 16)) * 0.7
    if t < 0.33:   u = t / 0.33;           out = (1 - u) * out + u * tri
    elif t < 0.66: u = (t - 0.33) / 0.33;  out = (1 - u) * tri + u * saw
    else:          u = (t - 0.66) / 0.34;  out = (1 - u) * saw + u * pul
    return max(-1.0, min(1.0, out))

def wave(w, h, n=64):
    depth = 10                                 # frames drawn behind, as a stack going up and to the right
    dx, dy = 1.6, 2.4                          # offset per stacked frame (px)
    pw, ph = w - 40 - depth * dx, (h - 34 - depth * dy) / 2
    frames = []
    for i in range(n):
        t = i / (n - 1)
        im = display(w, h); d = ImageDraw.Draw(im)
        def curve(tt, k):
            ox, oy = 20 + k * dx, h / 2 + depth * dy / 2 - k * dy + 4
            return [((ox + pw * x) * SS, (oy - ph * wave_shape(tt, x)) * SS) for x in np.linspace(0, 1, 120)]
        for k in range(depth, -1, -1):          # back to front
            tt = k / depth
            dist = abs(tt - t)
            a = int(26 + 50 * max(0.0, 1 - dist * 3))
            polyline(d, curve(tt, k), hexc("8c8a85", a), int(1.2 * SS))
        cur = curve(t, t * depth)
        glow = canvas(w, h); gd = ImageDraw.Draw(glow)
        polyline(gd, cur, hexc("e8871e", 150), int(7 * SS))
        im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
        polyline(d, cur, ACC_HI, int(2.6 * SS))
        # position bar along the bottom
        y = (h - 9) * SS
        d.line(((14 * SS, y), ((w - 14) * SS, y)), fill=LINE, width=2 * SS)
        d.line(((14 * SS, y), ((14 + (w - 28) * t) * SS, y)), fill=ACC, width=2 * SS)
        frames.append(down(im, w, h))
    strip(frames, "wave_%dx%d.png" % (w, h))

# ---------------------------------------------------------------------------------------------- filters
FLT_TYPES = ["lp12", "lp24", "hp12", "hp24", "bp12", "bp24", "notch12", "notch24"]

def response(kind, f, fc, q=0.7071):
    s = f / fc
    lp = 1 / np.sqrt((1 - s ** 2) ** 2 + (s / q) ** 2)
    hp = s ** 2 * lp
    bp = (s / q) * lp
    nt = np.abs(1 - s ** 2) * lp
    base = {"lp": lp, "hp": hp, "bp": bp, "notch": nt}[kind.rstrip("0123456789")]
    return base ** (2 if kind.endswith("24") else 1)

def filt(kind, w, h):
    """128 frames: CUTOFF in 32 steps x RESONANCE in 4 (frame = cut * 4 + res), the value the DSP reports as mN_fview.
    Cutoff on the engine's scale (MIDI note 8..135); resonance with the engine's Q law at 0, 1/3, 2/3, 1 of the knob
    (drawn up to Q 8: the top of the knob only gets louder, the picture would clip)."""
    frames = []
    f = np.geomspace(20, 20000, 240)
    for i in range(128):
        cut, res = i // 4, i % 4
        fc = 440.0 * 2 ** ((8 + cut / 31 * 127 - 69) / 12)
        q = min(8.0, 0.7071 / (1 - (res / 3) * 0.99))
        r = response(kind, f, fc, q) * (response(kind, f, fc, 0.7071) if kind.endswith("24") else 1)
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
        fd.polygon(pts + [(pts[-1][0], (h - 6) * SS), (pts[0][0], (h - 6) * SS)], fill=hexc("e8871e", 55))
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
        fd.polygon(pts + [(X(1), Y(0)), (X(0), Y(0))], fill=hexc("e8871e", 50))
        im.alpha_composite(fill)
        polyline(d, pts, ACC_HI, int(2.4 * SS))
        frames.append(down(im, w, h))
    strip(frames, "env_%s_%dx%d.png" % (stage, w, h))

# ---------------------------------------------------------------------------------------------- logo
LOGO_FONT = "/usr/share/fonts/opentype/inter/Inter-ExtraLight.otf"   # Inter (OFL); only the PNG ships

def logo(size=64):
    """'NorthWest' in thin letters, the W in the accent colour. Needs the font above; without it the committed
    images/nw/logo.png is kept as it is."""
    from PIL import ImageFont
    if not os.path.exists(LOGO_FONT):
        print("  logo.png                   kept (font not found)"); return
    font = ImageFont.truetype(LOGO_FONT, size * SS)
    parts = [("North", INK), ("W", ACC), ("est", INK)]
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
WAVE = (560, 110)     # v2.0: the slot pages' sizes
FLT = (220, 56)
ENV = (96, 50)

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    knob()
    wave(*WAVE)
    for k in FLT_TYPES: filt(k, *FLT)
    for s in "adsr": env(s, *ENV)
    logo()
