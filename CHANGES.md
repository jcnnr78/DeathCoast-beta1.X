# Changes

## DeathCoast 1.8 (in progress, branch v1.8)

- "Machine" is "Engine" everywhere on screen (tabs ENGINE 1-4, Q-Link pages, MATRIX E1-E4); GLOBAL's ENGINE block
  is SYSTEM.
- MAIN: SOLO over MUTE in each column (touch only; any engine on SOLO: only those play, MUTE still wins); the column
  moved down, with room under the sends for a fourth; the name, small, in ENGINE 1's corner.
- Modulation: 4 envelopes (EG1-EG4) and 4 LFOs, pages ENVS and LFOS; the matrix has 16 rows (MATRIX 1-8, 9-16). New
  sources are at the end of the list, so saved routings keep theirs.
- The envelopes, glide, LFO and knob curves are DeathCoast's own (no Tablor code left in them); same curves, the
  smooth random LFO glides on a cosine.
- Stop: when the MPC's sequencer stops the delay and reverb fade away in about a second (HAS_TRANSPORT); a note or
  play ends the fade.

NorthWest is called DeathCoast from 1.7 (after the Costa da Morte). The entries below 1.7 keep the old name.

## DeathCoast 1.7

- OVERSAMPLING: OFF, AUTO (default), ON. AUTO runs a machine at twice the rate only while its sound needs it (fold,
  sync, drive, hard FM, counting the matrix), and steps down only while the note is quiet, so it is not heard.
- 61 new presets: INIT, BASS, LEAD, KEYS, PAD, DRUM, KIT (one machine per MIDI channel), EFX, DRONE, BUCHLA, MOOG,
  STRESS, mixing the four models and the new delay and reverb.
- The workflow checks the plugin against the MPC's system libraries (tools/check_abi.py) and stops before making a
  zip that would not load.
- Cleaner: no compiler warnings; reverb and delay cheaper per sample.

- The name: DeathCoast everywhere (plugin, MIDI port "DeathCoast MIDI In", folder, presets, wavetable pack, code).
  The VST id stays "NWst" and NorthWest's states ("NW1;") and table paths ("NorthWest/...") are still read.

- REVERB, new: an 8-line feedback delay network that keeps each machine's place in the stereo field. DECAY 0.3-20 s
  and FREEZE at the top, SIZE, PRE-DELAY 0-250 ms, TONE (dark <- -> thin), MOD (less metal, then a moving cloud),
  LEVEL, and FROM DELAY (the delay's repeats into the reverb).
- DELAY: SYNC to the MPC's tempo (1/32 .. 1/1, dotted and triplet), FEEDBACK up to 110 % held by tape saturation
  (DRIVE), wow and flutter (MOD), TONE both ways, DUCK.
- FX page: DIRT, DELAY and REVERB on one page, Q-Links in two sets (FX DIRT DELAY, FX REVERB).
- FM: OP1 LEVEL and OP1 RATIO (K14); the page shows the four operators' levels over their ratios.
- Parameters keep their 1.6 index (new ones at the end), so 1.6 projects open with their settings; the reverb's
  old DAMP knob is its TONE now.

- Fix: the FILTER picture showed the wrong frame for every type but MULTI (e.g. LP 4 with RESONANCE at 0 drew a resonant peak). The view parameter runs 0..191 but those strips had 128 frames, so the MPC spread the value over the wrong frames. Every filter strip now has 192 frames: LP 4, COMB, LEGACY follow CUTOFF in 48 steps x RESONANCE 4, EQ CUTOFF 24 x GAIN 8.

## NorthWest 1.6.1

- Fix: 1.6.0 did not load on the MPC. The build's newer libstdc++ headers pulled in `std::__throw_bad_array_new_length` (GLIBCXX_3.4.29), which the MPC's libstdc++ lacks; the plugin now defines it itself.

## NorthWest 1.6 (branch v1.6)

| Area | Change |
|---|---|
| Filter | Digitone II style: BASE/WIDTH in front, then MULTI (LP>BP>HP morph), LP 4 (ZDF ladder, drive, self-oscillation), COMB - / +, EQ, LEGACY LP / HP; a SHAPE knob per type; a picture per type |
| Engine | control rate 32 samples (was 128): tighter envelopes and modulation; 2x oversampling for WEST COAST, ANALOG (sync, FM, drive) and hard FM; a master limiter; DRIFT |
| Effects | DIRT (drive, character, tone, grit) as a third send; the effects get their own page, FX |
| MIDI | a MIDI channel per machine, through the plugin's own MIDI port (NorthWest MIDI In) |
| Models | WEST COAST: LPG panel with LPG MODE (VCA, COMBO, LP), MOD RATIO's default is x1; ANALOG: OSC FREQ names, VEL > P.ENV back, OSC 1 FINE |
| Screens | MAIN in columns (mute, MIDI channel, level, pan, tune, three send sliders; two Q-Link sets); flat stepper boxes; WAVETABLE: the table's picture bigger (440x112) with the POSITION line, SUB as sliders; ANALOG: PITCH as six sliders; CPU readout (beta) |
| Presets | 41 test sounds, by kind (BASS, MELODY, DRUM, EFX, DRONE, STRESS), more West Coast ones |
| Cleanup | Tablor's Move/Schwung code gone (user preset folder, Schwung preset export, Move UI data, pack lists); state tag NW1 (1.3-1.5 TBLR2 states still load); tables saved by their path inside the plugin; the table list reaches every table (was cut at 128); the old biquad filter library removed |

NorthWest 1.1-1.5: see PROJECT.md (phases) and README.md. Below: the first release (then called WaveTablor, 1.0.x),
as changed from Tablor.

## WaveTablor 1.0.x

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
| Factory sounds | 8 new starting points that need no wavetable files | `presets/wavetablor.tbl` (now `presets/northwest.tbl`) |

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
