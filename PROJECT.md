# NorthWest - project notes

- 4 mono slots; each slot picks a machine (WAVETABLE, FM, WEST COAST, ANALOG), repeats allowed; machine + params stored in the preset.
- Note routing: layers for now (zones / MIDI channel per slot later).
- FM: TONE/DRUM switch in one voice; velocity modulates FM index and pitch.
- ANALOG (Moog DFAM style): 2 oscillators (sine/tri/saw/pulse), FM 1->2, sync, shared pitch EG, noise mix, 24 dB ladder LP/HP, decay-only envelopes.
- WEST COAST shares oscillators/mixer/envelopes with ANALOG: wavefolder + LPG instead of ladder.
- Target: MPC One. CPU figures are estimates until measured with tools/bench.sh.
- Next visual version: minimalist rounded knobs (value box under each knob), thin NorthWest logo, graphic filter/envelope/wavetable displays.
- 12 test presets planned: 4 bass/arp, 4 FM percussion, 4 mixed.

## Phase 5 (analog + effects) - v1.5, branch v1.5 (built locally, not yet on the MPC)
- ANALOG model (src/dsp/machines.h AnMachine), DFAM style: OscPair waves (pulse with PULSE WIDTH), osc 1 phase-modulates
  osc 2 (FM 1>2), sync, white noise, tanh-like DRIVE (Pade, cheaper than tanh), decay pitch envelope (amount up to
  +48 st, decay 5 ms..4 s, velocity amount, target OSC 1 / BOTH / OSC 2). The ladder is the machine filter's LP 24 /
  HP 24 (same FILTER block as the other models). Header: OSC 1 WAVE, OSC 2 WAVE, SYNC (shared osc_w1/osc_w2/osc_sync).
- K: FM 1>2, OSC 2 LEVEL, NOISE, DRIVE, PITCH ENV, PITCH DECAY, PULSE WIDTH, OSC 2 TUNE, OSC 2 FINE, OSC 1 OCTAVE,
  VEL > P.ENV, P.ENV TARGET; the right-hand 2x2 (K5, K6, K11, K12) is the PITCH ENV panel, like SUB in WAVETABLE.
- FX (src/dsp/fx.h): delay (10 ms..1.5 s, gliding time, feedback with LP tone + 90 Hz HP, stereo or ping pong) and
  reverb (4 combs + 2 allpasses a side). Each machine has DELAY and REVERB sends (mN_dsend / mN_rsend, on MIX with
  Q-Links level, pan, delay, reverb). Effects controls on GLOBAL. Each effect sleeps once silent.
- Presets: the 8 inherited from Tablor/v1 and the 1.3/1.4 test sounds replaced by 24 written by tools/make_presets.py,
  named BASS / MELODY / DRUM / EFX / DRONE. User presets folder renamed from Tablor's to "NorthWest Presets".
- Header selectors (user, 24-nov, after trying 1.4 on the MPC One: the wheel works on the boxes but is "a long way"):
  under QLINK_TRAVEL an option list needs half an option's width of wheel travel per step (a wheel click is 1 % of the
  range: 50 clicks on a 2-option list, 17 on MODEL). Each box now shows "<key>_st", a whole-number twin the plugin
  aliases to the option list (stepper_alias), with nudge_pct 50: one option per wheel click; arrows unchanged. The MPC's
  -/+ keys should step a focused box the same way (not yet checked on the device).
- Checks (PC): host test PASSED; 24 presets render finite; preset -> state -> reload identical (tables load).
  CPU x86: 4 x ANALOG 1.37 %, mixed with delay + reverb 1.6 %.

## Phase 4 (west coast) - v1.4, branch v1.4 (built locally, not yet on the MPC)
- WEST COAST model (src/dsp/machines.h WcMachine): osc 1 phase-modulates osc 2 (OscPair, band-limited AnalogTables
  waves, hard sync), sine wavefolder (up to ~3 folds, symmetry offset), low pass gate = 2-pole low pass + VCA on a
  vactrol follower of the amp envelope (rise 3 ms, fall = LPG RESPONSE 20 ms..2 s). The slot does not apply the amp
  envelope again for this model and stays active until the vactrol has fallen.
- K: FM AMOUNT, FOLD, SYMMETRY, OSC MIX, LPG DEPTH, -, LPG RESPONSE, MOD RATIO, MOD FINE, -, -, VEL > FOLD.
- osc_w1/osc_w2 default SINE now (they were SAW/PULSE, unused until 1.4).
- Fold picture: mN_cview (FOLD 16 x SYMMETRY 8) drives a 128-frame meter (380x96, exactly the 12288 px limit).
- 3 presets: West Coast Bongo, Folded Bass, West Arp. CPU 4 x WEST COAST: 1.27 % of one x86 core.

## Phase 3 (machines) - v1.3 (1.0.3 tested on the MPC One by the user, 24-nov)
- After the first test: page names carry the machine number; WAVETABLE knobs regrouped so SUB is the right-hand 2x2
  block on its own panel (K5 SUB LEVEL, K6 SUB DRIVE (new), K11 SUB OCTAVE, K12 SUB WAVE; SPREAD moved to K8, 64 =
  half); a POSITION cursor drawn over the table picture (posmark, a second meter on top of it); header selectors are
  steppers (MPC's data wheel turns a touched stepper box; a popup's box is its open flag, so the wheel can't turn it).
- Framework gotcha (offline, 2026-11-24): a stepper with when= got blank arrows, because the builder cuts the arrow
  images from the last layer it drew. tools/gen_layout.py fix_arrows(): the arrows go on the page background, each
  model patches the others' arrows, and each machine page ends with a neutral layer (an invisible pixel, when muted).
- Numbered 1.3, not 2.0 (user, 24-nov). Names: MACHINE 1-4, each runs a MODEL (WAVETABLE, FM, WEST COAST, ANALOG).
  Keys: mN_* (mN_model, mN_k1..k12, ...); matrix rows target ALL or M1-M4 (mmN_slot).
- Pages by machine, not by model: MIX, MACHINE 1-4, MOD, MATRIX, GLOBAL (8 Fn keys).
- Generic knobs K1..K12 per machine with dynamic_name / dynamic_display (src/dsp/machines.h knobName/knobText).
- Engine: src/dsp/machines.h (Slot = a machine, WtMachine, FmMachine), src/dsp/engine.cpp (note stack, shared
  modulation). src/dsp/voice.h (v1) is gone.
- WAVETABLE slimmed (1 table, no noise/filter 2) so 4 fit; filter envelope = decay only. SUB kept (user, 24-nov):
  K6 level, K8 octave, K9 wave.
- Table picture: tools/gen_tables.py draws all 169 shipped tables (52 here + 115 Tablor library + Init) into one
  169-frame strip (360x64) and src/dsp/table_view.h; the plugin reports mN_wview (which frame). Frame 0 = USER TABLE.
  A 169-frame meter is more than the 128 seen on the device so far: check it.
- Filter picture reacts to RESONANCE: mN_fview (CUTOFF 32 steps x RESONANCE 4, worked out by the plugin) drives a
  128-frame meter per filter type; HAS_DISPLAY_REV reports it (display_rev). Refreshes about 10 times a second.
- Factory presets: the 8 v1 presets migrated (machine 1 = osc 1, machine 2 = osc 2), plus FM Bass, FM Kick, FM Snare,
  FM Hat, Layer WT + FM, Bajo y Percusion.
- Checks (PC): framework host test PASSED (ASan); all presets and a 4-machines-at-maximum patch render finite with
  clean tails; preset -> state -> reload identical; dynamic names/values and table selection per machine; CPU 1.0-1.6 %.
- Open: CPU on the MPC One; when= switching of whole model areas on the device; Q-Link sub-pages on the device;
  the engine-driven filter picture on the device.

## Phase 2 (look) - v1.2, done (built locally, not yet on the MPC)
- tools/gen_art.py draws images/nw/: knob strip (128 frames), wavetable view (64 frames, follows wtN_pos), filter
  curves per type (64 frames, follow flt_freq / flt2_freq, shown per type with when=), ADSR stage pictures
  (64 frames each), logo (Inter ExtraLight, OFL; only the PNG ships).
- tools/gen_layout.py draws all 8 pages with frames; MAIN and ENVELOPES no longer use the Stitch backgrounds.
- Meters (display filmstrips) were checked by the framework on an MPC One only; the when= switching of the filter
  curves is the same mechanism as the OSC effect knobs (not yet seen on the device).
- Unison 2 by default, max 4; factory presets carry wt1_uni/wt2_uni explicitly so they sound as before.

## Phase 1 (cleanup) - v1.1, done (not yet built on the MPC)
- MOD page: envelope destination/amount removed; EG1/EG2 are routed only through the MATRIX. The me1/me2 dst/amt parameters are gone from the engine too.
- Monophonic engine (one voice, last-note priority): VOICES, VOICE MODE, LEGATO removed. GLIDE MODE gains LEGATO (glide + no envelope retrigger, only between overlapping notes).
- Unison in one block: page UNISON = VOICES, DETUNE, SPREAD, MODE per oscillator. PAN moved to MAIN (where UNISON was).
- BEND/FORMANT vs CLASSIC/MODERN: decided. Effect modes are NONE / FM / SYNC / FOLD / WARP; WARP = bend (FX1) + formant (FX2), upper half of the old knobs only. Matrix destinations WT1/WT2 BEND/FORMANT removed (use WTn FX1/FX2).
- SUB and NOISE stay in the wavetable machine (page OSC); the ANALOG machine gets its own noise in v2.2.
- FLT ENV / AMP ENV stay fixed per voice; EG1/EG2 are matrix sources only.
- Page VOICE renamed GLOBAL; pages SHAPE and OSC FX became UNISON and OSC. Pages UNISON, OSC and GLOBAL are drawn with framework frames (no Stitch background).
- Factory presets migrated with tools/migrate_v11.py.
- Checks (PC, offline): 13 patches bit-identical to the fase1 engine in mono (tools/offline/regress_v11.sh); every preset and a matrix-at-maximum patch render with clean release tails; preset -> state -> reload gives the same state through the plugin API; CPU 0.3 % (typical) / 0.8 % (heavy) of one x86 core for the mono voice.
- Not touched yet: src/ui_pages.json and src/web_ui.html (Move-only UIs, not used by the MPC build) still list the removed parameters.
