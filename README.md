# DeathCoast

A synthesizer for the Akai MPC (built and tested for the MPC One on MPC OS 2.15.1): **four monophonic machines
in layers**, each running its own **model** — wavetable, FM, west coast or analog — for rich, harmonic sequences
(basslines, arpeggios, FM percussion) at a low CPU cost. Its own minimal, rounded charcoal-and-orange screen.

Status: **1.7** (`main`). Called NorthWest up to 1.6; projects saved with it still open. Plan and decisions:
`PROJECT.md`.

## How it plays

Every note of the plugin's track plays on all four machines at once (layers). A machine can instead listen to a
MIDI channel of its own (MAIN, MIDI: CH 1-16): other MPC tracks send to it by picking **DeathCoast MIDI In** as their
MIDI output port, so each machine can have its own sequence. Each machine is monophonic, with last-note priority;
GLIDE MODE can be OFF, GLISSANDO, PORTAMENTO or LEGATO (303-style slides between overlapping notes). A machine at
level 0 or muted costs no CPU. Models can repeat: four FMs, four wavetables or any mix.

## Models

| Model | Sound | Controls |
|---|---|---|
| WAVETABLE | 1 wavetable with unison (1-4) and a sub oscillator | position, effect (FM, SYNC, FOLD, WARP) with two amounts, detune, unison, spread, unison mode; SUB (sliders): level, drive, octave, wave; a picture of the loaded table with a line on the frame POSITION is on |
| FM | 4 sine operators, 8 algorithms, feedback on operator 4 | level and ratio of each operator (row 1 levels, row 2 ratios), a shared modulator decay, pitch envelope, noise, velocity to FM; mode TONE or DRUM (one-shot) |
| WEST COAST | carrier phase-modulated by a second oscillator (sine, triangle, saw, pulse; sync), wavefolder, low pass gate (filter + VCA through a vactrol, driven by the amp envelope) | FM amount, fold, symmetry, osc mix, modulator ratio and fine; LPG (its panel): depth, response, mode (VCA, COMBO, LP), velocity to fold; a picture of the folded wave |
| ANALOG | DFAM-style: 2 band-limited oscillators (sine, triangle, saw, pulse; sync), osc 1 modulates osc 2, noise, drive, a decay pitch envelope | FM 1>2, noise, drive, pulse width, level and frequency of each oscillator (±24 st); PITCH (sliders): pitch envelope amount, decay, target (osc 1, both, osc 2) and velocity, the fine tune of each oscillator |

Every machine also has a filter, an amp envelope (attack, decay, sustain, release, velocity) and three sends:
DELAY, REVERB and DIRT. The filter (Digitone II style): BASE and WIDTH (a high pass and a low pass in front), then one
of **MULTI** (12 dB, morphs LP > BP > HP), **LP 4** (24 dB ladder with drive, self-oscillates), **COMB -** / **COMB +**
(tuned to CUTOFF; KEY 127 plays it in tune), **EQ** (a peak or a dip), **LEGACY LP** / **LEGACY HP** (1.0-1.5's
filter, 12 or 24 dB); cutoff, resonance, SHAPE (what it does follows the type), envelope amount and decay, key
tracking. WEST COAST, ANALOG and FM can run at twice the rate (OVERSAMPLING, page GLOBAL: OFF, AUTO, ON) so their
folding, sync and phase modulation don't alias; AUTO (the default) does it only while the sound needs it; DRIFT (GLOBAL) lets each machine's pitch and cutoff wander slightly.

## Effects

Three effects, shared, fed by each machine's sends (page MAIN), set on page FX. **DIRT**, a distortion: DRIVE,
CHARACTER (warm saturation > hard asymmetric clipping > wavefolding), TONE (dark .. bright, before the distortion),
GRIT (Erosion-style hash, then bit and sample-rate reduction); its level follows what goes in. **DELAY**, tape-like:
TIME (10 ms .. 1.5 s, or a note value at the MPC's tempo with SYNC), FEEDBACK up to 110 % (the tape saturation holds
it), TONE (dark .. thin), DRIVE, MOD (wow and flutter), DUCK (the repeats dip while you play), LEVEL, STEREO or
PING PONG. **REVERB**, an 8-line network that keeps each machine's place in the stereo field: DECAY (0.3-20 s, and
FREEZE at the top), SIZE, PRE-DELAY, TONE, MOD, FROM DELAY (the delay's repeats into it), LEVEL.
An effect with nothing going into it stops working once its tail has died away, so it costs no CPU. A limiter on the
master keeps the sum from clipping hard.

## Pages

| Page | What is on it | Q-Link sets |
|---|---|---|
| MAIN | a column per machine: model, mute, MIDI channel, level, pan, tune, the three sends (sliders); CPU (beta) | MAIN · SENDS |
| MACHINE 1-4 | the model selector (arrows and list), the model's controls (they change with the model), filter and amp | MACHINE · FILTER AMP |
| MOD | EG1, EG2, LFO1, LFO2, shared by all machines | ENVS · LFOS |
| MATRIX | 8 rows: source, destination, machine (ALL or M1-M4), amount | MATRIX |
| FX | DIRT, DELAY, REVERB | FX DIRT DELAY · FX REVERB |
| GLOBAL | glide, bend range, volume, drift, oversampling, presets | GLOBAL |

The selectors (model, table, algorithm, mode, filter type, MIDI channel...) are steppers: tap the arrows, or touch the
box and turn the MPC's data wheel or press -/+ (one option a click). On screen, left to right is a Q-Link column top
to bottom. The wavetable window draws the loaded table, with a line on the frame POSITION is on; the filter view follows CUTOFF,
RESONANCE and SHAPE; the envelope views follow their knobs. CPU on MAIN is the engine's share of each audio block
(the screen's own drawing is not in it).

## Presets

61 sounds (`presets/deathcoast.tbl`, written by `tools/make_presets.py`), named by kind: **INIT** (one a model and
all four), **BASS**, **LEAD**, **KEYS**, **PAD**, **DRUM**, **KIT** (one machine per MIDI channel), **EFX**, **DRONE**,
**BUCHLA** (west coast at full stretch), **MOOG** (ladder and fat oscillators) and **STRESS** (built to load the CPU as
far as it goes). They mix the four models and use the new delay and reverb in many settings. A sound is saved with the MPC project, or as an MPC plugin preset.

## Wavetables

- `wavetables/DeathCoast/`: 52 original tables in 6 categories, made by `tools/make_wavetables.py`.
- Fetched at build time (`release/library.json`): **Adventure Kid** (AKWF/AKWP) by Kristoffer Ekstrand, public
  domain, 65 tables; **Neu KatalYst**, free to use, 50 tables.
- Your own tables go in the plugin's `deathcoast/wavetables/` folder on the MPC.

## Building

GitHub Actions: *Actions → Release (draft) → Run workflow* (version, *dry_run* to build only). The zip it makes is
for MPC OS 3.x; for MPC OS 2.15.1 its screen has to be converted (`TUI.json`, `os_compat`). The build stops if the
plugin would need a newer system library than the MPC has (`tools/check_abi.py`; 1.6.0 did not load for that). Local builds and the
offline checks: `BUILD.md` and `tools/offline/README.md`.

The files that make the plugin are generated: `tools/gen_params.py` (parameters), `tools/gen_tables.py` (table
pictures), `tools/gen_art.py` (knob and other pictures), `tools/gen_layout.py` (pages).

## Credits and licence

DeathCoast is released under the BSD-3-Clause licence (`LICENSE`). Parts of its code (`src/ported/`, the wavetable oscillator
and loader) come from **Tablor** by athousanddetails and from **Wavetable** by Roland Rabien (FigBug), with dr_flac
(David Reid, public domain) and pink noise code (Thomas Merchant, MIT); their copyright notices are kept in
`LICENSE`, and `NOTICE` says where the code comes from. The MPC plugin framework is
[mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins). The logo uses Inter (SIL Open Font Licence).
Not affiliated with Akai Professional; "MPC" is a trademark of inMusic Brands.
