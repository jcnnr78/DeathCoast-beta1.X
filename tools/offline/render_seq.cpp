// NorthWest offline check: ./render_seq out.raw "key=val,..." [seq]
// Plays a monophonic line on the engine and writes 3 s of int16 stereo @44.1 kHz.
//   seq 0: separate notes (48, 55, 60, 43), each released before the next
//   seq 1: overlapping notes (legato line) 48 -> 55 -> 60, then back to 55 when 60 is released
#include "engine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace nw;
int main(int argc, char **argv) {
  srand(1234);
  Engine e;
  if (argc > 2) { std::string s = argv[2]; size_t p = 0; while (p < s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
      std::string kv = s.substr(p, q - p); p = q + 1; size_t eq = kv.find('='); if (eq == std::string::npos) continue;
      std::string k = kv.substr(0, eq); float v = atof(kv.c_str() + eq + 1); bool ok = false;
      for (int i = 0; i < NW_PARAM_COUNT; i++) if (k == nw_params[i].key) { e.pots[i] = v; ok = true; }
      if (!ok) { fprintf(stderr, "unknown param %s\n", k.c_str()); return 2; } } }
  int seq = argc > 3 ? atoi(argv[3]) : 0;
  const int SR = 44100, frames = SR * 3;
  std::vector<int16_t> buf((size_t) frames * 2);
  struct Ev { int t, st, note; };
  std::vector<Ev> ev;
  if (seq == 0) ev = { {0,0x90,48},{SR/2-4000,0x80,48},{SR/2,0x90,55},{SR-4000,0x80,55},{SR,0x90,60},{SR*3/2,0x80,60},{SR*3/2+2000,0x90,43},{SR*2,0x80,43} };
  else          ev = { {0,0x90,48},{SR/2,0x90,55},{SR/2+3000,0x80,48},{SR,0x90,60},{SR*3/2,0x80,60},{SR*2,0x80,55} };
  int pos = 0; size_t k = 0;
  while (pos < frames) {
    while (k < ev.size() && ev[k].t <= pos) { uint8_t m[3] = { (uint8_t) ev[k].st, (uint8_t) ev[k].note, 100 }; e.onTrackMidi(m, 3); k++; }
    int next = k < ev.size() ? ev[k].t : frames;
    int n = std::min(next - pos, 128); if (n <= 0) n = 1;
    e.renderBlock(buf.data() + pos * 2, n); pos += n;
  }
  FILE *f = fopen(argc > 1 ? argv[1] : "out.raw", "wb"); fwrite(buf.data(), 2, buf.size(), f); fclose(f); return 0;
}
