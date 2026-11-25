/* NorthWest -- its own MIDI input port, so each machine can be played from its own MPC track and channel.
 *
 * MPC hands a plugin every note-on of its own track on channel 1, so a plugin cannot tell channels apart there. A
 * plugin's own ALSA sequencer port does keep them: it shows in a MIDI track's MIDI OUTPUT list ("NorthWest MIDI In")
 * and the track's notes arrive on its output channel (framework docs/NOTES.md, verified on a Live II, MPC OS 3.9.1;
 * unverified on 2.15.1). A machine whose MIDI CHANNEL is CH n listens there; TRACK (the default) keeps playing from
 * the plugin's own track.
 *
 * libasound is opened at run time (dlopen), so the build needs no ALSA headers and a host without it simply has no
 * port. The few declarations below follow alsa-lib's seq_event.h / seq.h (stable ABI). The port is read without
 * blocking from the audio callback, the way the framework's MIDI port work does it. */
#pragma once

#include <cstdint>
#include <cstring>
#include <dlfcn.h>

namespace nw {

class MidiPort {
public:
    ~MidiPort() { close(); }

    bool open(const char *clientName)
    {
        lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        f_open = (int (*)(void **, const char *, int, int)) dlsym(lib, "snd_seq_open");
        f_close = (int (*)(void *)) dlsym(lib, "snd_seq_close");
        f_name = (int (*)(void *, const char *)) dlsym(lib, "snd_seq_set_client_name");
        f_port = (int (*)(void *, const char *, unsigned, unsigned)) dlsym(lib, "snd_seq_create_simple_port");
        f_input = (int (*)(void *, SeqEvent **)) dlsym(lib, "snd_seq_event_input");
        f_freeConfig = (int (*)()) dlsym(lib, "snd_config_update_free_global");
        if (!f_open || !f_close || !f_name || !f_port || !f_input) { close(); return false; }
        if (f_open(&seq, "default", kOpenInput, kNonBlock) < 0) { seq = nullptr; close(); return false; }
        f_name(seq, clientName);
        if (f_port(seq, "MIDI In", kCapWrite | kCapSubsWrite, kTypeMidiGeneric | kTypeApplication) < 0) {
            close();
            return false;
        }
        return true;
    }

    bool isOpen() const { return seq != nullptr; }

    /* audio thread: every waiting event as MIDI bytes (status with its channel) to fn(msg, len) */
    template <class Fn> void poll(Fn &&fn)
    {
        if (!seq) return;
        for (int guard = 0; guard < 256; guard++) {
            SeqEvent *ev = nullptr;
            if (f_input(seq, &ev) < 0 || !ev) return;      /* -EAGAIN: nothing more */
            uint8_t m[3];
            const uint8_t ch = ev->data[0] & 0x0F;
            switch (ev->type) {
            case kNoteOn:  m[0] = 0x90 | ch; m[1] = ev->data[1] & 0x7F; m[2] = ev->data[2] & 0x7F; fn(m, 3); break;
            case kNoteOff: m[0] = 0x80 | ch; m[1] = ev->data[1] & 0x7F; m[2] = 0; fn(m, 3); break;
            case kController: {
                uint32_t param; int32_t value;
                std::memcpy(&param, ev->data + 4, 4); std::memcpy(&value, ev->data + 8, 4);
                m[0] = 0xB0 | ch; m[1] = (uint8_t) (param & 0x7F); m[2] = (uint8_t) (value & 0x7F); fn(m, 3); break; }
            case kChanPress: {
                int32_t value; std::memcpy(&value, ev->data + 8, 4);
                m[0] = 0xD0 | ch; m[1] = (uint8_t) (value & 0x7F); fn(m, 2); break; }
            case kPitchBend: {
                int32_t value; std::memcpy(&value, ev->data + 8, 4);
                const int v = value + 8192;
                m[0] = 0xE0 | ch; m[1] = (uint8_t) (v & 0x7F); m[2] = (uint8_t) ((v >> 7) & 0x7F); fn(m, 3); break; }
            default: break;
            }
        }
    }

    void close()
    {
        if (!lib) return;
        if (seq && f_close) f_close(seq);
        seq = nullptr;
        if (f_freeConfig) f_freeConfig();          /* alsa-lib keeps its parsed configuration until told */
        dlclose(lib);
        lib = nullptr;
        f_open = nullptr; f_close = nullptr; f_name = nullptr; f_port = nullptr; f_input = nullptr; f_freeConfig = nullptr;
    }

private:
    /* snd_seq_event_t: type, flags, tag, queue, an 8-byte time, source and dest addresses, 12 bytes of data
     * (note: channel, note, velocity, off velocity, duration; control: channel, 3 unused, param, value) */
    struct SeqEvent {
        uint8_t type, flags, tag, queue;
        uint8_t time[8];
        uint8_t source[2], dest[2];
        uint8_t data[12];
    };
    static_assert(sizeof(SeqEvent) == 28, "snd_seq_event_t is 28 bytes");
    enum { kNoteOn = 6, kNoteOff = 7, kController = 10, kChanPress = 12, kPitchBend = 13 };
    enum { kOpenInput = 2, kNonBlock = 1 };
    enum : unsigned { kCapWrite = 1u << 1, kCapSubsWrite = 1u << 6, kTypeMidiGeneric = 1u << 1,
                      kTypeApplication = 1u << 20 };

    void *lib = nullptr, *seq = nullptr;
    int (*f_open)(void **, const char *, int, int) = nullptr;
    int (*f_close)(void *) = nullptr;
    int (*f_name)(void *, const char *) = nullptr;
    int (*f_port)(void *, const char *, unsigned, unsigned) = nullptr;
    int (*f_input)(void *, SeqEvent **) = nullptr;
    int (*f_freeConfig)() = nullptr;
};

} // namespace nw
