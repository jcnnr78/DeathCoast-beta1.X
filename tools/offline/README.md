Offline checks of the sound engine on a normal PC (no MPC, no Docker). From the project root:

    g++ -O2 -std=c++17 -DMPC_PORT -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/host_render.cpp \
        src/dsp/engine.cpp src/ported/wavetable.cpp src/ported/audiofilter/ParametricCreator.cpp -o render_wt
    ./render_wt out.raw "wt1_fx_mode=1,wt1_fx1=70"      # 3 s of 16-bit stereo 44.1 kHz, raw
    g++ ... tools/offline/bench.cpp ... -o bench_wt && ./bench_wt h     # CPU cost estimate on this PC

`regress.sh` compares against a build of the original Tablor (`render_orig`) and `analyze.py` prints level / brightness
of the new features. Both expect those binaries next to them; they are kept as a record of what was checked.

NorthWest v1.1 (mono engine):

    g++ -O2 -std=c++17 -DMPC_PORT -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/render_seq.cpp \
        src/dsp/engine.cpp src/ported/wavetable.cpp src/ported/audiofilter/ParametricCreator.cpp -o seq_new
    ./seq_new out.raw "glide_mode=3,glide=60" 1      # seq 0 = separate notes, 1 = overlapping (legato) line

`regress_v11.sh` compares `seq_new` with `seq_orig` (the same file built from NorthWest-fase1, forced to Mono):
13 patches, all bit-identical when v1.1 was made, including the old BEND/FORMANT -> WARP, EG1 destination ->
matrix and LEGATO switch -> GLIDE MODE LEGATO translations.
