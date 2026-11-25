// NorthWest v1.5 CPU bench: 4 s of audio, one held note, all 4 machines playing.
//   ./bench t   typical: 4 x WAVETABLE unison 2, filter on
//   ./bench h   heavy:   4 x WAVETABLE unison 4 + FM effect + resonant 24 dB filter
//   ./bench f   4 x FM, 4 operators, feedback, DRUM off
//   ./bench w   4 x WEST COAST, FM + fold + low pass gate
//   ./bench a   4 x ANALOG, saw + pulse, FM, noise, drive, pitch env
//   ./bench x   mixed (as m) + delay and reverb on every machine
//   ./bench m   mixed: WAVETABLE unison 2 . FM . WAVETABLE unison 4 + fold . FM DRUM
#include "engine.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace nw;
static void set(Engine &e, std::string k, float v) {
  for (int i = 0; i < NW_PARAM_COUNT; i++) if (k == nw_params[i].key) { e.pots[i] = v; return; }
  fprintf(stderr, "no param %s\n", k.c_str());
}
int main(int argc, char **argv) {
  const char m = argc > 1 ? argv[1][0] : 't';
  Engine e;
  for (int s = 1; s <= 4; s++) {
    std::string p = "m" + std::to_string(s) + "_";
    set(e, p + "level", 100); set(e, p + "amp_s", 127); set(e, p + "cutoff", 90); set(e, p + "reso", 40);
    int machine = m == 'f' ? 1 : m == 'w' ? 2 : m == 'a' ? 3 : (m == 'm' || m == 'x') ? (s % 2 == 0 ? 1 : 0) : 0;
    if (m == 'x') { set(e, p + "dsend", 90); set(e, p + "rsend", 90); }
    set(e, p + "model", machine);
    if (machine == 0) {
      set(e, p + "k7", (m == 'h' || ((m == 'm' || m == 'x') && s == 3)) ? 127 : 43);
      set(e, p + "k4", 40);
      if (m == 'h') { set(e, p + "wt_fx", 1); set(e, p + "k2", 90); }
      if ((m == 'm' || m == 'x') && s == 3) { set(e, p + "wt_fx", 3); set(e, p + "k3", 60); }
    } else if (machine == 3) {
      set(e, p + "osc_w1", 2); set(e, p + "osc_w2", 3); set(e, p + "osc_sync", 1);
      set(e, p + "k1", 50); set(e, p + "k2", 90); set(e, p + "k3", 40); set(e, p + "k4", 60); set(e, p + "k5", 70); set(e, p + "k6", 80);
    } else if (machine == 2) {
      set(e, p + "k1", 80); set(e, p + "k2", 100); set(e, p + "k3", 40); set(e, p + "k5", 100);
    } else {
      set(e, p + "fm_algo", 0); set(e, p + "k1", 80); set(e, p + "k2", 70); set(e, p + "k3", 60); set(e, p + "k4", 40);
      set(e, p + "k7", 127);
      if ((m == 'm' || m == 'x') && s == 4) { set(e, p + "fm_mode", 1); set(e, p + "k7", 90); set(e, p + "amp_d", 127); }
    }
  }
  uint8_t on[3] = {0x90, 48, 100}; e.onTrackMidi(on, 3);
  std::vector<int16_t> b(128 * 2); int blocks = 44100 * 4 / 128;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < blocks; i++) e.renderBlock(b.data(), 128);
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("%c: 4 slots, 4 s audio in %.3f s -> %.2f%% of one x86 core\n", m, sec, sec / 4 * 100);
}
