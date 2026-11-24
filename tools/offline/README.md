Offline checks of the sound engine on a normal PC (no MPC, no Docker). From the project root:

    g++ -O2 -std=c++17 -DMPC_PORT -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/host_render.cpp \
        src/dsp/engine.cpp src/ported/wavetable.cpp src/ported/audiofilter/ParametricCreator.cpp -o render_wt
    ./render_wt out.raw "m1_wt_fx=1,m1_k2=70"      # 3 s of 16-bit stereo 44.1 kHz, raw
    g++ ... tools/offline/bench.cpp ... -o bench_wt && ./bench_wt h     # CPU cost estimate on this PC

Note sequences (NorthWest is monophonic per machine):

    g++ -O2 -std=c++17 -DMPC_PORT -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/render_seq.cpp \
        src/dsp/engine.cpp src/ported/wavetable.cpp src/ported/audiofilter/ParametricCreator.cpp -o seq_new
    ./seq_new out.raw "glide_mode=3,glide=60" 1      # seq 0 = separate notes, 1 = overlapping (legato) line

`bench.cpp` (`./bench t|h|f|m`) measures the CPU of four machines playing.
