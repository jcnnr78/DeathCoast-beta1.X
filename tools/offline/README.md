Offline checks of the sound engine on a normal PC (no MPC, no Docker). From the project root:

    g++ -O2 -std=c++17 -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/host_render.cpp \
        src/dsp/engine.cpp src/dsp/wt/loader.cpp src/ported/wavetable.cpp -o render_wt -lpthread
    ./render_wt out.raw "m1_wt_fx=1,m1_k2=70"      # 3 s of 16-bit stereo 44.1 kHz, raw

Note sequences (DeathCoast is monophonic per machine):

    g++ -O2 -std=c++17 -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/render_seq.cpp \
        src/dsp/engine.cpp src/dsp/wt/loader.cpp src/ported/wavetable.cpp -o seq_new -lpthread
    ./seq_new out.raw "glide_mode=3,glide=60" 1      # seq 0 = separate notes, 1 = overlapping (legato) line

`bench.cpp` (same build line; `./bench t|f|w|a|m|x`) measures the CPU of four machines playing: wavetable, FM,
west coast, analog, mixed, mixed with every send. The plugin itself: the framework's `tools/test_port.sh vst.json`
(ASan host test) with the presets and tables staged in `build/deathcoast/` (BUILD.md).

Every factory preset, with its tables, through a short line (peak, rms, clipped samples, what is left after 5 s):

    g++ -O2 -std=c++17 -Isrc -Isrc/dsp -Isrc/host -Isrc/ported tools/offline/preset_audit.cpp \
        src/dsp/engine.cpp src/dsp/wt/loader.cpp src/ported/wavetable.cpp -o preset_audit -lpthread
    ./preset_audit presets/deathcoast.tbl wavetables
