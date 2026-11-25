/* DeathCoast -- the plugin around the engine: parameters, factory presets, the wavetable library, the pictures the
 * skin shows, its own MIDI port, the CPU readout and the saved state. Built for the MPC (the framework's VST2
 * wrapper calls the plugin_api_v2 functions at the end of this file).
 *
 * Threads: set_param / get_param / render_block come from MPC's audio callback; file work (the table scan and
 * every table load) runs on the loader's worker. render_block never allocates and never touches a file.
 *
 * Parts of the wavetable engine and of the loader come from Tablor (BSD-3-Clause): see LICENSE and NOTICE. */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "../host/plugin_api_v1.h"
#include "params.h"
#include "table_view.h"
#include "engine.h"
#include "midi_port.h"
#include "wt/scanner.h"
#include "wt/loader.h"

// The build toolchain's libstdc++ headers (GCC 11+) make vector/new[] call std::__throw_bad_array_new_length,
// which lives only in libstdc++ GLIBCXX_3.4.29. The MPC's libstdc++ is older, so the .so would fail to load
// ("version GLIBCXX_3.4.29 not found"). Defining it here keeps that versioned import out of the .so.
namespace std {
void __throw_bad_array_new_length() { std::abort(); }
}

static const host_api_v1_t *g_host = nullptr;

/* the state's tag; NorthWest 1.6 saved "NW1;" and the 1.3-1.5 betas "TBLR2;" (same key=value body), still read */
static constexpr char kStateTag[] = "DC1;";
static constexpr char kOldStateTag[] = "TBLR2;";
static constexpr char kNwStateTag[] = "NW1;";
/* presets and old states name tables by the Move library path the code came from; the tables are in
 * <module>/wavetables, and a table inside it is saved by its path from there ("DeathCoast/Basics/...") */
static constexpr char kOldLibrary[] = "/data/UserData/UserLibrary/Wavetables/";

struct Instance {
    dc::Engine engine;
    dc::WtScanner scanner;
    dc::WtLoader loader { engine };
    dc::MidiPort midiPort;
    char module_dir[512] = {};
    char wt_path[DC_SLOTS][512] = {};            /* absolute; "" = the built-in Init table */

    struct Preset { std::string name, blob; };
    std::vector<Preset> presets;                  /* factory, read once in create_instance */
    std::atomic<int> preset_index { 0 };

    /* the table list (index = the mN_select value), published by the worker once its scan is done */
    std::shared_ptr<const std::vector<dc::WtEntry>> tables { std::make_shared<std::vector<dc::WtEntry>>() };
    std::shared_ptr<const std::vector<dc::WtEntry>> tableList() const { return std::atomic_load(&tables); }

    std::atomic<bool> ready { false };
    char state_buf[8 * 1024];

    /* CPU readout: the engine's share of each block, peak-held, shown in whole percent twice a second */
    float cpuPeak = 0.0f;
    int cpuShown = 0, cpuSamples = 0;

    float *params() { return engine.pots; }
};

/* ------------------------------------------------------------------ parameters */

static int param_index(const char *key)
{
    for (int i = 0; i < DC_PARAM_COUNT; i++)
        if (!strcmp(dc_params[i].key, key)) return i;
    return -1;
}

static float clamp_param(const dc_param_t *p, float v) { return std::clamp(v, p->min, p->max); }

/* "mN_<suffix>" -> machine 0..3, else -1 */
static int slot_key(const char *k, const char *suffix)
{
    if (k[0] != 'm' || k[1] < '1' || k[1] > '0' + DC_SLOTS || k[2] != '_') return -1;
    return strcmp(k + 3, suffix) == 0 ? k[1] - '1' : -1;
}

static float slotParam(Instance *inst, int m, int s1Index) { return inst->params()[s1Index + m * DC_SLOT_STRIDE]; }

/* "<enum key>_st": the same selector as a whole number 0..n-1, for the steppers' boxes. MPC's data wheel (and its
 * -/+ keys) step a whole-number param with "nudge_pct" one value a click; an option list under QLINK_TRAVEL needs
 * half an option's width of travel (50 clicks on a 2-option list). Text: "<key>_st_display". */
static int stepper_alias(const char *key, bool *display)
{
    char base[64];
    size_t n = strlen(key);
    *display = false;
    if (n > 11 && !strcmp(key + n - 11, "_st_display")) { *display = true; n -= 11; }
    else if (n > 3 && !strcmp(key + n - 3, "_st")) n -= 3;
    else return -1;
    if (n >= sizeof base) return -1;
    memcpy(base, key, n); base[n] = 0;
    const int idx = param_index(base);
    return idx >= 0 && dc_params[idx].type == DC_ENUM ? idx : -1;
}

/* ------------------------------------------------------------------ wavetables */

static int path_slot(int param_idx) { return std::clamp((param_idx - DC_P_M1_TABLE) / DC_SLOT_STRIDE, 0, DC_SLOTS - 1); }

/* a table's index in the list (0 = Init, or not found) */
static int table_index(const Instance *inst, const char *path)
{
    if (!path[0]) return 0;
    auto list = inst->tableList();
    for (size_t i = 1; i < list->size(); i++)
        if ((*list)[i].path == path) return (int) i;
    return 0;
}

static void set_table_path(Instance *inst, int m, const char *path)
{
    char full[sizeof inst->wt_path[0]];
    if (!strcmp(path, "Init")) path = "";
    if (!strncmp(path, kOldLibrary, sizeof kOldLibrary - 1))            /* an old preset or state */
        path += sizeof kOldLibrary - 1;
    const char *pack = "";                                               /* 1.6 and before: the pack was "NorthWest/" */
    if (!strncmp(path, "NorthWest/", 10)) { pack = "DeathCoast/"; path += 10; }
    if (path[0] && path[0] != '/') {                                     /* relative to <module>/wavetables */
        if (snprintf(full, sizeof full, "%s/wavetables/%s%s", inst->module_dir, pack, path) >= (int) sizeof full) return;
        path = full;
    }
    if (strlen(path) >= sizeof inst->wt_path[m]) return;                 /* a path that long can't be one of ours */
    memcpy(inst->wt_path[m], path, strlen(path) + 1);
    dc::WtEntry e;
    e.path = path;
    if (const char *dot = strrchr(path, '.'); dot && !strncasecmp(dot, ".wt", 3)) {   /* .wtNNNN: FLAC */
        const int fs = atoi(dot + 3);
        e.flac = true;
        if (fs >= 32 && fs <= 4096 && (fs & (fs - 1)) == 0) e.flacFrameSize = fs;
    }
    inst->loader.requestLoad(m, e);
}

/* the path a state or preset stores: relative when the table is one of the plugin's own */
static const char *saved_path(const Instance *inst, const char *path, char *buf, size_t len)
{
    snprintf(buf, len, "%s/wavetables/", inst->module_dir);
    const size_t n = strlen(buf);
    if (!strncmp(path, buf, n)) return path + n;
    return path;
}

/* the table picture: which picture shows machine m's table (0 = not one of the shipped, 1 = Init); the pictures
 * are split over two strips (tablesa_*, tablesb_*), each with a blank frame 0 -- table_strip() says which frame */
static int table_view(const Instance *inst, int m)
{
    const char *path = inst->wt_path[m];
    if (!path[0]) return 1;
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    const char *dot = strrchr(leaf, '.');
    const size_t len = dot ? (size_t) (dot - leaf) : strlen(leaf);
    for (size_t i = 0; i < sizeof kTableViewNames / sizeof kTableViewNames[0]; i++)
        if (strlen(kTableViewNames[i]) == len && !strncmp(kTableViewNames[i], leaf, len)) return 2 + (int) i;
    return 0;
}

static int table_strip(const Instance *inst, int m, bool second)
{
    const int v = table_view(inst, m);
    if (v < DC_TABLE_VIEW_SPLIT) return second ? 0 : v + 1;
    return second ? v - DC_TABLE_VIEW_SPLIT + 1 : 0;
}

/* ------------------------------------------------------------------ pictures */

/* WEST COAST: FOLD (K2) in 16 steps x SYMMETRY (K3) in 8 */
static int fold_view(Instance *inst, int m)
{
    const int fo = (int) std::lround(slotParam(inst, m, DC_P_M1_K2) / 127.0f * 15.0f);
    const int sy = (int) std::lround(slotParam(inst, m, DC_P_M1_K3) / 127.0f * 7.0f);
    return std::clamp(fo, 0, 15) * 8 + std::clamp(sy, 0, 7);
}

static int filter_view(Instance *inst, int m)
{
    return dc::filterView((int) slotParam(inst, m, DC_P_M1_FTYPE), slotParam(inst, m, DC_P_M1_CUTOFF),
                          slotParam(inst, m, DC_P_M1_RESO), slotParam(inst, m, DC_P_M1_FSHAPE));
}

/* ------------------------------------------------------------------ presets and state */

static void load_presets(Instance *inst)
{
    char path[600];
    snprintf(path, sizeof path, "%s/presets/deathcoast.tbl", inst->module_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[8192];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = 0;
        char *blob = bar + 1;
        blob[strcspn(blob, "\r\n")] = 0;
        inst->presets.push_back({ line, blob });
    }
    fclose(f);
}

static void build_state(Instance *inst)
{
    int o = snprintf(inst->state_buf, sizeof inst->state_buf, "%s", kStateTag);
    char rel[600];
    for (int i = 0; i < DC_PARAM_COUNT && o < (int) sizeof inst->state_buf - 64; i++) {
        if (dc_params[i].type == DC_PATH) {
            const char *path = inst->wt_path[path_slot(i)];
            if (path[0])
                o += snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "%s=%s;", dc_params[i].key,
                              saved_path(inst, path, rel, sizeof rel));
            continue;
        }
        const float v = inst->params()[i];
        if (v == dc_params[i].def) continue;                 /* defaults are implicit */
        o += snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "%s=%g;", dc_params[i].key, (double) v);
    }
    /* the preset chosen, so a reopened project doesn't take MPC's PRESET value as a new choice */
    const int pi = inst->preset_index.load(std::memory_order_relaxed);
    if (pi > 0 && o < (int) sizeof inst->state_buf - 32)
        snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "mpc_preset=%d;", pi);
}

static void reset_to_defaults(Instance *inst)
{
    for (int i = 0; i < DC_PARAM_COUNT; i++)
        if (dc_params[i].type != DC_PATH) inst->params()[i] = dc_params[i].def;
    for (int m = 0; m < DC_SLOTS; m++) set_table_path(inst, m, "");
}

/* "DC1;key=value;..." (or NorthWest's "NW1;..." / a beta's "TBLR2;..."); anything else is ignored whole */
static void apply_state(Instance *inst, const char *val)
{
    const char *p;
    if (!strncmp(val, kStateTag, sizeof kStateTag - 1)) p = val + sizeof kStateTag - 1;
    else if (!strncmp(val, kOldStateTag, sizeof kOldStateTag - 1)) p = val + sizeof kOldStateTag - 1;
    else if (!strncmp(val, kNwStateTag, sizeof kNwStateTag - 1)) p = val + sizeof kNwStateTag - 1;
    else return;
    char kbuf[64], vbuf[512];
    while (*p) {
        const char *eq = strchr(p, '=');
        if (!eq) break;
        const char *semi = strchr(eq, ';');
        if (!semi) semi = eq + strlen(eq);
        const size_t kl = (size_t) (eq - p), vl = (size_t) (semi - eq - 1);
        if (kl < sizeof kbuf && vl < sizeof vbuf) {
            memcpy(kbuf, p, kl); kbuf[kl] = 0;
            memcpy(vbuf, eq + 1, vl); vbuf[vl] = 0;
            if (!strcmp(kbuf, "mpc_preset")) inst->preset_index.store(atoi(vbuf), std::memory_order_relaxed);
            const int idx = param_index(kbuf);
            if (idx >= 0) {
                if (dc_params[idx].type == DC_PATH) set_table_path(inst, path_slot(idx), vbuf);
                else inst->params()[idx] = clamp_param(&dc_params[idx], strtof(vbuf, nullptr));
            }
        }
        p = *semi ? semi + 1 : semi;
    }
}

/* ------------------------------------------------------------------ set / get */

static void dc_set_param(void *instance, const char *key, const char *val)
{
    auto *inst = (Instance *) instance;
    if (!inst || !key || !val) return;
    const char *colon = strrchr(key, ':');                   /* "<prefix>:state" or "state" */
    const char *k = colon ? colon + 1 : key;

    if (!strcmp(k, "state")) { apply_state(inst, val); return; }
    if (!strcmp(k, "preset")) {          /* the PRESET menu: preset n = the defaults, then its blob */
        if (inst->presets.empty()) return;
        const int n = std::clamp(atoi(val), 0, (int) inst->presets.size() - 1);
        /* the preset already chosen: nothing to do, so a project that restores every parameter after its chunk
         * (as MPC does) keeps its edits instead of having the preset applied over them */
        if (n == inst->preset_index.load(std::memory_order_relaxed)) return;
        reset_to_defaults(inst);
        apply_state(inst, inst->presets[(size_t) n].blob.c_str());
        inst->preset_index.store(n, std::memory_order_relaxed);
        return;
    }
    if (int m = slot_key(k, "select"); m >= 0) {
        auto list = inst->tableList();
        const int n = atoi(val);
        if (n < 0 || n >= (int) list->size()) return;
        set_table_path(inst, m, (*list)[(size_t) n].path.c_str());
        return;
    }

    int idx = param_index(k);
    if (idx < 0) {
        bool disp;
        idx = stepper_alias(k, &disp);
        if (idx < 0 || disp) return;
    }
    const dc_param_t *p = &dc_params[idx];
    if (p->type == DC_PATH) { set_table_path(inst, path_slot(idx), val); return; }
    float v;
    if (p->type == DC_ENUM) {            /* an option name or its index */
        v = -1;
        for (int i = 0; i < p->n_options; i++)
            if (!strcmp(p->options[i], val)) { v = (float) i; break; }
        if (v < 0) {
            char *end = nullptr;
            const float n = strtof(val, &end);
            if (end == val) return;
            v = (float) (int) n;
        }
    } else {
        v = strtof(val, nullptr);
    }
    inst->params()[idx] = clamp_param(p, v);
}

static int write_str(char *buf, int buf_len, const char *s)
{
    int n = (int) strlen(s);
    if (n >= buf_len) n = buf_len - 1;
    memcpy(buf, s, (size_t) n);
    buf[n] = 0;
    return n;
}

static int write_int(char *buf, int buf_len, int v)
{
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%d", v);
    return write_str(buf, buf_len, tmp);
}

/* the readouts whose text the plugin writes (params.json "dynamic_display" / "dynamic_name") */
static int dynamic_text(Instance *inst, const char *key, char *buf, int buf_len)
{
    const float *P = inst->params();
    char tmp[48];
    /* a machine's knob K1..K14: its name and value text follow the machine's model */
    if (key[0] == 'm' && key[1] >= '1' && key[1] <= '0' + DC_SLOTS && key[2] == '_' && key[3] == 'k') {
        const int m = key[1] - '1';
        char *end = nullptr;
        const long k = strtol(key + 4, &end, 10);
        if (end && end != key + 4 && k >= 1 && k <= DC_KNOBS) {
            const int model = (int) P[DC_P_M1_MODEL + m * DC_SLOT_STRIDE];
            if (!strcmp(end, "_name")) {
                snprintf(tmp, sizeof tmp, "M%d %s", m + 1,
                         dc::knobName(model, (int) k - 1, (int) P[DC_P_M1_WT_FX + m * DC_SLOT_STRIDE]));
                return write_str(buf, buf_len, tmp);
            }
            if (!strcmp(end, "_display")) {
                dc::knobText(tmp, sizeof tmp, model, (int) k - 1, P[DC_P_M1_K1 + (k - 1) + m * DC_SLOT_STRIDE]);
                return write_str(buf, buf_len, tmp);
            }
        }
        return -1;
    }
    if (int m = slot_key(key, "fshape_name"); m >= 0) {
        snprintf(tmp, sizeof tmp, "M%d %s", m + 1, dc::filterShapeName((int) slotParam(inst, m, DC_P_M1_FTYPE)));
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "fshape_display"); m >= 0) {
        dc::filterShapeText(tmp, sizeof tmp, (int) slotParam(inst, m, DC_P_M1_FTYPE), slotParam(inst, m, DC_P_M1_FSHAPE));
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "bwlo_display"); m >= 0) {
        dc::baseWidthText(tmp, sizeof tmp, slotParam(inst, m, DC_P_M1_BWLO), false);
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "bwhi_display"); m >= 0) {
        dc::baseWidthText(tmp, sizeof tmp, slotParam(inst, m, DC_P_M1_BWHI), true);
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "drift_display")) {
        snprintf(tmp, sizeof tmp, P[DC_P_DRIFT] < 0.5f ? "OFF" : "%d %%", (int) std::lround(P[DC_P_DRIFT] / 1.27f));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_dtime_display")) {
        if ((int) P[DC_P_FX_DSYNC] == 1) return write_str(buf, buf_len, dc::delayDivName(dc::delayDivIndex(P[DC_P_FX_DTIME])));
        const float s = dc::delayTimeSeconds(P[DC_P_FX_DTIME]);
        if (s < 1.0f) snprintf(tmp, sizeof tmp, "%d ms", (int) std::lround(s * 1000.0f));
        else snprintf(tmp, sizeof tmp, "%.2f s", (double) s);
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_dfb_display")) {
        snprintf(tmp, sizeof tmp, "%d %%", (int) std::lround(P[DC_P_FX_DFB] / 127.0f * 110.0f));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_dtone_display")) { dc::dirtToneText(tmp, sizeof tmp, P[DC_P_FX_DTONE]); return write_str(buf, buf_len, tmp); }
    if (!strcmp(key, "fx_rdecay_display")) {
        const float v = P[DC_P_FX_RDECAY], t = dc::reverbDecaySeconds(v);
        if (dc::reverbFreeze(v)) snprintf(tmp, sizeof tmp, "FREEZE");
        else snprintf(tmp, sizeof tmp, t < 10.0f ? "%.1f s" : "%.0f s", (double) t);
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_rpre_display")) {
        snprintf(tmp, sizeof tmp, "%d ms", (int) std::lround(dc::reverbPreDelaySeconds(P[DC_P_FX_RPRE]) * 1000.0f));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_rdamp_display")) { dc::dirtToneText(tmp, sizeof tmp, P[DC_P_FX_RDAMP]); return write_str(buf, buf_len, tmp); }
    if (!strcmp(key, "fx_xchar_display")) { dc::dirtCharacterText(tmp, sizeof tmp, P[DC_P_FX_XCHAR]); return write_str(buf, buf_len, tmp); }
    if (!strcmp(key, "fx_xtone_display")) { dc::dirtToneText(tmp, sizeof tmp, P[DC_P_FX_XTONE]); return write_str(buf, buf_len, tmp); }
    return -1;
}

static unsigned display_rev(Instance *inst)
{
    unsigned v = 2166136261u;                                /* FNV-1a over every picture value */
    auto mix = [&v](int x) { v = (v ^ (unsigned) x) * 16777619u; };
    for (int m = 0; m < DC_SLOTS; m++) {
        mix(filter_view(inst, m)); mix(table_view(inst, m)); mix(fold_view(inst, m));
    }
    mix((int) inst->params()[DC_P_FX_DSYNC]);                /* DELAY TIME's readout: ms or a note value */
    mix(inst->cpuShown);
    return v;
}

static int dc_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    auto *inst = (Instance *) instance;
    if (!inst || !key || !buf || buf_len <= 1) return -1;

    if (!strcmp(key, "state")) { build_state(inst); return write_str(buf, buf_len, inst->state_buf); }
    if (!strcmp(key, "preset")) return write_int(buf, buf_len, inst->preset_index.load(std::memory_order_relaxed));
    if (!strcmp(key, "preset_count")) return write_int(buf, buf_len, (int) inst->presets.size());
    if (!strcmp(key, "preset_name") || !strncmp(key, "preset_name_at:", 15)) {
        const int n = key[11] ? atoi(key + 15) : inst->preset_index.load(std::memory_order_relaxed);
        if (n < 0 || n >= (int) inst->presets.size()) return write_str(buf, buf_len, "");
        return write_str(buf, buf_len, inst->presets[(size_t) n].name.c_str());
    }
    if (!strcmp(key, "display_rev")) return write_int(buf, buf_len, (int) (display_rev(inst) & 0x7FFFFFFF));
    if (!strcmp(key, "cpu")) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "CPU %d%%", inst->cpuShown);
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "fview"); m >= 0) return write_int(buf, buf_len, filter_view(inst, m));
    if (int m = slot_key(key, "cview"); m >= 0) return write_int(buf, buf_len, fold_view(inst, m));
    if (int m = slot_key(key, "wview"); m >= 0) return write_int(buf, buf_len, table_strip(inst, m, false));
    if (int m = slot_key(key, "wviewb"); m >= 0) return write_int(buf, buf_len, table_strip(inst, m, true));
    if (int m = slot_key(key, "select"); m >= 0) return write_int(buf, buf_len, table_index(inst, inst->wt_path[m]));
    if (int m = slot_key(key, "tname"); m >= 0) {
        const char *path = inst->wt_path[m];
        if (!path[0]) return write_str(buf, buf_len, "Init");
        std::string leaf = path;
        leaf = leaf.substr(leaf.find_last_of('/') + 1);
        const size_t dot = leaf.find_last_of('.');
        if (dot != std::string::npos && dot > 0) leaf.resize(dot);
        return write_str(buf, buf_len, leaf.c_str());
    }
    if (!strcmp(key, "ready")) return write_str(buf, buf_len, inst->ready.load(std::memory_order_relaxed) ? "1" : "0");
    if (!strcmp(key, "busy")) return write_str(buf, buf_len, inst->loader.busy() ? "1" : "0");
    if (int r = dynamic_text(inst, key, buf, buf_len); r >= 0) return r;

    int idx = param_index(key);
    if (idx < 0) {
        bool disp;
        const int a = stepper_alias(key, &disp);
        if (a < 0) return -1;
        const int v = std::clamp((int) inst->params()[a], 0, dc_params[a].n_options - 1);
        return disp ? write_str(buf, buf_len, dc_params[a].options[v]) : write_int(buf, buf_len, v);
    }
    const dc_param_t *p = &dc_params[idx];
    if (p->type == DC_PATH) {
        char rel[600];
        return write_str(buf, buf_len, saved_path(inst, inst->wt_path[path_slot(idx)], rel, sizeof rel));
    }
    if (p->type == DC_ENUM)
        return write_str(buf, buf_len, p->options[std::clamp((int) inst->params()[idx], 0, p->n_options - 1)]);
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%g", (double) inst->params()[idx]);
    return write_str(buf, buf_len, tmp);
}

static int dc_get_error(void *, char *, int) { return 0; }

/* ------------------------------------------------------------------ MIDI and audio */

static void dc_on_midi(void *instance, const uint8_t *msg, int len, int)
{
    auto *inst = (Instance *) instance;
    if (inst && msg) inst->engine.onTrackMidi(msg, len);
}

static double now_seconds()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static void dc_render_block(void *instance, int16_t *out_lr, int frames)
{
    auto *inst = (Instance *) instance;
    if (!inst || !inst->ready.load(std::memory_order_relaxed)) {
        memset(out_lr, 0, (size_t) frames * 2 * sizeof(int16_t));
        return;
    }
    const double t0 = now_seconds();
    inst->midiPort.poll([inst](const uint8_t *m, int len) { inst->engine.onPortMidi(m, len); });
    if (g_host && g_host->get_bpm) inst->engine.setHostBpm(g_host->get_bpm());
    inst->engine.renderBlock(out_lr, frames);

    /* CPU: this block's time over the time it plays for, peak-held and published in whole percent every 0.5 s */
    const float load = (float) ((now_seconds() - t0) / ((double) frames / 44100.0));
    inst->cpuPeak = std::max(load, inst->cpuPeak * 0.995f);
    inst->cpuSamples += frames;
    if (inst->cpuSamples >= 22050) {
        inst->cpuSamples = 0;
        inst->cpuShown = std::clamp((int) std::lround(inst->cpuPeak * 100.0f), 0, 999);
    }
}

/* ------------------------------------------------------------------ lifecycle */

static void *dc_create_instance(const char *module_dir, const char *)
{
    auto *inst = new (std::nothrow) Instance();
    if (!inst) return nullptr;
    if (module_dir) snprintf(inst->module_dir, sizeof inst->module_dir, "%s", module_dir);

    /* the presets now: MPC reads the PRESET menu's size when it inserts the plugin (one small file) */
    load_presets(inst);
    inst->midiPort.open("DeathCoast");
    /* everything else is file work: on the worker, while the engine renders silence */
    inst->loader.start();
    inst->loader.post([inst] {
        inst->scanner.scan(inst->module_dir);
        std::atomic_store(&inst->tables, std::make_shared<const std::vector<dc::WtEntry>>(inst->scanner.list()));
        auto init = dc::makeInitTable();
        for (int m = 0; m < DC_SLOTS; m++) inst->engine.setTable(m, init);
        /* a state restored meanwhile named its tables: load them now the list and the loader are live */
        for (int m = 0; m < DC_SLOTS; m++)
            if (inst->wt_path[m][0]) {
                char keep[512];
                snprintf(keep, sizeof keep, "%s", inst->wt_path[m]);
                set_table_path(inst, m, keep);
            }
        inst->ready = true;
        if (g_host && g_host->log) {
            char msg[160];
            snprintf(msg, sizeof msg, "deathcoast %s ready: %d wavetables, %d presets, MIDI port %s", DC_VERSION,
                     (int) inst->scanner.list().size(), (int) inst->presets.size(),
                     inst->midiPort.isOpen() ? "open" : "unavailable");
            g_host->log(msg);
        }
    });
    return inst;
}

static void dc_destroy_instance(void *instance)
{
    auto *inst = (Instance *) instance;
    if (!inst) return;
    inst->loader.stop();             /* join the worker before the members it publishes into go */
    delete inst;
}

static plugin_api_v2_t g_api = {
    /* .api_version      = */ 2,
    /* .create_instance  = */ dc_create_instance,
    /* .destroy_instance = */ dc_destroy_instance,
    /* .on_midi          = */ dc_on_midi,
    /* .set_param        = */ dc_set_param,
    /* .get_param        = */ dc_get_param,
    /* .get_error        = */ dc_get_error,
    /* .render_block     = */ dc_render_block,
};

/* the framework's entry point (the name comes from the plugin API it implements) */
extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
