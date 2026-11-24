# NorthWest

A synthesizer for the Akai MPC (built and tested for the MPC One on MPC OS 2.15.1): **four monophonic machines
in layers**, each running its own **model** — wavetable, FM, west coast or analog — for rich, harmonic sequences
(basslines, arpeggios, FM percussion) at a low CPU cost. Its own minimal, rounded charcoal-and-orange screen.

Status: **1.4** in progress (branch v1.4: WEST COAST); 1.3 in testing. ANALOG and the effects arrive in 1.5. Plan and decisions:
`PROJECT.md`.

## How it plays

Every note plays on all four machines at once (layers), on the track's MIDI channel. Each machine is monophonic,
with last-note priority; GLIDE MODE can be OFF, GLISSANDO, PORTAMENTO or LEGATO (303-style slides between
overlapping notes). A machine at level 0 or muted costs no CPU. Models can repeat: four FMs, four wavetables or any mix.

## Models

| Model | Sound | Controls |
|---|---|---|
| WAVETABLE | 1 wavetable with unison (1-4) and a sub oscillator | position, effect (FM, SYNC, FOLD, WARP) with two amounts, detune, spread, unison mode; sub level, octave, wave |
| FM | 4 sine operators, 8 algorithms, feedback on operator 4 | levels and ratios of operators 2-4, a shared modulator decay, pitch envelope, noise, velocity to FM; mode TONE or DRUM (one-shot) |
| WEST COAST | carrier phase-modulated by a second oscillator (sine, triangle, saw, pulse; sync), wavefolder, low pass gate (filter + VCA through a vactrol, driven by the amp envelope) | FM amount, fold, symmetry, osc mix, LPG depth and response, modulator ratio and fine, velocity to fold; a picture of the folded wave |
| ANALOG | coming in 1.5 | 2 oscillators, FM, sync, pitch envelope, noise, 24 dB ladder |

Every machine also has a filter (8 types; cutoff, resonance, envelope amount and decay, key tracking) and an amp
envelope (attack, decay, sustain, release, velocity).

## Pages

| Page | What is on it | Q-Link sets |
|---|---|---|
| MIX | model, level, pan, tune and mute of each machine (one column each) | – |
| MACHINE 1-4 | the model selector (arrows and list), the model's controls (they change with the model), filter and amp | MACHINE · FILTER AMP |
| MOD | EG1, EG2, LFO1, LFO2, shared by all machines | ENVS · LFOS |
| MATRIX | 8 rows: source, destination, machine (ALL or M1-M4), amount | – |
| GLOBAL | glide, bend range, volume, presets | – |

The 12 machine knobs (K1-K12) keep their place for every model, so a Q-Link always turns the same knob; their names
and values follow the model. On screen, left to right is a Q-Link column top to bottom. The wavetable view draws the
loaded table's own waves, the filter view follows CUTOFF and RESONANCE, the envelope views follow their knobs.

## Presets

17 factory sounds (`presets/northwest.tbl`), among them FM Bass, FM Kick, FM Snare, FM Hat, Layer WT + FM,
"Bajo y Percusion" (all four machines), West Coast Bongo, Folded Bass and West Arp. The sound is also saved with the MPC project.

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
