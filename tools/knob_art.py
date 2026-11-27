"""DeathCoast knobs, after the Moog way of sizing a panel: the controls you reach for most get the bigger, fluted
knob, the rest a smaller pointer knob. Both are drawn flat and thin, seen from above, with a turned-aluminium cap and
a ring that lights from light grey to turquoise as the value goes up.

    fluted(v)   MAIN: CUTOFF, RESONANCE, the LEVELs, VOLUME. Eight round lobes turning with the knob, a black skirt
                with two white dots at the position, the value ring inside the skirt.
    pointer(v)  everything else. A small body drawn out into a fin, a white line along it, a scale of 11 dots and
                a thin value ring outside.

Drawn supersampled (SS) and brought down to the strip's size by gen_art.py."""
import math
from PIL import Image, ImageDraw

SS = 6
GREY, TURQ, INK = (195, 200, 208), (111, 211, 199), (242, 240, 236)
TRACK, DOT_OFF = (61, 61, 66), (74, 74, 80)
SKIRT, SKIRT_EDGE = (16, 16, 18), (52, 52, 57)
BODY, BODY_EDGE = (44, 44, 48), (84, 84, 90)
A0, SPAN = 135, 270                                       # the travel: 7 o'clock round to 5 o'clock


def _ang(v): return math.radians(A0 + SPAN * v)
def _pol(c, r, a): return (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
def _mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def _arc(d, c, r, w, a0, a1, col):
    d.arc((c[0] - r, c[1] - r, c[0] + r, c[1] + r), a0, a1, fill=col, width=w)


def _ring(d, c, r, w, v):
    """the value ring: the track, then lit up to v, its colour going from grey to turquoise along the way"""
    _arc(d, c, r, max(1, int(w * 0.6)), A0, A0 + SPAN, TRACK)
    if v <= 0.005: return
    n = max(1, int(60 * v))
    for i in range(n):
        t0, t1 = v * i / n, v * (i + 1) / n
        _arc(d, c, r, w, A0 + SPAN * t0, A0 + SPAN * t1 + 0.6, _mix(GREY, TURQ, (t0 + t1) / 2))


def _line(d, p0, p1, w, col):
    d.line((p0, p1), fill=col, width=w)
    for p in (p0, p1): d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=col)


def _cap(im, c, r, u):
    """turned aluminium seen from above: light in a soft cross, flat otherwise"""
    d = ImageDraw.Draw(im)
    for k in range(72):
        a0 = k * 5
        t = 0.5 + 0.5 * math.cos(math.radians(a0 * 2 + 30))
        g = int(150 + 70 * t)
        d.pieslice((c[0] - r, c[1] - r, c[0] + r, c[1] + r), a0, a0 + 5.5, fill=(g - 2, g, g + 3))
    d.ellipse((c[0] - r, c[1] - r, c[0] + r, c[1] + r), outline=(70, 70, 76), width=max(1, int(0.8 * u)))


def fluted(v, size):
    s = size * SS
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c, u, a = (s / 2, s / 2), s / 100.0, _ang(v)
    d.ellipse((c[0] - 45 * u, c[1] - 45 * u, c[0] + 45 * u, c[1] + 45 * u), fill=SKIRT, outline=SKIRT_EDGE, width=int(u))
    _ring(d, c, 40 * u, int(2.6 * u), v)
    lobes = []
    for k in range(240):
        t = 2 * math.pi * k / 240
        x = abs(math.cos(4 * (t - a)))                    # eight round tips, the valleys between them deep
        lobes.append(_pol(c, (27 + 6 * x ** 0.6) * u, t))
    d.polygon(lobes, fill=BODY, outline=BODY_EDGE)
    for p in (_pol(c, 36.5 * u, a - 0.09), _pol(c, 36.5 * u, a + 0.09)):
        d.ellipse((p[0] - 1.7 * u, p[1] - 1.7 * u, p[0] + 1.7 * u, p[1] + 1.7 * u), fill=INK)
    _cap(im, c, 21 * u, u)
    return im.resize((size, size), Image.LANCZOS)


def pointer(v, size):
    s = size * SS
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c, u, a = (s / 2, s / 2), s / 100.0, _ang(v)
    for i in range(11):
        p = _pol(c, 47.5 * u, _ang(i / 10)); r = 1.0 * u
        d.ellipse((p[0] - r, p[1] - r, p[0] + r, p[1] + r), fill=GREY if i / 10 <= v + 1e-6 else DOT_OFF)
    _ring(d, c, 42 * u, int(1.8 * u), v)
    w = math.radians(13)
    fin = [_pol(c, 22 * u, a - 3 * w), _pol(c, 36 * u, a - w * 0.5), _pol(c, 36 * u, a + w * 0.5), _pol(c, 22 * u, a + 3 * w)]
    d.polygon(fin, fill=BODY, outline=BODY_EDGE)
    d.ellipse((c[0] - 27 * u, c[1] - 27 * u, c[0] + 27 * u, c[1] + 27 * u), fill=BODY, outline=BODY_EDGE, width=int(u))
    _line(d, _pol(c, 26 * u, a), _pol(c, 35 * u, a), int(1.8 * u), INK)
    _cap(im, c, 19 * u, u)
    return im.resize((size, size), Image.LANCZOS)
