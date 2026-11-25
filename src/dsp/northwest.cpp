/* NorthWest -- the plugin around the engine: parameters, factory presets, the wavetable library, the pictures the
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

static const host_api_v1_t *g_host = nullptr;

/* the state's tag; 1.3-1.5 betas saved "TBLR2;" (same key=value body), still read */
static constexpr char kStateTag[] = "NW1;";
static constexpr char kOldStateTag[] = "TBLR2;";
/* presets and old states name tables by the Move library path the code came from; the tables are in
 * <module>/wavetables, and a table inside it is saved by its path from there ("NorthWest/Basics/...") */
static constexpr char kOldLibrary[] = "/data/UserData/UserLibrary/Wavetables/";

struct Instance {
    nw::Engine engine;
    nw::WtScanner scanner;
    nw::WtLoader loader { engine };
    nw::MidiPort midiPort;
    char module_dir[512] = {};
    char wt_path[NW_SLOTS][512] = {};            /* absolute; "" = the built-in Init table */

    struct Preset { std::string name, blob; };
    std::vector<Preset> presets;                  /* factory, read once in create_instance */
    std::atomic<int> preset_index { 0 };

    /* the table list (index = the mN_select value), published by the worker once its scan is done */
    std::shared_ptr<const std::vector<nw::WtEntry>> tables { std::make_shared<std::vector<nw::WtEntry>>() };
    std::shared_ptr<const std::vector<nw::WtEntry>> tableList() const { return std::atomic_load(&tables); }

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
    for (int i = 0; i < NW_PARAM_COUNT; i++)
        if (!strcmp(nw_params[i].key, key)) return i;
    return -1;
}

static float clamp_param(const nw_param_t *p, float v) { return std::clamp(v, p->min, p->max); }

/* "mN_<suffix>" -> machine 0..3, else -1 */
static int slot_key(const char *k, const char *suffix)
{
    if (k[0] != 'm' || k[1] < '1' || k[1] > '0' + NW_SLOTS || k[2] != '_') return -1;
    return strcmp(k + 3, suffix) == 0 ? k[1] - '1' : -1;
}

static float slotParam(Instance *inst, int m, int s1Index) { return inst->params()[s1Index + m * NW_SLOT_STRIDE]; }

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
    return idx >= 0 && nw_params[idx].type == NW_ENUM ? idx : -1;
}

/* ------------------------------------------------------------------ wavetables */

static int path_slot(int param_idx) { return std::clamp((param_idx - NW_P_M1_TABLE) / NW_SLOT_STRIDE, 0, NW_SLOTS - 1); }

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
    if (path[0] && path[0] != '/') {                                     /* relative to <module>/wavetables */
        if (snprintf(full, sizeof full, "%s/wavetables/%s", inst->module_dir, path) >= (int) sizeof full) return;
        path = full;
    }
    if (strlen(path) >= sizeof inst->wt_path[m]) return;                 /* a path that long can't be one of ours */
    memcpy(inst->wt_path[m], path, strlen(path) + 1);
    nw::WtEntry e;
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

/* the table picture: which frame of images/nw/tables_*.png shows machine m's table (0 = not one of the shipped) */
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

/* the scope over the table picture: column c of the frame POSITION (K1) is on, 0..NW_SCOPE_LEVELS-1 */
static int scope_value(Instance *inst, int m, int c)
{
    auto t = inst->engine.table(m);
    const int mid = NW_SCOPE_LEVELS / 2;
    if (!t || t->size() < 1) return mid;
    const int nf = t->size();
    const int f = std::clamp((int) std::lround(slotParam(inst, m, NW_P_M1_K1) / 127.0f * (float) (nf - 1)), 0, nf - 1);
    const nw::FrameTable *ft = t->frame(f);
    if (!ft || ft->levels.empty() || ft->levels[0].data.empty()) return mid;
    const auto &d = ft->levels[0].data;
    const size_t j = std::min(d.size() - 1, (size_t) ((double) c / NW_SCOPE_COLS * (double) ft->levels[0].size));
    const float v = std::clamp(d[j], -1.0f, 1.0f);
    return std::clamp((int) std::lround((v + 1.0f) * 0.5f * (float) (NW_SCOPE_LEVELS - 1)), 0, NW_SCOPE_LEVELS - 1);
}

/* ------------------------------------------------------------------ pictures */

/* WEST COAST: FOLD (K2) in 16 steps x SYMMETRY (K3) in 8 */
static int fold_view(Instance *inst, int m)
{
    const int fo = (int) std::lround(slotParam(inst, m, NW_P_M1_K2) / 127.0f * 15.0f);
    const int sy = (int) std::lround(slotParam(inst, m, NW_P_M1_K3) / 127.0f * 7.0f);
    return std::clamp(fo, 0, 15) * 8 + std::clamp(sy, 0, 7);
}

static int filter_view(Instance *inst, int m)
{
    return nw::filterView((int) slotParam(inst, m, NW_P_M1_FTYPE), slotParam(inst, m, NW_P_M1_CUTOFF),
                          slotParam(inst, m, NW_P_M1_RESO), slotParam(inst, m, NW_P_M1_FSHAPE));
}

/* ------------------------------------------------------------------ presets and state */

static void load_presets(Instance *inst)
{
    char path[600];
    snprintf(path, sizeof path, "%s/presets/northwest.tbl", inst->module_dir);
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
    for (int i = 0; i < NW_PARAM_COUNT && o < (int) sizeof inst->state_buf - 64; i++) {
        if (nw_params[i].type == NW_PATH) {
            const char *path = inst->wt_path[path_slot(i)];
            if (path[0])
                o += snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "%s=%s;", nw_params[i].key,
                              saved_path(inst, path, rel, sizeof rel));
            continue;
        }
        const float v = inst->params()[i];
        if (v == nw_params[i].def) continue;                 /* defaults are implicit */
        o += snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "%s=%g;", nw_params[i].key, (double) v);
    }
    /* the preset chosen, so a reopened project doesn't take MPC's PRESET value as a new choice */
    const int pi = inst->preset_index.load(std::memory_order_relaxed);
    if (pi > 0 && o < (int) sizeof inst->state_buf - 32)
        snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "mpc_preset=%d;", pi);
}

static void reset_to_defaults(Instance *inst)
{
    for (int i = 0; i < NW_PARAM_COUNT; i++)
        if (nw_params[i].type != NW_PATH) inst->params()[i] = nw_params[i].def;
    for (int m = 0; m < NW_SLOTS; m++) set_table_path(inst, m, "");
}

/* "NW1;key=value;..." (or a beta's "TBLR2;..."); anything else is ignored whole rather than half applied */
static void apply_state(Instance *inst, const char *val)
{
    const char *p;
    if (!strncmp(val, kStateTag, sizeof kStateTag - 1)) p = val + sizeof kStateTag - 1;
    else if (!strncmp(val, kOldStateTag, sizeof kOldStateTag - 1)) p = val + sizeof kOldStateTag - 1;
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
                if (nw_params[idx].type == NW_PATH) set_table_path(inst, path_slot(idx), vbuf);
                else inst->params()[idx] = clamp_param(&nw_params[idx], strtof(vbuf, nullptr));
            }
        }
        p = *semi ? semi + 1 : semi;
    }
}

/* ------------------------------------------------------------------ set / get */

static void nw_set_param(void *instance, const char *key, const char *val)
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
    const nw_param_t *p = &nw_params[idx];
    if (p->type == NW_PATH) { set_table_path(inst, path_slot(idx), val); return; }
    float v;
    if (p->type == NW_ENUM) {            /* an option name or its index */
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
    if (key[0] == 'm' && key[1] >= '1' && key[1] <= '0' + NW_SLOTS && key[2] == '_' && key[3] == 'k') {
        const int m = key[1] - '1';
        char *end = nullptr;
        const long k = strtol(key + 4, &end, 10);
        if (end && end != key + 4 && k >= 1 && k <= NW_KNOBS) {
            const int model = (int) P[NW_P_M1_MODEL + m * NW_SLOT_STRIDE];
            if (!strcmp(end, "_name")) {
                snprintf(tmp, sizeof tmp, "M%d %s", m + 1,
                         nw::knobName(model, (int) k - 1, (int) P[NW_P_M1_WT_FX + m * NW_SLOT_STRIDE]));
                return write_str(buf, buf_len, tmp);
            }
            if (!strcmp(end, "_display")) {
                nw::knobText(tmp, sizeof tmp, model, (int) k - 1, P[NW_P_M1_K1 + (k - 1) + m * NW_SLOT_STRIDE]);
                return write_str(buf, buf_len, tmp);
            }
        }
        return -1;
    }
    if (int m = slot_key(key, "fshape_name"); m >= 0) {
        snprintf(tmp, sizeof tmp, "M%d %s", m + 1, nw::filterShapeName((int) slotParam(inst, m, NW_P_M1_FTYPE)));
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "fshape_display"); m >= 0) {
        nw::filterShapeText(tmp, sizeof tmp, (int) slotParam(inst, m, NW_P_M1_FTYPE), slotParam(inst, m, NW_P_M1_FSHAPE));
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "bwlo_display"); m >= 0) {
        nw::baseWidthText(tmp, sizeof tmp, slotParam(inst, m, NW_P_M1_BWLO), false);
        return write_str(buf, buf_len, tmp);
    }
    if (int m = slot_key(key, "bwhi_display"); m >= 0) {
        nw::baseWidthText(tmp, sizeof tmp, slotParam(inst, m, NW_P_M1_BWHI), true);
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "drift_display")) {
        snprintf(tmp, sizeof tmp, P[NW_P_DRIFT] < 0.5f ? "OFF" : "%d %%", (int) std::lround(P[NW_P_DRIFT] / 1.27f));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_dtime_display")) {
        const float s = nw::delayTimeSeconds(P[NW_P_FX_DTIME]);
        if (s < 1.0f) snprintf(tmp, sizeof tmp, "%d ms", (int) std::lround(s * 1000.0f));
        else snprintf(tmp, sizeof tmp, "%.2f s", (double) s);
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "fx_xchar_display")) { nw::dirtCharacterText(tmp, sizeof tmp, P[NW_P_FX_XCHAR]); return write_str(buf, buf_len, tmp); }
    if (!strcmp(key, "fx_xtone_display")) { nw::dirtToneText(tmp, sizeof tmp, P[NW_P_FX_XTONE]); return write_str(buf, buf_len, tmp); }
    return -1;
}

static unsigned display_rev(Instance *inst)
{
    unsigned v = 2166136261u;                                /* FNV-1a over every picture value */
    auto mix = [&v](int x) { v = (v ^ (unsigned) x) * 16777619u; };
    for (int m = 0; m < NW_SLOTS; m++) {
        mix(filter_view(inst, m)); mix(table_view(inst, m)); mix(fold_view(inst, m));
        if ((int) slotParam(inst, m, NW_P_M1_MODEL) == nw::M_WAVETABLE)
            for (int c = 0; c < NW_SCOPE_COLS; c++) mix(scope_value(inst, m, c));
    }
    mix(inst->cpuShown);
    return v;
}

static int nw_get_param(void *instance, const char *key, char *buf, int buf_len)
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
    if (int m = slot_key(key, "wview"); m >= 0) return write_int(buf, buf_len, table_view(inst, m));
    if (key[0] == 'm' && key[1] >= '1' && key[1] <= '0' + NW_SLOTS && !strncmp(key + 2, "_ws", 3)) {
        const int c = atoi(key + 5);
        if (c >= 0 && c < NW_SCOPE_COLS) return write_int(buf, buf_len, scope_value(inst, key[1] - '1', c));
    }
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
        const int v = std::clamp((int) inst->params()[a], 0, nw_params[a].n_options - 1);
        return disp ? write_str(buf, buf_len, nw_params[a].options[v]) : write_int(buf, buf_len, v);
    }
    const nw_param_t *p = &nw_params[idx];
    if (p->type == NW_PATH) {
        char rel[600];
        return write_str(buf, buf_len, saved_path(inst, inst->wt_path[path_slot(idx)], rel, sizeof rel));
    }
    if (p->type == NW_ENUM)
        return write_str(buf, buf_len, p->options[std::clamp((int) inst->params()[idx], 0, p->n_options - 1)]);
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%g", (double) inst->params()[idx]);
    return write_str(buf, buf_len, tmp);
}

static int nw_get_error(void *, char *, int) { return 0; }

/* ------------------------------------------------------------------ MIDI and audio */

static void nw_on_midi(void *instance, const uint8_t *msg, int len, int)
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

static void nw_render_block(void *instance, int16_t *out_lr, int frames)
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

static void *nw_create_instance(const char *module_dir, const char *)
{
    auto *inst = new (std::nothrow) Instance();
    if (!inst) return nullptr;
    if (module_dir) snprintf(inst->module_dir, sizeof inst->module_dir, "%s", module_dir);

    /* the presets now: MPC reads the PRESET menu's size when it inserts the plugin (one small file) */
    load_presets(inst);
    inst->midiPort.open("NorthWest");
    /* everything else is file work: on the worker, while the engine renders silence */
    inst->loader.start();
    inst->loader.post([inst] {
        inst->scanner.scan(inst->module_dir);
        std::atomic_store(&inst->tables, std::make_shared<const std::vector<nw::WtEntry>>(inst->scanner.list()));
        auto init = nw::makeInitTable();
        for (int m = 0; m < NW_SLOTS; m++) inst->engine.setTable(m, init);
        /* a state restored meanwhile named its tables: load them now the list and the loader are live */
        for (int m = 0; m < NW_SLOTS; m++)
            if (inst->wt_path[m][0]) {
                char keep[512];
                snprintf(keep, sizeof keep, "%s", inst->wt_path[m]);
                set_table_path(inst, m, keep);
            }
        inst->ready = true;
        if (g_host && g_host->log) {
            char msg[160];
            snprintf(msg, sizeof msg, "northwest %s ready: %d wavetables, %d presets, MIDI port %s", NW_VERSION,
                     (int) inst->scanner.list().size(), (int) inst->presets.size(),
                     inst->midiPort.isOpen() ? "open" : "unavailable");
            g_host->log(msg);
        }
    });
    return inst;
}

static void nw_destroy_instance(void *instance)
{
    auto *inst = (Instance *) instance;
    if (!inst) return;
    inst->loader.stop();             /* join the worker before the members it publishes into go */
    delete inst;
}

static plugin_api_v2_t g_api = {
    /* .api_version      = */ 2,
    /* .create_instance  = */ nw_create_instance,
    /* .destroy_instance = */ nw_destroy_instance,
    /* .on_midi          = */ nw_on_midi,
    /* .set_param        = */ nw_set_param,
    /* .get_param        = */ nw_get_param,
    /* .get_error        = */ nw_get_error,
    /* .render_block     = */ nw_render_block,
};

/* the framework's entry point (the name comes from the plugin API it implements) */
extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
