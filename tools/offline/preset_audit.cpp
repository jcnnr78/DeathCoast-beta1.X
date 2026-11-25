// DeathCoast offline check: plays every preset of a .tbl (with its tables) and prints peak / rms / clipping / tail.
#include "engine.h"
#include "wt/loader.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
using namespace dc;
int main(int argc, char **argv) {
  const char *tbl = argv[1], *wtdir = argv[2];
  std::ifstream in(tbl); std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto bar = line.find('|'); std::string name = line.substr(0, bar), body = line.substr(bar + 5);
    srand(1234);
    Engine e; WtLoader ld(e); ld.start();
    for (int i = 0; i < DC_PARAM_COUNT; i++) e.pots[i] = dc_params[i].def;
    size_t p = 0;
    while (p < body.size()) {
      size_t q = body.find(';', p); if (q == std::string::npos) q = body.size();
      std::string kv = body.substr(p, q - p); p = q + 1; auto eq = kv.find('='); if (eq == std::string::npos) continue;
      std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
      if (k.size() == 8 && k.substr(2) == "_table") {
        WtEntry en; en.path = std::string(wtdir) + "/" + v; ld.requestLoad(k[1] - '1', en);
        while (ld.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      for (int i = 0; i < DC_PARAM_COUNT; i++) if (k == dc_params[i].key) e.pots[i] = atof(v.c_str());
    }
    for (int s = 0; s < 4; s++) if (!e.table(s)) e.setTable(s, makeInitTable());
    const int SR = 44100, frames = SR * 6;
    std::vector<int16_t> buf((size_t) frames * 2);
    struct Ev { int t, st, note; };
    std::vector<Ev> ev = { {0,0x90,48},{SR/2-3000,0x80,48},{SR/2,0x90,55},{SR-3000,0x80,55},{SR,0x90,60},
                           {SR*2,0x80,60},{SR*2+2000,0x90,43},{SR*3,0x80,43} };
    int pos = 0; size_t k = 0;
    while (pos < frames) {
      while (k < ev.size() && ev[k].t <= pos) { for (int ch = 0; ch < 5; ch++) { uint8_t m[3] = { (uint8_t) (ev[k].st | ch), (uint8_t) ev[k].note, 100 };
          if (ch == 0) e.onTrackMidi(m, 3); else e.onPortMidi(m, 3); } k++; }
      int next = k < ev.size() ? ev[k].t : frames;
      int n = std::min(next - pos, 128); if (n <= 0) n = 1;
      e.renderBlock(buf.data() + pos * 2, n); pos += n;
    }
    double pk = 0, ss = 0, tail = 0; int clip = 0;
    for (int i = 0; i < frames * 2; i++) { double x = buf[i] / 32768.0; pk = std::max(pk, std::fabs(x)); ss += x * x; if (std::abs(buf[i]) >= 32767) clip++;
      if (i >= SR * 5 * 2) tail = std::max(tail, std::fabs(x)); }
    printf("%-30s peak %.2f rms %.3f clip %5d tail %.3f\n", name.c_str(), pk, std::sqrt(ss / (frames * 2)), clip, tail);
    ld.stop();
  }
}
