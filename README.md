# NorthWest

A synthesizer for the Akai MPC (built and tested for the MPC One on MPC OS 2.15.1): **four monophonic machines
in layers**, each running its own **model** — wavetable, FM, west coast or analog — for rich, harmonic sequences
(basslines, arpeggios, FM percussion) at a low CPU cost. Its own minimal, rounded charcoal-and-orange screen.

Status: **1.5** in progress (branch v1.5: ANALOG, delay and reverb, new test presets); 1.4 in testing. Plan and decisions:
`PROJECT.md`.

## How it plays

Every note plays on all four machines at once (layers), on the track's MIDI channel. Each machine is monophonic,
with last-note priority; GLIDE MODE can be OFF, GLISSANDO, PORTAMENTO or LEGATO (303-style slides between
overlapping notes). A machine at level 0 or muted costs no CPU. Models can repeat: four FMs, four wavetables or any mix.

## Models

| Model | Sound | Controls |
|---|---|---|
| WAVETABLE | 1 wavetable with unison (1-4) and a sub oscillator | position, effect (FM, SYNC, FOLD, WARP) with two amounts, detune, unison, spread, unison mode; SUB (its own panel): level, drive, octave, wave |
| FM | 4 sine operators, 8 algorithms, feedback on operator 4 | levels and ratios of operators 2-4, a shared modulator decay, pitch envelope, noise, velocity to FM; mode TONE or DRUM (one-shot) |
| WEST COAST | carrier phase-modulated by a second oscillator (sine, triangle, saw, pulse; sync), wavefolder, low pass gate (filter + VCA through a vactrol, driven by the amp envelope) | FM amount, fold, symmetry, osc mix, LPG depth and response, modulator ratio and fine, velocity to fold; a picture of the folded wave |
| ANALOG | DFAM-style: 2 band-limited oscillators (sine, triangle, saw, pulse; sync), osc 1 modulates osc 2, noise, drive, a decay pitch envelope | FM 1>2, osc 2 level, noise, drive, pulse width, osc 2 tune and fine, osc 1 octave; PITCH ENV (its own panel): amount, decay, velocity, target (osc 1, both, osc 2). The filter's LP 24 / HP 24 give it the ladder's slopes |

Every machine also has a filter (8 types; cutoff, resonance, envelope amount and decay, key tracking), an amp
envelope (attack, decay, sustain, release, velocity) and two sends: DELAY and REVERB.

## Effects

One delay and one reverb, shared, fed by each machine's sends (page MIX), set on page GLOBAL. Delay: time (10 ms to
1.5 s), feedback, tone (a low pass inside the repeats), level, mode STEREO or PING PONG. Reverb: size, damp, level.
An effect with nothing going into it stops working once its tail has died away, so it costs no CPU.

## Pages

| Page | What is on it | Q-Link sets |
|---|---|---|
| MIX | model, level, pan, delay and reverb sends, tune and mute of each machine (one column each) | MIX |
| MACHINE 1-4 | the model selector (arrows and list), the model's controls (they change with the model), filter and amp | MACHINE · FILTER AMP |
| MOD | EG1, EG2, LFO1, LFO2, shared by all machines | ENVS · LFOS |
| MATRIX | 8 rows: source, destination, machine (ALL or M1-M4), amount | – |
| GLOBAL | glide, bend range, volume, delay, reverb, presets | GLOBAL |

The selectors at the top of a machine page (model, table, algorithm, mode...) are steppers: tap the arrows, or touch
the box and turn the MPC's data wheel. The 12 machine knobs (K1-K12) keep their place for every model, so a Q-Link always turns the same knob; their names
and values follow the model. On screen, left to right is a Q-Link column top to bottom. The wavetable view draws the
loaded table's own waves with a cursor on the frame POSITION is on, the filter view follows CUTOFF and RESONANCE, the envelope views follow their knobs.

## Presets

24 test sounds (`presets/northwest.tbl`, written by `tools/make_presets.py`), named by kind: **BASS**, **MELODY**,
**DRUM**, **EFX** and **DRONE**, plus Init. They use every model and the NorthWest wavetables; they are for trying the
versions out, not the final library. User presets go in `NorthWest Presets`. The sound is also saved with the MPC project.

## Wavetables

- `wavetables/NorthWest/`: 52 original tables in 6 categories, made by `tools/make_wavetables.py`.
- Fetched at build time (`release/library.json`): **Adventure Kid** (AKWF/AKWP) by Kristoffer Ekstrand, public
  domain, 65 tables; **Neu KatalYst**, free to use, 50 tables.
- Your own tables go in the plugin's `northwest/wavetables/` folder on the MPC.

## Building

GitHub Actions: *Actions → Release (draft) → Run workflow* (version, *dry_run* to build only). The zip it makes is
for MPC OS 3.x; for MPC OS 2.15.1 its screen has to be converted (`TUI.json`, `os_compat`). Local builds and the
offline checks: `BUILD.md` and `tools/offline/README.md`.

The files that make the plugin are generated: `tools/gen_params_v2.py` (parameters), `tools/gen_tables.py` (table
pictures), `tools/gen_art.py` (knob and other pictures), `tools/gen_layout.py` (pages).

## Credits and licence

NorthWest is released under the BSD-3-Clause licence (`LICENSE`). Parts of its code come from **Tablor** by
athousanddetails and from **Wavetable** by Roland Rabien (FigBug), with AudioFilter (Michael Massberg), dr_flac
(David Reid, public domain) and pink noise code (Thomas Merchant, MIT); their copyright notices are kept in
`LICENSE`, and `NOTICE` says where the code comes from. The MPC plugin framework is
[mpc-vst-plugins](https://github.com/saustin2010/mpc-vst-plugins). The logo uses Inter (SIL Open Font Licence).
Not affiliated with Akai Professional; "MPC" is a trademark of inMusic Brands.
