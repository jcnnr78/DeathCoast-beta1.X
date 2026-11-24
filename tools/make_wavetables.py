#!/usr/bin/env python3
"""Generates the NorthWest wavetable library (original, procedural, no samples from anywhere).

    python3 tools/make_wavetables.py            # writes wavetables/NorthWest/<Pack>/<Name> 2048.wav

Every table is 64 frames x 2048 samples, 16-bit mono WAV. The file name ends in " 2048" so the plugin's
loader reads the frame size from the name. Frames are built from harmonics (or in the time domain and then
band-limited to 512 harmonics), peak-normalised per frame and DC-free. Deterministic: same output every run.
Needs numpy.
"""
import numpy as np, os, wave, sys

N, F, H = 2048, 64, 1024          # samples per frame, frames, spectrum bins
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "wavetables", "NorthWest")
k = np.arange(1, 513, dtype=np.float64)           # harmonic numbers 1..512
ph = (np.arange(N) / N)                           # one cycle, 0..1

def harm(amps, phases=None):
    """amps[0] is harmonic 1 ... -> one cycle (sine phase 0)."""
    a = np.zeros(H + 1, dtype=np.complex128)
    n = min(len(amps), 511)
    ang = np.zeros(n) if phases is None else np.asarray(phases)[:n]
    a[1:n + 1] = np.asarray(amps)[:n] * np.exp(1j * (ang - np.pi / 2))
    return np.fft.irfft(a, N) * N / 2

def timedom(x):
    """band-limit a time-domain cycle to 511 harmonics"""
    s = np.fft.rfft(x); s[512:] = 0
    return np.fft.irfft(s, N)

def prep(x):
    x = x - x.mean()
    p = np.abs(x).max()
    return (x / p * 0.95) if p > 1e-9 else x

def make(fn, frames=F):
    return np.stack([prep(fn(i / (frames - 1))) for i in range(frames)])

def gauss(c, w): return np.exp(-0.5 * ((k - c) / w) ** 2)
def saw_amps(): return 1.0 / k
def lerp(a, b, t): return a + (b - a) * t
def mixharm(a, b, t): return harm(lerp(a, b, t))
rng = np.random.default_rng(20261123)

T = {}   # (pack, name) -> (frames x N)

# ---------------------------------------------------------------- Basics
sq = np.where(k % 2 == 1, 1.0 / k, 0.0)
tri = np.where(k % 2 == 1, 1.0 / k ** 2, 0.0)
tri_ph = np.where(((k - 1) // 2) % 2 == 0, 0.0, np.pi)
T["Basics", "Sine to Saw"] = make(lambda m: harm(np.where(k == 1, 1.0, m / k)))
T["Basics", "Sine to Square"] = make(lambda m: harm(np.where(k == 1, 1.0, m * sq)))
T["Basics", "Triangle to Saw"] = make(lambda m: harm(lerp(tri * 8 / np.pi ** 2, saw_amps(), m), lerp(tri_ph, 0, m)))
T["Basics", "PWM Sweep"] = make(lambda m: harm(np.abs(np.sin(np.pi * k * (0.5 - 0.46 * m))) / k, -np.pi * k * (0.5 - 0.46 * m) + np.pi / 2))
T["Basics", "Harmonic Ladder"] = make(lambda m: harm(np.clip(1 + m * 31 - (k - 1), 0, 1) * k ** -0.7))
T["Basics", "Odd Harmonics"] = make(lambda m: harm(np.where(k % 2 == 1, np.clip(1 + m * 30 - (k - 1) / 2, 0, 1) / k, 0)))
T["Basics", "Even Emphasis"] = make(lambda m: harm(np.where(k == 1, 1.0, np.where(k % 2 == 0, m * 1.4, 0.15) / k ** 0.8)))
T["Basics", "Phase Scatter"] = make(lambda m: harm(saw_amps(), m * np.sin(k * 12.9898) * np.pi))
T["Basics", "Hollow to Full"] = make(lambda m: harm(np.where(k % 2 == 1, 1, m) / k))

# ---------------------------------------------------------------- Harmonic
T["Harmonic", "Spectral Tilt"] = make(lambda m: harm(k ** -(0.35 + 2.3 * m)))
T["Harmonic", "Moving Peak"] = make(lambda m: harm(saw_amps() * (0.25 + 5 * gauss(1 + 39 * m, 1.6 + 0.08 * (1 + 39 * m)))))
T["Harmonic", "Comb Sweep"] = make(lambda m: harm(saw_amps() * (1 + np.cos(2 * np.pi * k * (0.5 / (1 + 14 * m)))) / 2 + (k == 1) * 0.5))
T["Harmonic", "Twin Formants"] = make(lambda m: harm(saw_amps() * (0.1 + 4 * gauss(2 + 16 * m, 1.5) + 3 * gauss(9 + 26 * m ** 0.7, 2.5))))
dbars = [np.array([8, 8, 8, 0, 0, 0, 0, 0, 0]), np.array([8, 0, 8, 0, 0, 4, 0, 0, 0]), np.array([8, 4, 3, 8, 0, 0, 5, 0, 8]), np.array([4, 8, 0, 8, 0, 8, 0, 8, 0]), np.array([8, 8, 8, 8, 8, 8, 8, 8, 8])]
orgh = [1, 2, 3, 4, 5, 6, 8, 10, 12]
def organ(m):
    t = m * (len(dbars) - 1); i = min(int(t), len(dbars) - 2); w = lerp(dbars[i], dbars[i + 1], t - i)
    a = np.zeros(512)
    for h, v in zip(orgh, w): a[h - 1] = v / 8
    return harm(a)
T["Harmonic", "Organ Drawbars"] = make(organ)
def bell(m):
    a = np.zeros(512)
    for n, (h, amp) in enumerate(zip([1, 2, 3, 5, 7, 9, 11, 14, 18, 23], [1, .6, .5, .5, .4, .35, .3, .25, .2, .15])):
        a[h - 1] = amp * (0.3 + 0.7 * np.sin(np.pi * (m * 2.2 + n * 0.37)) ** 2)
    return harm(a)
T["Harmonic", "Bell Partials"] = make(bell)
walk = np.cumsum(rng.normal(0, 0.18, (F, 64)), axis=0)
def swalk(m):
    i = int(round(m * (F - 1))); a = np.zeros(512)
    a[:64] = np.exp(walk[i] - walk[i].mean()) / np.arange(1, 65) ** 0.7
    return harm(a)
T["Harmonic", "Spectral Walk"] = make(swalk)
T["Harmonic", "Odd Sweep Peak"] = make(lambda m: harm(np.where(k % 2 == 1, 1.0, 0.1) * k ** -1.0 * (0.3 + 4 * gauss(1 + 30 * m, 2.0))))

# ---------------------------------------------------------------- Complex
def fm(m, r, idx): return np.sin(2 * np.pi * ph + idx * m * np.sin(2 * np.pi * r * ph))
T["Complex", "FM 1 to 2"] = make(lambda m: timedom(fm(m, 2, 7)))
T["Complex", "FM 1 to 3"] = make(lambda m: timedom(fm(m, 3, 6)))
def fmr(m):
    r = 1 + 7 * m; a = r - np.floor(r)
    return timedom((1 - a) * fm(1, int(np.floor(r)), 3.2) + a * fm(1, int(np.ceil(r)), 3.2))
T["Complex", "FM Ratio Sweep"] = make(fmr)
T["Complex", "Resonant Saw"] = make(lambda m: timedom((1 - ph) * np.sin(2 * np.pi * (1 + 15 * m) * ph) * (1 + 0.0) + 0.0))
T["Complex", "Hard Sync Sweep"] = make(lambda m: timedom(2 * np.mod(ph * (1 + 7 * m), 1.0) - 1))
T["Complex", "Sine Wavefold"] = make(lambda m: timedom(np.sin(np.sin(2 * np.pi * ph) * (1 + 7 * m) * np.pi / 2)))
def trifold(m):
    x = (4 * np.abs(ph - 0.5) - 1) * (1 + 5 * m)
    return timedom(np.abs(np.mod(x + 1, 4) - 2) - 1)
T["Complex", "Triangle Fold"] = make(trifold)
def crush(m):
    lv = max(3, int(round(64 * (1 - m) ** 2 + 3))); x = 0.7 * np.sin(2 * np.pi * ph) + 0.3 * (2 * ph - 1)
    return timedom(np.round(x * lv) / lv)
T["Complex", "Bit Crush"] = make(crush)
def hold(m):
    step = max(1, int(round(2 ** (m * 7)))); x = np.sin(2 * np.pi * ph) * 0.6 + 0.4 * (2 * ph - 1)
    return timedom(np.repeat(x[::step], step)[:N])
T["Complex", "Sample Hold Reduce"] = make(hold)
def ringm(m):
    r = 1 + 11 * m; a = r - np.floor(r)
    return timedom(np.sin(2 * np.pi * ph) * ((1 - a) * np.sin(2 * np.pi * int(np.floor(r)) * ph + 1) + a * np.sin(2 * np.pi * int(np.ceil(r)) * ph + 1)) + 0.4 * np.sin(2 * np.pi * ph))
T["Complex", "Ring Modulation"] = make(ringm)
def cheb(m):
    n = 1 + 11 * m; a = n - np.floor(n); x = np.sin(2 * np.pi * ph)
    T_ = lambda j: np.cos(j * np.arccos(np.clip(x, -1, 1)))
    return timedom((1 - a) * T_(int(np.floor(n))) + a * T_(int(np.ceil(n))))
T["Complex", "Chebyshev"] = make(cheb)
inh = rng.uniform(0, 2 * np.pi, (6, 512)); inha = rng.uniform(0.2, 1.0, (6, 512)) / np.sqrt(k)
def metal(m):
    t = m * 5; i = min(int(t), 4); a = t - i
    return harm(lerp(inha[i], inha[i + 1], a), lerp(inh[i], inh[i + 1], a))
T["Complex", "Metallic"] = make(metal)
nz = rng.normal(0, 1, (8, 200)); nzp = rng.uniform(0, 2 * np.pi, (8, 200))
def noisy(m):
    t = m * 7; i = min(int(t), 6); a = t - i
    return harm(np.abs(lerp(nz[i], nz[i + 1], a)) / np.arange(1, 201) ** 0.45, lerp(nzp[i], nzp[i + 1], a))
T["Complex", "Noisy Cycles"] = make(noisy)
shapes = [np.sin(2 * np.pi * ph), 2 * ph - 1, np.where(ph < 0.5, 1.0, -1.0), 4 * np.abs(ph - 0.5) - 1, np.sign(np.sin(6 * np.pi * ph)) * 0.8, np.sin(2 * np.pi * ph) ** 3]
def glitch(m):
    t = m * (len(shapes) - 1) * 2; i = int(t // 2); a = 0.0 if t % 2 < 1.4 else (t % 2 - 1.4) / 0.6
    j = min(i + 1, len(shapes) - 1); return timedom(lerp(shapes[min(i, len(shapes) - 1)], shapes[j], a))
T["Complex", "Digital Steps"] = make(glitch)
T["Complex", "Multi Cycle"] = make(lambda m: timedom(2 * np.mod(ph * (1 + int(round(15 * m))), 1.0) - 1))
T["Complex", "Spectral Fold"] = make(lambda m: harm(saw_amps() * np.abs(np.cos(np.pi * k * m * 0.5)) + (k == 1) * 0.3))

# ---------------------------------------------------------------- Vocal
VM = {"A": (730, 1090, 2440), "E": (530, 1840, 2480), "I": (270, 2290, 3010), "O": (570, 840, 2410), "U": (300, 870, 2240)}
VF = {"A": (850, 1220, 2810), "E": (610, 2330, 2990), "I": (280, 2650, 3140), "O": (590, 920, 2710), "U": (370, 950, 2670)}
def vowel_table(table, f0, seq):
    def f(m):
        t = m * (len(seq) - 1); i = min(int(t), len(seq) - 2); a = t - i
        fs = [lerp(x, y, a) for x, y in zip(table[seq[i]], table[seq[i + 1]])]
        env = sum(g * gauss(fr / f0, max(1.2, fr / f0 * 0.12)) for fr, g in zip(fs, (1.0, 0.7, 0.35)))
        return harm(saw_amps() * (0.04 + env))
    return f
T["Vocal", "Vowels Male"] = make(vowel_table(VM, 100, "AEIOU"))
T["Vocal", "Vowels Female"] = make(vowel_table(VF, 200, "AEIOU"))
T["Vocal", "Choir Ah to Oo"] = make(vowel_table(VM, 130, "AOUOA"))
T["Vocal", "Whisper Vowels"] = make(lambda m: harm(saw_amps() * (0.04 + gauss(lerp(5, 18, m), 3)), np.sin(k * 7.3) * 3.0))
T["Vocal", "Alien Formants"] = make(lambda m: harm(saw_amps() * (0.05 + 3 * gauss(3 + 40 * m, 1.8) + 2 * gauss(60 - 45 * m, 2.5) + 2 * gauss(20 + 10 * np.sin(m * 9), 1.5))))

# ---------------------------------------------------------------- Instruments
T["Instruments", "Pluck"] = make(lambda m: harm(k ** -(0.8 + 2.4 * m) * np.abs(np.sin(np.pi * k * 0.18)) + (k == 1) * 0.4))
T["Instruments", "E-Piano Tine"] = make(lambda m: harm(np.where(k == 1, 1.0, 0) + 0.5 * m * gauss(7, 0.4) + 0.3 * m * gauss(14, 0.4) + 0.5 * (1 - m) * k ** -1.6))
T["Instruments", "Clav"] = make(lambda m: harm(np.where(k % 2 == 1, 1, 0.35) * k ** -(0.8 + m) * (0.3 + 2 * gauss(6 + 14 * m, 1.8))))
T["Instruments", "Brass Swell"] = make(lambda m: harm(k ** -(2.2 - 1.6 * m) * (1 + 1.5 * m * gauss(5, 2.5))))
T["Instruments", "Flute Breath"] = make(lambda m: harm((np.where(k == 1, 1.0, 0.35 / k ** 1.5)) + m * 0.2 * np.abs(np.sin(k * 3.7)) * (k > 5) / np.sqrt(k), np.where(k > 5, np.sin(k * 5.1) * 3, 0)))
T["Instruments", "Bowed String"] = make(lambda m: harm(saw_amps() * (0.5 + 3 * gauss(6 + 10 * m, 3) + 2 * gauss(22, 4)), 0.4 * m * np.sin(k * 1.7)))
T["Instruments", "Clarinet"] = make(lambda m: harm(np.where(k % 2 == 1, 1.0 / k ** 0.9, m * 0.5 / k ** 1.5)))

# ---------------------------------------------------------------- Sound design
T["Sound Design", "Filter Sweep Saw"] = make(lambda m: harm(saw_amps() * np.exp(-(k / (2 + 60 * m)) ** 2)))
T["Sound Design", "Resonant Sweep"] = make(lambda m: harm(saw_amps() * (np.exp(-(k / (2 + 50 * m)) ** 2) + 2.5 * gauss(2 + 50 * m, 0.8))))
T["Sound Design", "Growl"] = make(lambda m: timedom(np.tanh((1 + 6 * m) * (2 * ph - 1)) * (1 + 0.5 * np.sin(2 * np.pi * 3 * ph))))
T["Sound Design", "Hollow Pad"] = make(lambda m: harm((np.where(k <= 6, 1.0 / k ** 1.2, 0)) * (0.5 + 0.5 * np.cos(m * 2 * np.pi * k * 0.3)), m * 2 * np.pi * np.sin(k * 2.1)))
T["Sound Design", "Pulse Stack"] = make(lambda m: timedom(np.where(np.mod(ph * (1 + int(round(7 * m))), 1.0) < 0.5 - 0.4 * m, 1.0, -1.0)))
T["Sound Design", "Saw Crush Fold"] = make(lambda m: timedom(np.sin((2 * ph - 1) * (1 + 6 * m) * np.pi / 2)))
T["Sound Design", "Super Saw Spectrum"] = make(lambda m: harm(saw_amps() * (1 + m * 1.6 * np.cos(k * 0.5)) , m * 0.9 * np.sin(k * 1.31) * (k % 7)))

def write(path, frames):
    pcm = np.round(frames.reshape(-1) * 32767).astype("<i2")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100); w.writeframes(pcm.tobytes())

if __name__ == "__main__":
    for (pack, name), fr in T.items():
        assert fr.shape == (F, N) and np.isfinite(fr).all(), (pack, name)
        write(os.path.join(OUT, pack, name + " 2048.wav"), fr)
    print(len(T), "tables written to", OUT)
