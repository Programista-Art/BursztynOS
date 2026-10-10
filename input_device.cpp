/*
 * Bursztyn OS - Implementacja Abstrakcji Urzadzen Wejsciowych (InputDevice)
 *
 * Plik: input_device.cpp
 */

#include "input_device.h"
#include "pointer_coordinates.h"
#include "xhci_pointing_diag.h"
#include "mysz_input.h"
#include "abs_map_diag.h"
#if BURSZTYNDIAGABSMAP
#include "sterowniki/czas/hpet.h"
void grafika_abs_map_geometria();
#endif

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstring>
#include <cstdio>
#else
extern "C" void* memset(void* s, int c, size_t n);
extern "C" int strcmp(const char* s1, const char* s2);
extern "C" size_t strlen(const char* s);
#endif

void klog_serial(const char* msg);

/* Interfejsy z podsystemu grafiki i klawiatury Bursztyn OS */
extern "C" void zaktualizuj_mysze_zrodlo(int dx, int dy, uint8_t przyciski, const char* zrodlo);
extern "C" void zaktualizuj_kolo_myszy(int pion, int poziom, uint8_t przyciski);
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x, int y, uint8_t przyciski, const char* zrodlo);
int grafika_pobierz_szerokosc(void);
int grafika_pobierz_wysokosc(void);
extern "C" void klawiatura_obsluz_klawisz(uint16_t keycode, uint32_t value);

namespace {

constexpr size_t MAX_INPUT_DEVICES = 16;
static InputDevice* s_devices[MAX_INPUT_DEVICES] = {};
static size_t s_device_count = 0;
static InputDevice* s_pointer_owner = nullptr;
static bool s_pointer_delivery_ready = false;
#if BURSZTYNDIAGXHCIPOINTING
static unsigned s_pointer_traces[MAX_INPUT_DEVICES]{};
#endif
static void pointer_trace(InputDevice* dev,InputPointerMode mode,int32_t raw_x,int32_t raw_y,int before_x,int before_y,int result) {
#if BURSZTYNDIAGXHCIPOINTING
    size_t slot=0;while(slot<MAX_INPUT_DEVICES && s_devices[slot]!=dev)++slot;
    if(slot==MAX_INPUT_DEVICES || s_pointer_traces[slot]>=32)return;
    ++s_pointer_traces[slot];
    const auto* owner=input_get_pointer_owner();
    const int64_t v[]={dev->device_id,dev->bus_type,owner?int64_t(owner->device_id):-1,
        uint32_t(raw_x),uint32_t(raw_y),raw_x,raw_y,before_x,before_y,
        grafika_pobierz_mysz_x(),grafika_pobierz_mysz_y(),grafika_pobierz_szerokosc(),
        grafika_pobierz_wysokosc(),result,dev->pointer_state,dev->usb_controller};
    pointing_diag(mode==INPUT_POINTER_ABSOLUTE ?
        "[POINTER ABSOLUTE] id/bus/owner/rawbitsX/Y/signedX/Y/beforeX/Y/afterX/Y/width/height/accepted(0=owner-reject,2=deferred)/state/controller" :
        "[POINTER RELATIVE] id/bus/owner/rawbitsX/Y/signedX/Y/beforeX/Y/afterX/Y/width/height/accepted(0=owner-reject,2=deferred)/state/controller",v,16);
#else
    (void)dev;(void)mode;(void)raw_x;(void)raw_y;(void)before_x;(void)before_y;(void)result;
#endif
}
static int32_t input_accumulate_delta(int32_t previous, int32_t delta) {
    const int64_t sum = int64_t(previous) + delta;
    // Keep negation valid too: the graphics convention negates Y deltas.
    return sum > INT32_MAX ? INT32_MAX : sum < -INT32_MAX ? -INT32_MAX : int32_t(sum);
}

#if BURSZTYNDIAGABSMAP
static AbsMapSample s_abs_maps[MAX_INPUT_DEVICES]{};
static AbsMapSample* abs_sample(InputDevice* dev) {
    for(size_t i=0;i<MAX_INPUT_DEVICES;++i)if(s_devices[i]==dev)return &s_abs_maps[i];
    return nullptr;
}
static void abs_report(InputDevice* dev,uint64_t now) {
    auto* a=abs_sample(dev);if(!a||!a->due(now))return;
    char buf[640]{};size_t n=0;
    auto text=[&](const char* s){while(*s&&n+1<sizeof(buf))buf[n++]=*s++;};
    auto num=[&](int64_t v){if(v<0){text("-");v=-v;}char b[24];unsigned i=0;do{b[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=b[--i];};
    auto pair=[&](const char* k,int64_t x,int64_t y){text(k);num(x);text(",");num(y);text(" ");};
    text("[ABS MAP] dev='");text(dev->name);text("' mode=ABS ");
    pair("raw=",a->raw_x,a->raw_y);pair("xrange=",a->min_x,a->max_x);pair("yrange=",a->min_y,a->max_y);
    pair("valid=",a->valid_x,a->valid_y);pair("mapped=",a->mapped_x,a->mapped_y);
    pair("logical=",grafika_pobierz_mysz_x(),grafika_pobierz_mysz_y());pair("screen=",a->width,a->height);
    pair("observed_x=",a->low_x,a->high_x);pair("observed_y=",a->low_y,a->high_y);
    pair("stalled=",a->stalled_x,a->stalled_y);text("near_edge=");num(a->near_edge);
    pair(" hit_x_min_max=",bool(a->endpoints&1),bool(a->endpoints&2));
    pair("hit_y_min_max=",bool(a->endpoints&4),bool(a->endpoints&8));text("reports=");num(a->reports);
    text(" interval_ns=");num(now-a->started);text("\n");klog_serial(buf);
    grafika_abs_map_geometria();a->consumed(now);
}
#endif

static inline bool str_rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return (*a == '\0' && *b == '\0');
}

static uint32_t input_calc_priority(const InputDevice* dev) {
    if (!dev) return 0;
    if (dev->bus_type == INPUT_BUS_VIRTIO) return 30;
    if (dev->bus_type == INPUT_BUS_USB) return 20;
    if (dev->bus_type == INPUT_BUS_PS2) return 10;
    return 0;
}

static int32_t s_last_tablet_raw_x = -1;
static int32_t s_last_tablet_raw_y = -1;

extern "C" void input_device_pobierz_ostatnie_abs(int32_t* rx, int32_t* ry) {
    if (rx) *rx = s_last_tablet_raw_x;
    if (ry) *ry = s_last_tablet_raw_y;
}

static inline bool input_is_pointer_device(const InputDevice* dev) {
    if (!dev) return false;
    return (dev->kinds & (INPUT_DEVICE_KIND_MOUSE | INPUT_DEVICE_KIND_TABLET)) != 0;
}

static void input_recalc_pointer_owner(void) {
    InputDevice* best = nullptr;
    uint32_t best_prio = 0;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        InputDevice* d = s_devices[i];
        if (d && d->registered && d->pointer_state == INPUT_POINTER_PROVEN_ACTIVE && input_is_pointer_device(d)) {
            uint32_t prio = input_calc_priority(d);
            if (prio > best_prio || best == nullptr) {
                best = d;
                best_prio = prio;
            }
        }
    }
    if (s_pointer_owner != best) {
        // Pending input belongs to its old owner, never replay it on fallback.
        if (s_pointer_owner) {
            s_pointer_owner->rel_x = s_pointer_owner->rel_y = 0;
            s_pointer_owner->rel_wheel = s_pointer_owner->rel_hwheel = 0;
            s_pointer_owner->has_abs_x = s_pointer_owner->has_abs_y = false;
        }
        s_pointer_owner = best;
        pointing_count(pointing_counters.owner_transitions);
        pointing_snapshot("owner-change");
        char buf[160]{};
        char* p = buf;
        char* e = buf + sizeof(buf);
        auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
        auto app_d = [&](uint32_t v) {
            char b[12]; int idx = 0;
            do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
            while (idx > 0 && p + 1 < e) *p++ = b[--idx];
        };
        app("[INPUT OWNER] Active pointer owner: '");
        app(best ? best->name : "none");
        app("' bus=");
        app(best ? input_bus_name(best->bus_type) : "none");
        app(" id=");
        app_d(best ? best->device_id : 0);
        app(" prio=");
        app_d(best_prio);
        *p = '\0';
        klog_serial(buf);
    }
}

#if defined(BURSZTYNDIAGINPUTPOINTER) && BURSZTYNDIAGINPUTPOINTER
static void log_pointer_event(const InputDevice* dev, int dx, int dy, int res_x, int res_y, bool accepted) {
    static uint32_t s_log_count = 0;
    static uint32_t s_ignored_log_count = 0;

    const int sw = grafika_pobierz_szerokosc();
    const int sh = grafika_pobierz_wysokosc();
    const bool is_corner_or_edge = (res_x <= 0 || res_y <= 0 ||
        (sw > 0 && res_x >= (sw - 1)) ||
        (sh > 0 && res_y >= (sh - 1)));

    if (accepted) {
        if (s_log_count >= 5000 && !is_corner_or_edge && (s_log_count % 100 != 0)) {
            ++s_log_count;
            return;
        }
        ++s_log_count;
    } else {
        if (s_ignored_log_count >= 50 && (s_ignored_log_count % 50 != 0)) {
            ++s_ignored_log_count;
            return;
        }
        ++s_ignored_log_count;
    }

    char buf[180]{};
    char* p = buf;
    char* e = buf + sizeof(buf);
    auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
    auto app_i = [&](int v) {
        if (v < 0) {
            if (p + 1 < e) *p++ = '-';
            v = -v;
        }
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };

    app("[INPUT POINTER] dev='");
    app(dev ? dev->name : "null");
    app("' bus=");
    app(dev ? input_bus_name(dev->bus_type) : "UNKNOWN");
    app(" id=");
    app_i(dev ? static_cast<int>(dev->device_id) : 0);
    app(" dx=");
    app_i(dx);
    app(" dy=");
    app_i(dy);
    app(" res=");
    app_i(res_x);
    app(",");
    app_i(res_y);
    app(" state=");
    if (accepted) {
        app("ACCEPTED");
    } else {
        app("IGNORED (owner='");
        app(s_pointer_owner ? s_pointer_owner->name : "none");
        app("')");
    }
    *p = '\0';
    klog_serial(buf);
}
#else
static inline void log_pointer_event(const InputDevice*, int, int, int, int, bool) {}
#endif

#if defined(BURSZTYN_DIAG_RAW_VIRTIO_INPUT) && BURSZTYN_DIAG_RAW_VIRTIO_INPUT
static void log_raw_virtio_rel(const InputDevice* dev, const InputEvent* ev, int32_t delta) {
    if (!dev || dev->bus_type != INPUT_BUS_VIRTIO) return;
    static uint32_t s_rel_log_count = 0;
    const bool is_large = (delta < -200 || delta > 200);
    if (s_rel_log_count >= 50 && !is_large) return;
    ++s_rel_log_count;

    char buf[180]{};
    char* p = buf;
    char* e = buf + sizeof(buf);
    auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
    auto app_hex = [&](uint32_t v) {
        app("0x");
        for (int i = 7; i >= 0; --i) {
            uint8_t nib = (v >> (i * 4)) & 0xFU;
            if (p + 1 < e) *p++ = (nib < 10) ? static_cast<char>('0' + nib) : static_cast<char>('A' + (nib - 10));
        }
    };
    auto app_u = [&](uint32_t v) {
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };
    auto app_i = [&](int32_t v) {
        if (v < 0) {
            if (p + 1 < e) *p++ = '-';
            v = -v;
        }
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };

    app("[VIRTIO RAW REL] dev='");
    app(dev->name);
    app("' type=");
    app_u(ev->type);
    app(" code=");
    app_u(ev->code);
    if (ev->code == INPUT_REL_X) app("(REL_X)");
    else if (ev->code == INPUT_REL_Y) app("(REL_Y)");
    else if (ev->code == INPUT_REL_WHEEL) app("(REL_WHEEL)");
    app(" hex=");
    app_hex(ev->value);
    app(" u32=");
    app_u(ev->value);
    app(" i32=");
    app_i(delta);
    app(" acc_x=");
    app_i(dev->rel_x);
    app(" acc_y=");
    app_i(dev->rel_y);
    *p = '\0';
    klog_serial(buf);
}

static void log_raw_virtio_syn(const InputDevice* dev, int32_t commit_dx, int32_t commit_dy) {
    if (!dev || dev->bus_type != INPUT_BUS_VIRTIO) return;
    static uint32_t s_syn_log_count = 0;
    const bool is_large = (commit_dx < -200 || commit_dx > 200 || commit_dy < -200 || commit_dy > 200);
    if (s_syn_log_count >= 50 && !is_large) return;
    ++s_syn_log_count;

    char buf[160]{};
    char* p = buf;
    char* e = buf + sizeof(buf);
    auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
    auto app_i = [&](int32_t v) {
        if (v < 0) {
            if (p + 1 < e) *p++ = '-';
            v = -v;
        }
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };

    app("[VIRTIO RAW SYN] dev='");
    app(dev->name);
    app("' SYN_REPORT commit dx=");
    app_i(commit_dx);
    app(" dy=");
    app_i(commit_dy);
    app(" -> acc reset to 0,0");
    *p = '\0';
    klog_serial(buf);
}

static void log_raw_virtio_abs(const InputDevice* dev, const InputEvent* ev, int32_t coord) {
    if (!dev || dev->bus_type != INPUT_BUS_VIRTIO) return;
    static uint32_t s_abs_log_count = 0;
    if (s_abs_log_count >= 30) return;
    ++s_abs_log_count;

    char buf[160]{};
    char* p = buf;
    char* e = buf + sizeof(buf);
    auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
    auto app_u = [&](uint32_t v) {
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };
    auto app_i = [&](int32_t v) {
        if (v < 0) {
            if (p + 1 < e) *p++ = '-';
            v = -v;
        }
        char b[12]; int idx = 0;
        do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (idx > 0 && p + 1 < e) *p++ = b[--idx];
    };

    app("[VIRTIO RAW ABS] dev='");
    app(dev->name);
    app("' axis=");
    if (ev->code == INPUT_ABS_X) app("ABS_X");
    else if (ev->code == INPUT_ABS_Y) app("ABS_Y");
    else app_u(ev->code);
    app(" raw=");
    app_i(coord);
    app(" min=");
    app_i(ev->code == INPUT_ABS_X ? dev->abs_x.min : dev->abs_y.min);
    app(" max=");
    app_i(ev->code == INPUT_ABS_X ? dev->abs_x.max : dev->abs_y.max);
    *p = '\0';
    klog_serial(buf);
}
#else
static inline void log_raw_virtio_rel(const InputDevice*, const InputEvent*, int32_t) {}
static inline void log_raw_virtio_syn(const InputDevice*, int32_t, int32_t) {}
static inline void log_raw_virtio_abs(const InputDevice*, const InputEvent*, int32_t) {}
#endif

} // namespace

extern "C" {

InputDevice* input_get_pointer_owner(void) {
    if (!s_pointer_owner) {
        input_recalc_pointer_owner();
    }
    return s_pointer_owner;
}

const char* input_bus_name(uint32_t bus_type) {
    switch (bus_type) {
        case INPUT_BUS_PS2:    return "PS2";
        case INPUT_BUS_USB:    return "USB";
        case INPUT_BUS_VIRTIO: return "VIRTIO";
        default:               return "UNKNOWN";
    }
}

bool input_register_device(InputDevice* dev) {
    if (!dev || dev->registered) return false;
    if (s_device_count >= MAX_INPUT_DEVICES) return false;
    // Compatibility for callers declaring capabilities rather than the mode.
    // Never derive coordinate semantics from transport, priority or values.
    if (dev->pointer_mode == INPUT_POINTER_UNSPECIFIED)
        dev->pointer_mode = (dev->kinds & INPUT_DEVICE_KIND_TABLET) ?
            ((dev->kinds & INPUT_DEVICE_KIND_MOUSE) ? INPUT_POINTER_RELATIVE_AND_ABSOLUTE : INPUT_POINTER_ABSOLUTE) : INPUT_POINTER_RELATIVE;
    if (dev->pointer_mode != INPUT_POINTER_RELATIVE &&
        dev->pointer_mode != INPUT_POINTER_ABSOLUTE &&
        dev->pointer_mode != INPUT_POINTER_RELATIVE_AND_ABSOLUTE) return false;

    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] == nullptr) {
            s_devices[i] = dev;
#if BURSZTYNDIAGABSMAP
            s_abs_maps[i]=AbsMapSample{};
#endif
#if BURSZTYNDIAGXHCIPOINTING
            s_pointer_traces[i]=0;
#endif
            dev->registered = true;
            dev->pointer_state = dev->bus_type == INPUT_BUS_USB ?
                INPUT_POINTER_REGISTERED_INACTIVE : INPUT_POINTER_PROVEN_ACTIVE;
            dev->rel_x = 0;
            dev->rel_y = 0;
    dev->rel_wheel = 0;
    dev->rel_hwheel = 0;
            dev->abs_val_x = 0;
            dev->abs_val_y = 0;
            dev->has_abs_x = false;
            dev->has_abs_y = false;
            dev->buttons_state = 0;
            dev->prev_buttons_state = 0;
            s_device_count++;

            char buf[160]{};
            char* p = buf;
            char* e = buf + sizeof(buf);
            auto app = [&](const char* s) { while (*s && p + 1 < e) *p++ = *s++; };
            auto app_d = [&](uint32_t v) {
                char b[12]; int idx = 0;
                do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
                while (idx > 0 && p + 1 < e) *p++ = b[--idx];
            };
            app("[INPUT REGISTER] dev='");
            app(dev->name);
            app("' bus=");
            app(input_bus_name(dev->bus_type));
            app(" id=");
            app_d(dev->device_id);
            app(" kinds=0x");
            app_d(dev->kinds);
            app(" prio=");
            app_d(input_calc_priority(dev));
            *p = '\0';
            klog_serial(buf);

            input_recalc_pointer_owner();
            return true;
        }
    }
    return false;
}

bool input_arm_pointer(InputDevice* dev) {
    if (!dev || !dev->registered || dev->bus_type != INPUT_BUS_USB ||
        !input_is_pointer_device(dev)) return false;
    if (dev->pointer_state == INPUT_POINTER_REGISTERED_INACTIVE)
        dev->pointer_state = INPUT_POINTER_ARMED;
    return true;
}
bool input_prove_pointer(InputDevice* dev) {
    if (!dev || !dev->registered || dev->bus_type != INPUT_BUS_USB ||
        !input_is_pointer_device(dev) || dev->pointer_state == INPUT_POINTER_REGISTERED_INACTIVE)
        return false;
    if (dev->pointer_state != INPUT_POINTER_PROVEN_ACTIVE) {
        dev->pointer_state = INPUT_POINTER_PROVEN_ACTIVE;
        pointing_count(pointing_counters.proven);
        pointing_snapshot("first-valid-report");
        input_recalc_pointer_owner();
    }
    return true;
}

bool input_unregister_device(InputDevice* dev) {
    if (!dev || !dev->registered) return false;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] == dev) {
            s_devices[i] = nullptr;
            dev->registered = false;
            dev->pointer_state = INPUT_POINTER_REGISTERED_INACTIVE;
            if (s_device_count > 0) s_device_count--;
            input_recalc_pointer_owner();
            return true;
        }
    }
    return false;
}

size_t input_get_device_count(void) {
    return s_device_count;
}

InputDevice* input_get_device(size_t index) {
    if (index >= MAX_INPUT_DEVICES) return nullptr;
    return s_devices[index];
}

InputDevice* input_find_device_by_name(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] && str_rowne(s_devices[i]->name, name)) {
            return s_devices[i];
        }
    }
    return nullptr;
}

void input_dispatch_event(InputDevice* dev, const InputEvent* ev) {
    if (!dev || !ev || !dev->registered) return;

    switch (ev->type) {
        case INPUT_EV_KEY: {
            const uint16_t code = ev->code;
            const uint32_t val = ev->value;
            if (val > 2) break; // Only release, press and repeat are defined.

            // Przyciski myszy / urzadzenia wskazujacego
            if (code == INPUT_BTN_LEFT || code == INPUT_BTN_TOUCH) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_LEWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_LEWY;
                }
            } else if (code == INPUT_BTN_RIGHT) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_PRAWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_PRAWY;
                }
            } else if (code == INPUT_BTN_MIDDLE) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_SRODKOWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_SRODKOWY;
                }
            } else if (code == INPUT_BTN_SIDE || code == INPUT_BTN_EXTRA) {
                const uint8_t bit=uint8_t(1U << (code-INPUT_BTN_LEFT));
                if(val)dev->buttons_state|=bit;
                else dev->buttons_state &= uint8_t(~bit);
            } else if (code >= INPUT_BTN_MISC && code < 0x200U) {
                // Unsupported buttons must not manufacture left-click edges.
            } else if (code > 0 && code < INPUT_BTN_MISC &&
                       (dev->kinds & INPUT_DEVICE_KIND_KEYBOARD)) {
                // Standardowe klawisze klawiatury
                klawiatura_obsluz_klawisz(code, val);
            }
            break;
        }

        case INPUT_EV_REL: {
            const int32_t delta = input_signed_value(ev->value);
            if ((ev->code == INPUT_REL_X || ev->code == INPUT_REL_Y) &&
                !(dev->pointer_mode & INPUT_POINTER_RELATIVE)) break;
            if (ev->code == INPUT_REL_X) {
                dev->rel_x = input_accumulate_delta(dev->rel_x, delta);
            } else if (ev->code == INPUT_REL_Y) {
                dev->rel_y = input_accumulate_delta(dev->rel_y, delta);
            } else if (ev->code == INPUT_REL_WHEEL) {
                dev->rel_wheel = input_accumulate_delta(dev->rel_wheel, delta);
            } else if (ev->code == INPUT_REL_HWHEEL) {
                dev->rel_hwheel = input_accumulate_delta(dev->rel_hwheel, delta);
            }
            log_raw_virtio_rel(dev, ev, delta);
            break;
        }

        case INPUT_EV_ABS: {
            if (!(dev->pointer_mode & INPUT_POINTER_ABSOLUTE)) break;
            const int32_t coord = input_signed_value(ev->value);
            if (ev->code == INPUT_ABS_X) {
                dev->abs_val_x = coord;
                dev->has_abs_x = true;
                s_last_tablet_raw_x = coord;
            } else if (ev->code == INPUT_ABS_Y) {
                dev->abs_val_y = coord;
                dev->has_abs_y = true;
                s_last_tablet_raw_y = coord;
            }
            log_raw_virtio_abs(dev, ev, coord);
            break;
        }

        case INPUT_EV_SYN: {
            if (ev->code == INPUT_SYN_REPORT) {
                const char* src_name = (dev->name[0] != '\0') ? dev->name : "input";

                const bool has_pointer_activity = (dev->rel_x != 0 || dev->rel_y != 0 ||
                    dev->rel_wheel != 0 || dev->rel_hwheel != 0 || dev->has_abs_x || dev->has_abs_y ||
                    dev->buttons_state != dev->prev_buttons_state);

                if (has_pointer_activity) {
                    if (!s_pointer_owner) {
                        input_recalc_pointer_owner();
                    }
                    const bool is_owner = s_pointer_owner == dev;
                    const int before_x=grafika_pobierz_mysz_x(),before_y=grafika_pobierz_mysz_y();
                    const InputPointerMode trace_mode=(dev->has_abs_x || dev->has_abs_y ||
                        dev->pointer_mode==INPUT_POINTER_ABSOLUTE) ? INPUT_POINTER_ABSOLUTE : INPUT_POINTER_RELATIVE;
                    const int32_t trace_x=trace_mode==INPUT_POINTER_ABSOLUTE?dev->abs_val_x:dev->rel_x;
                    const int32_t trace_y=trace_mode==INPUT_POINTER_ABSOLUTE?dev->abs_val_y:dev->rel_y;
                    pointing_count(is_owner ? pointing_counters.owner_accepted : pointing_counters.nonowner_ignored);
                    if (!is_owner && dev->bus_type == INPUT_BUS_PS2 && !pointing_counters.ps2_drop_seen) {
                        pointing_counters.ps2_drop_seen = true; pointing_snapshot("ps2-owner-drop");
                    }

                    if (is_owner && !input_pointer_geometry_ready()) {
                        // Keep signed motion / last absolute axes and buttons until
                        // display bootstrap publishes usable cursor geometry.
                        pointer_trace(dev,trace_mode,trace_x,trace_y,before_x,before_y,2);
                        return;
                    }
                    if (is_owner) {
                        // 1. Zdarzenia myszy wzglednej (ruch lub zmiana przyciskow)
                        if (dev->rel_x != 0 || dev->rel_y != 0) {
                            const int dx = dev->rel_x;
                            const int dy = dev->rel_y;
                            log_raw_virtio_syn(dev, dx, dy);
                            zaktualizuj_mysze_zrodlo(dev->rel_x, -dev->rel_y, dev->buttons_state, src_name);
                            dev->rel_x = 0;
                            dev->rel_y = 0;
                            const int rx = grafika_pobierz_mysz_x();
                            const int ry = grafika_pobierz_mysz_y();
                            log_pointer_event(dev, dx, dy, rx, ry, true);
                        } else if (!dev->has_abs_x && !dev->has_abs_y && dev->buttons_state != dev->prev_buttons_state) {
                            zaktualizuj_mysze_zrodlo(0, 0, dev->buttons_state, src_name);
                        }

                        // 2. Kolo myszy
                        if (dev->rel_wheel != 0 || dev->rel_hwheel != 0) {
                            zaktualizuj_kolo_myszy(dev->rel_wheel, dev->rel_hwheel, dev->buttons_state);
                            dev->rel_wheel = 0;
                            dev->rel_hwheel = 0;
                        }

                        // 3. Pozycjonowanie bezwzgledne (tablet / ekran dotykowy)
                        if (dev->has_abs_x || dev->has_abs_y) {
                            const int screen_w = grafika_pobierz_szerokosc();
                            const int screen_h = grafika_pobierz_wysokosc();

                            if (screen_w > 0 && screen_h > 0) {
                                int target_x = dev->has_abs_x ? dev->abs_val_x : grafika_pobierz_mysz_x();
                                int target_y = dev->has_abs_y ? dev->abs_val_y : grafika_pobierz_mysz_y();
                                if (dev->abs_x.max <= dev->abs_x.min) target_x = grafika_pobierz_mysz_x();
                                if (dev->abs_y.max <= dev->abs_y.min) target_y = grafika_pobierz_mysz_y();

                                // Skalowanie osi X z pelna ochrona przed overflow
                                if (dev->has_abs_x && dev->abs_x.max > dev->abs_x.min) {
                                    if (target_x < dev->abs_x.min) target_x = dev->abs_x.min;
                                    if (target_x > dev->abs_x.max) target_x = dev->abs_x.max;
                                    const int64_t span_in = static_cast<int64_t>(dev->abs_x.max) - dev->abs_x.min;
                                    const int64_t val_rel = static_cast<int64_t>(target_x) - dev->abs_x.min;
                                    const int64_t max_out = screen_w - 1;
                                    target_x = static_cast<int>((val_rel * max_out) / span_in);
                                }

                                // Skalowanie osi Y z pelna ochrona przed overflow
                                if (dev->has_abs_y && dev->abs_y.max > dev->abs_y.min) {
                                    if (target_y < dev->abs_y.min) target_y = dev->abs_y.min;
                                    if (target_y > dev->abs_y.max) target_y = dev->abs_y.max;
                                    const int64_t span_in = static_cast<int64_t>(dev->abs_y.max) - dev->abs_y.min;
                                    const int64_t val_rel = static_cast<int64_t>(target_y) - dev->abs_y.min;
                                    const int64_t max_out = screen_h - 1;
                                    target_y = static_cast<int>((val_rel * max_out) / span_in);
                                }

                                const bool can_abs_x = dev->has_abs_x && (dev->abs_x.max > dev->abs_x.min);
                                const bool can_abs_y = dev->has_abs_y && (dev->abs_y.max > dev->abs_y.min);
                                // Invalid/unavailable ranges preserve the current
                                // pixel position; never send raw/extreme ABS as pixels.
                                if (target_x < 0) target_x = 0;
                                if (target_y < 0) target_y = 0;
                                if (target_x >= screen_w) target_x = screen_w - 1;
                                if (target_y >= screen_h) target_y = screen_h - 1;
                                if ((dev->kinds & INPUT_DEVICE_KIND_TABLET) != 0 || can_abs_x || can_abs_y) {
#if BURSZTYNDIAGABSMAP
                                    if(auto* a=abs_sample(dev))a->record(dev->abs_val_x,dev->abs_val_y,
                                        dev->has_abs_x,dev->has_abs_y,dev->abs_x.min,dev->abs_x.max,
                                        dev->abs_y.min,dev->abs_y.max,target_x,target_y,screen_w,screen_h,
                                        hpet_dostepny()?czas_monotoniczny_ns():0);
#endif
                                    zaktualizuj_mysze_pozycja_zrodlo(target_x, target_y, dev->buttons_state, src_name);
                                    const int rx = grafika_pobierz_mysz_x();
                                    const int ry = grafika_pobierz_mysz_y();
                                    log_pointer_event(dev, 0, 0, rx, ry, true);
                                }
                            }

                            dev->has_abs_x = false;
                            dev->has_abs_y = false;
                        }
                    } else {
                        // IGNORED: Urzadzenie o nizszym priorytecie nie wplywa na logiczny kursor
                        const int dx = dev->rel_x;
                        const int dy = dev->rel_y;
                        const int rx = grafika_pobierz_mysz_x();
                        const int ry = grafika_pobierz_mysz_y();
                        log_pointer_event(dev, dx, dy, rx, ry, false);

                        dev->rel_x = 0;
                        dev->rel_y = 0;
                        dev->rel_wheel = 0;
                        dev->rel_hwheel = 0;
                        dev->has_abs_x = false;
                        dev->has_abs_y = false;
                    }
                    pointer_trace(dev,trace_mode,trace_x,trace_y,before_x,before_y,is_owner?1:0);
                }

                dev->prev_buttons_state = dev->buttons_state;
            }
            break;
        }

        default:
            // Nieobslugiwane lub opcjonalne typy zdarzen (EV_MSC, EV_LED itp.) sa bezpiecznie ignorowane
            break;
    }
}

void input_dispatch_events(InputDevice* dev, const InputEvent* evs, size_t count) {
    if (!dev || !evs || count == 0) return;
    for (size_t i = 0; i < count; ++i) {
        input_dispatch_event(dev, &evs[i]);
    }
}

#if BURSZTYNDIAGABSMAP
void input_abs_map_diag_obsluz() {
    const auto now=hpet_dostepny()?czas_monotoniczny_ns():0;
    for(size_t i=0;i<MAX_INPUT_DEVICES;++i)if(s_devices[i])abs_report(s_devices[i],now);
}
#endif

void input_set_pointer_ready(bool ready) { s_pointer_delivery_ready = ready; }
bool input_pointer_geometry_ready(void) {
    return s_pointer_delivery_ready && grafika_pobierz_szerokosc() > 0 &&
        grafika_pobierz_wysokosc() > 0;
}
void input_pointer_boot_diagnostic(void) {
#if BURSZTYNDIAGXHCIPOINTING
    const InputDevice* ps2 = input_find_device_by_name("ps2-mouse");
    int ohci=-1,xhci=-1;
    for(size_t i=0;i<input_get_device_count();++i) {
        const auto* d=input_get_device(i);
        if(d && d->bus_type==INPUT_BUS_USB && input_is_pointer_device(d)) {
            if(d->usb_controller==INPUT_USB_CONTROLLER_OHCI)ohci=d->pointer_state;
            if(d->usb_controller==INPUT_USB_CONTROLLER_XHCI)xhci=d->pointer_state;
        }
    }
    const auto* owner=input_get_pointer_owner();
    const int64_t v[]={s_pointer_delivery_ready,input_pointer_geometry_ready(),
        grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),ps2!=nullptr,
        ps2 && ps2->pointer_state==INPUT_POINTER_PROVEN_ACTIVE,owner?int64_t(owner->device_id):-1,ohci,xhci};
    pointing_diag("[POINTER BOOT] INPUTSUBSYSTEMREADY/CURSORGEOMETRYREADY/SCREENWIDTH/SCREENHEIGHT/PS2REGISTERED/PS2ELIGIBLE/CURRENTPOINTEROWNER/OHCIPOINTERSTATE/XHCIPOINTERSTATE",v,9);
#endif
}
void input_service_all(void) {
    if (input_pointer_geometry_ready() && s_pointer_owner) {
        const InputEvent sync{INPUT_EV_SYN, INPUT_SYN_REPORT, 0};
        input_dispatch_event(s_pointer_owner, &sync);
    }
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] && s_devices[i]->poll_fn &&
            (!input_is_pointer_device(s_devices[i]) ||
             (s_devices[i]->kinds & INPUT_DEVICE_KIND_KEYBOARD) || input_pointer_geometry_ready())) {
            s_devices[i]->poll_fn(s_devices[i]);
        }
    }
}

void input_reset_all(void) {
#if BURSZTYNDIAGABSMAP
    for(auto& a:s_abs_maps)a=AbsMapSample{};
#endif
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i]) {
            s_devices[i]->registered = false;
            s_devices[i]->pointer_state = INPUT_POINTER_REGISTERED_INACTIVE;
            s_devices[i] = nullptr;
        }
    }
    s_device_count = 0;
    s_pointer_owner = nullptr;
}

uint16_t input_ps2_scancode_to_linux(bool is_e0, uint8_t scancode) {
    const uint8_t code = scancode & 0x7FU;
    if (is_e0) {
        switch (code) {
            case 0x38: return INPUT_KEY_RIGHTALT;   // 100
            case 0x1D: return INPUT_KEY_RIGHTCTRL;  // 97
            case 0x5B: return INPUT_KEY_LEFTMETA;   // 125
            case 0x5C: return INPUT_KEY_RIGHTMETA;  // 126
            case 0x48: return INPUT_KEY_UP;         // 103
            case 0x50: return INPUT_KEY_DOWN;       // 108
            case 0x4B: return INPUT_KEY_LEFT;       // 105
            case 0x4D: return INPUT_KEY_RIGHT;      // 106
            case 0x47: return INPUT_KEY_HOME;       // 102
            case 0x4F: return INPUT_KEY_END;        // 107
            case 0x49: return INPUT_KEY_PAGEUP;     // 104
            case 0x51: return INPUT_KEY_PAGEDOWN;   // 109
            case 0x52: return INPUT_KEY_INSERT;     // 110
            case 0x53: return INPUT_KEY_DELETE;     // 111
            case 0x1C: return INPUT_KEY_KPENTER;    // 96
            case 0x35: return INPUT_KEY_KPSLASH;    // 98
            default:   return 0;
        }
    } else {
        switch (code) {
            case 0x2A: return INPUT_KEY_LEFTSHIFT;  // 42
            case 0x36: return INPUT_KEY_RIGHTSHIFT; // 54
            case 0x1D: return INPUT_KEY_LEFTCTRL;   // 29
            case 0x38: return INPUT_KEY_LEFTALT;    // 56
            case 0x3A: return INPUT_KEY_CAPSLOCK;   // 58
            case 0x45: return INPUT_KEY_NUMLOCK;    // 69
            default:
                if (code > 0 && code < 128) {
                    return static_cast<uint16_t>(code); // Scan Code Set 1 kody 1..88 odpowiadają Linux keycodes
                }
                return 0;
        }
    }
}

uint16_t input_usb_hid_mod_to_linux(uint8_t bit_index) {
    static const uint16_t s_mod_map[8] = {
        INPUT_KEY_LEFTCTRL,   // bit 0
        INPUT_KEY_LEFTSHIFT,  // bit 1
        INPUT_KEY_LEFTALT,    // bit 2
        INPUT_KEY_LEFTMETA,   // bit 3
        INPUT_KEY_RIGHTCTRL,  // bit 4
        INPUT_KEY_RIGHTSHIFT, // bit 5
        INPUT_KEY_RIGHTALT,   // bit 6 (AltGr)
        INPUT_KEY_RIGHTMETA   // bit 7
    };
    if (bit_index < 8) return s_mod_map[bit_index];
    return 0;
}

uint16_t input_usb_hid_usage_to_linux(uint8_t usage) {
    if (usage >= 4 && usage <= 29) {
        static const uint16_t az[26] = {
            INPUT_KEY_A, INPUT_KEY_B, INPUT_KEY_C, INPUT_KEY_D, INPUT_KEY_E,
            INPUT_KEY_F, INPUT_KEY_G, INPUT_KEY_H, INPUT_KEY_I, INPUT_KEY_J,
            INPUT_KEY_K, INPUT_KEY_L, INPUT_KEY_M, INPUT_KEY_N, INPUT_KEY_O,
            INPUT_KEY_P, INPUT_KEY_Q, INPUT_KEY_R, INPUT_KEY_S, INPUT_KEY_T,
            INPUT_KEY_U, INPUT_KEY_V, INPUT_KEY_W, INPUT_KEY_X, INPUT_KEY_Y,
            INPUT_KEY_Z
        };
        return az[usage - 4];
    }
    if (usage >= 30 && usage <= 38) {
        return static_cast<uint16_t>(INPUT_KEY_1 + (usage - 30));
    }
    if (usage == 39) return INPUT_KEY_0;
    if (usage == 40) return INPUT_KEY_ENTER;
    if (usage == 41) return INPUT_KEY_ESC;
    if (usage == 42) return INPUT_KEY_BACKSPACE;
    if (usage == 43) return INPUT_KEY_TAB;
    if (usage == 44) return INPUT_KEY_SPACE;
    if (usage == 45) return INPUT_KEY_MINUS;
    if (usage == 46) return INPUT_KEY_EQUAL;
    if (usage == 47) return INPUT_KEY_LEFTBRACE;
    if (usage == 48) return INPUT_KEY_RIGHTBRACE;
    if (usage == 49) return INPUT_KEY_BACKSLASH;
    if (usage == 51) return INPUT_KEY_SEMICOLON;
    if (usage == 52) return INPUT_KEY_APOSTROPHE;
    if (usage == 53) return INPUT_KEY_GRAVE;
    if (usage == 54) return INPUT_KEY_COMMA;
    if (usage == 55) return INPUT_KEY_DOT;
    if (usage == 56) return INPUT_KEY_SLASH;
    if (usage == 57) return INPUT_KEY_CAPSLOCK;
    if (usage >= 58 && usage <= 67) {
        return static_cast<uint16_t>(INPUT_KEY_F1 + (usage - 58));
    }
    if (usage == 68) return INPUT_KEY_F11;
    if (usage == 69) return INPUT_KEY_F12;
    if (usage == 73) return INPUT_KEY_INSERT;
    if (usage == 74) return INPUT_KEY_HOME;
    if (usage == 75) return INPUT_KEY_PAGEUP;
    if (usage == 76) return INPUT_KEY_DELETE;
    if (usage == 77) return INPUT_KEY_END;
    if (usage == 78) return INPUT_KEY_PAGEDOWN;
    if (usage == 79) return INPUT_KEY_RIGHT;
    if (usage == 80) return INPUT_KEY_LEFT;
    if (usage == 81) return INPUT_KEY_DOWN;
    if (usage == 82) return INPUT_KEY_UP;
    if (usage == 83) return INPUT_KEY_NUMLOCK;
    if (usage == 84) return INPUT_KEY_KPSLASH;
    if (usage == 85) return INPUT_KEY_KPASTERISK;
    if (usage == 86) return INPUT_KEY_KPMINUS;
    if (usage == 87) return INPUT_KEY_KPPLUS;
    if (usage == 88) return INPUT_KEY_KPENTER;
    if (usage >= 89 && usage <= 97) {
        static const uint16_t kp_digits[9] = {
            INPUT_KEY_KP1, INPUT_KEY_KP2, INPUT_KEY_KP3,
            INPUT_KEY_KP4, INPUT_KEY_KP5, INPUT_KEY_KP6,
            INPUT_KEY_KP7, INPUT_KEY_KP8, INPUT_KEY_KP9
        };
        return kp_digits[usage - 89];
    }
    if (usage == 98) return INPUT_KEY_KP0;
    if (usage == 99) return INPUT_KEY_KPDOT;
    return 0;
}

} // extern "C"
