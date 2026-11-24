# WaveTablor

A fork of Tablor aiming at Ableton Live's *Wavetable* instrument on the MPC. Phase 1 (this release) is the sound
engine; the screens are phase 2 (a mock-up exists, see "Screens" below).

## What is new compared with Tablor

| Area | New | Parameters |
|---|---|---|
| Oscillator effect modes (per oscillator) | **None / FM / Classic / Modern** with two knobs, FX1 and FX2 | `wt1_fx_mode`, `wt1_fx1`, `wt1_fx2` (same for `wt2_`) |
| &nbsp;&nbsp;FM | FX1 = amount, FX2 = ratio of the modulator (0.5x … 8x). The modulator is the *other* oscillator's table, read in mono at a harmonic ratio of this oscillator's pitch | |
| &nbsp;&nbsp;Classic | FX1 = hard sync (1x … 8x), FX2 = pulse-width style phase warp | |
| &nbsp;&nbsp;Modern | FX1 = asymmetry (even harmonics), FX2 = sine wavefolder (1x … 8x drive) | |
| Unison modes | **Classic, Shimmer, Noise, Phase Sync, Pos Spread, Random**; up to 6 voices (was 4) | `wt1_uni_mode`, `wt2_uni_mode`, `wt1_uni`, `wt2_uni` |
| LFOs | **2 LFOs per voice**: sine, triangle, saw up/down, square, S&H, smooth random; free (0.02–20 Hz) or tempo-synced (12 divisions, 2/1 … 1/32T); fade-in; start on key or random phase | `lfo1_*`, `lfo2_*` |
| Modulation matrix | **8 slots**, each source → destination × bipolar amount | `mm1_src` … `mm8_amt` |
| &nbsp;&nbsp;Sources | EG1, EG2, LFO1, LFO2, Velocity, Key, Mod Wheel, Aftertouch, Pitch Bend, Random (per note), Amp Env, Filter Env | |
| &nbsp;&nbsp;Destinations | everything the old EG routes had, plus Pitch (±2 oct), WT1/WT2 FX1 and FX2, Unison Detune, Filter 2 Freq/Res. EG1/EG2's own destination menus gained the same list | |
| Second filter | `flt2_type` (Off, LP/HP/BP/Notch × 12/24 dB), cutoff, resonance, key tracking, filter-envelope amount | `flt2_*` |
| Filter routing | **Serial**, **Parallel** (−3 dB sum) or **Split** (osc 1 + sub → F1, osc 2 + noise → F2) | `flt_route` |
| Factory sounds | 8 new starting points that need no wavetable files | `presets/wavetablor.tbl` |

## Compatibility

* Every new parameter defaults to "off". With all of them at their defaults WaveTablor renders **bit-identical audio** to
  Tablor 1.3.3 (checked on 4 patches with `tools/offline/regress.sh`; the noise generator of the original is random per run, so a patch
  with noise cannot be compared bit for bit).
* Old parameters keep their keys, order and defaults; new ones are appended (VST parameter index = order in
  `params.json`; `params.lock` keeps them). Old presets and projects load unchanged.
* Parameter count grows from 59 to 107 engine parameters (69 → 117 VST parameters).

## Build

See `BUILD.md`.

## Known limits

* **CPU.** On the development PC a typical patch costs the same as Tablor; a very heavy one (8 voices, 6-voice unison on
  both oscillators, FM + sync, second filter in parallel, LFO) costs about 2.6× Tablor's typical load. Nobody has measured
  it on a real MPC yet. If the MPC struggles: lower VOICES, UNISON voices, or leave FM and filter 2 off.
* LFO and matrix values are updated once per 128-sample block (~2.9 ms). Fine for modulation; extreme LFO rates on pitch
  can sound slightly stepped.
* The FM modulator is not affected by unison, and both oscillators in FM mode use each other's *unmodulated* table.
* The new parameters are not on the MPC screen yet (phase 2). They can be automated and are saved with the project,
  and the Q-Links only reach parameters that the layout lists.
* Untested on hardware. The sound engine was tested offline on x86; the ARM build, the screen and the host test of the
  framework (`test_port.sh`) have not been run.

## Offline tests

`tools/gen_wavetablor.py` regenerates `src/dsp/params.h`, `params.json`, `params.base.json` and `params.lock` from the
upstream copies in `tools/baseline/`. Edit the parameter list in that script, not the generated files.

## Pantallas (layout.conf)
8 páginas: MAIN, OSC FX (efecto por oscilador, modo unison, SUB, NOISE), FILTER (2 filtros + routing), SHAPE,
ENVELOPES, MOD (EG1/EG2 + LFO1/LFO2, dos subpáginas de Q-Links), MATRIX (8 ranuras) y VOICE.
`layout.conf` se genera con `python3 tools/gen_layout.py` a partir de `layout.stitch6.conf` (las 6 páginas
originales recoloreadas); edita el generador, no el resultado. Vista previa local: `tools/studio.py preview` del framework.
