/*
 * Host fixtures: exact VirtIO Input binary event format.
 *
 * 1) Wire-layout checks (type u16@0, code u16@2, value le32@4, 8 bytes).
 * 2) Raw little-endian byte sequences decoded into InputEvent and fed through
 *    the real accumulate/submit path.
 * 3) Supplied small/large values retain their signs in the common dispatcher.
 * These unit checks cannot rule out DMA, accumulation, lifecycle or host bugs.
 * test_virtio_input_forensic.cpp exercises the actual queue consumer instead.
 */
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstddef>

#include "input_device.h"
#include "pointer_coordinates.h"
#include "virtio_input.h"

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++g_fail; } \
    else { ++g_pass; } \
} while (0)

static int g_mx = 640, g_my = 360;
static int g_last_dx = 0, g_pub_count = 0;
static int64_t g_last_dy = 0;
static int g_abs_x = -1, g_abs_y = -1, g_abs_count = 0;

extern "C" void zaktualizuj_mysze_zrodlo(int dx, int dy, uint8_t, const char*) {
    g_last_dx = dx;
    g_last_dy = dy;
    ++g_pub_count;
    g_mx = pointer_accumulate(g_mx, int64_t(dx), 1280);
    g_my = pointer_accumulate(g_my, -int64_t(dy), 720);
}
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x, int y, uint8_t, const char*) {
    g_abs_x = x;
    g_abs_y = y;
    ++g_abs_count;
}
extern "C" void zaktualizuj_mysze_wzgledne_zrodlo(int32_t dx,int32_t dy,uint8_t,const char*) {
    g_last_dx=dx;g_last_dy=-int64_t(dy);++g_pub_count;
    g_mx=pointer_accumulate(g_mx,int64_t(dx),1280);g_my=pointer_accumulate(g_my,int64_t(dy),720);
}
extern "C" void zaktualizuj_kolo_myszy(int, int, uint8_t) {}
int grafika_pobierz_szerokosc(void) { return 1280; }
int grafika_pobierz_wysokosc(void) { return 720; }
extern "C" int grafika_pobierz_mysz_x() { return g_mx; }
extern "C" int grafika_pobierz_mysz_y() { return g_my; }
extern "C" void klawiatura_obsluz_klawisz(uint16_t, uint32_t) {}
void klog_serial(const char*) {}
void kursor_diag_input() {}

/* Exact QEMU virtio_input_event little-endian wire image. */
static void write_le_event(uint8_t* p, uint16_t type, uint16_t code, int32_t value) {
    p[0] = uint8_t(type & 0xFF);
    p[1] = uint8_t((type >> 8) & 0xFF);
    p[2] = uint8_t(code & 0xFF);
    p[3] = uint8_t((code >> 8) & 0xFF);
    const uint32_t u = static_cast<uint32_t>(value);
    p[4] = uint8_t(u & 0xFF);
    p[5] = uint8_t((u >> 8) & 0xFF);
    p[6] = uint8_t((u >> 16) & 0xFF);
    p[7] = uint8_t((u >> 24) & 0xFF);
}

static InputEvent decode_le(const uint8_t p[8]) {
    return {uint16_t(uint16_t(p[0]) | uint16_t(p[1])<<8),
        uint16_t(uint16_t(p[2]) | uint16_t(p[3])<<8),
        uint32_t(p[4]) | uint32_t(p[5])<<8 | uint32_t(p[6])<<16 | uint32_t(p[7])<<24};
}

static void reset_capture() {
    g_last_dx = g_last_dy = 0;
    g_pub_count = 0;
    g_abs_x = g_abs_y = -1;
    g_abs_count = 0;
    g_mx = 640;
    g_my = 360;
}

static InputDevice g_dev_pool[4];
static int g_dev_used = 0;

static InputDevice& make_rel_mouse() {
    InputDevice& d = g_dev_pool[g_dev_used++ % 4];
    d = InputDevice{};
    std::snprintf(d.name, sizeof(d.name), "QEMU Virtio Mouse");
    d.kinds = INPUT_DEVICE_KIND_MOUSE;
    d.bus_type = INPUT_BUS_VIRTIO;
    d.device_id = 2048;
    d.pointer_mode = INPUT_POINTER_RELATIVE;
    return d;
}

static void arm(InputDevice& d) {
    input_reset_all();
    CHECK(input_register_device(&d), "mouse registration succeeds");
    d.pointer_state = INPUT_POINTER_PROVEN_ACTIVE;
    input_set_pointer_ready(true);
}

static void feed_bytes(InputDevice& d, const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i + 8 <= n; i += 8) {
        const InputEvent ev = decode_le(bytes + i);
        input_dispatch_event(&d, &ev);
    }
}

static void feed_triplet(InputDevice& d, uint16_t t1, uint16_t c1, int32_t v1,
                         uint16_t t2, uint16_t c2, int32_t v2,
                         uint16_t t3, uint16_t c3, int32_t v3) {
    uint8_t raw[24];
    write_le_event(raw + 0, t1, c1, v1);
    write_le_event(raw + 8, t2, c2, v2);
    write_le_event(raw + 16, t3, c3, v3);
    feed_bytes(d, raw, 24);
}

static void test_wire_layout() {
    CHECK(sizeof(VirtioInputEvent) == 8, "VirtioInputEvent 8B");
    CHECK(offsetof(VirtioInputEvent, type) == 0, "type@0");
    CHECK(offsetof(VirtioInputEvent, code) == 2, "code@2");
    CHECK(offsetof(VirtioInputEvent, value) == 4, "value@4");
    CHECK(sizeof(InputEvent) == 8, "InputEvent 8B");
    CHECK(offsetof(InputEvent, type) == 0, "InputEvent type@0");
    CHECK(offsetof(InputEvent, code) == 2, "InputEvent code@2");
    CHECK(offsetof(InputEvent, value) == 4, "InputEvent value@4");

    uint8_t raw[8];
    write_le_event(raw, 2, 0, 1);
    CHECK(raw[0] == 2 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0, "LE type=2,code=0");
    CHECK(raw[4] == 1 && raw[5] == 0 && raw[6] == 0 && raw[7] == 0, "LE value=1");
    write_le_event(raw, 2, 0, -1);
    CHECK(raw[4] == 0xFF && raw[5] == 0xFF && raw[6] == 0xFF && raw[7] == 0xFF, "LE value=-1");
    write_le_event(raw, 2, 1, INT32_MIN);
    CHECK(raw[4] == 0x00 && raw[5] == 0x00 && raw[6] == 0x00 && raw[7] == 0x80, "LE INT32_MIN");
    write_le_event(raw, 2, 1, INT32_MAX);
    CHECK(raw[4] == 0xFF && raw[5] == 0xFF && raw[6] == 0xFF && raw[7] == 0x7F, "LE INT32_MAX");

    const InputEvent e = decode_le(raw);
    CHECK(e.type == 2 && e.code == 1 && e.value == 0x7FFFFFFFu, "decode LE INT32_MAX");
    CHECK(input_signed_value(e.value) == INT32_MAX, "signed INT32_MAX");
    CHECK(input_signed_value(1) == 1, "signed 1");
    CHECK(input_signed_value(0xFFFFFFFFu) == -1, "signed -1");
    CHECK(input_signed_value(0x80000000u) == INT32_MIN, "signed INT32_MIN");
}

static void test_rel_x3_y_neg2() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, 3,
                     INPUT_EV_REL, INPUT_REL_Y, -2,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    /* publish_report calls zaktualizuj_mysze_zrodlo(r.dx, -r.dy).
       r.dy = -2  =>  adapter dy argument = 2. */
    CHECK(g_last_dx == 3, "dx 3");
    CHECK(g_last_dy == 2, "adapter sees -(-2)");
    CHECK(g_pub_count == 1, "one SYN one publish");
}

static void test_negative_xy() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, -5,
                     INPUT_EV_REL, INPUT_REL_Y, -7,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_last_dx == -5, "dx -5");
    CHECK(g_last_dy == 7, "adapter -(-7)");
}

static void test_multi_rel_before_syn() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    uint8_t raw[40];
    write_le_event(raw + 0, INPUT_EV_REL, INPUT_REL_X, 3);
    write_le_event(raw + 8, INPUT_EV_REL, INPUT_REL_X, 4);
    write_le_event(raw + 16, INPUT_EV_REL, INPUT_REL_Y, -2);
    write_le_event(raw + 24, INPUT_EV_REL, INPUT_REL_Y, -5);
    write_le_event(raw + 32, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    feed_bytes(d, raw, 40);
    CHECK(g_last_dx == 7, "3+4=7");
    CHECK(g_last_dy == 7, "adapter -(-2-5)");
    CHECK(g_pub_count == 1, "single SYN single publish");
}

static void test_report_isolation() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, 10,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0,
                     INPUT_EV_REL, INPUT_REL_X, 1);
    /* second SYN */
    uint8_t syn[8];
    write_le_event(syn, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    feed_bytes(d, syn, 8);
    CHECK(g_last_dx == 1, "second report is only 1, not 11");
}

static void test_extremes() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, INT32_MAX,
                     INPUT_EV_REL, INPUT_REL_Y, INT32_MIN,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_last_dx == INT32_MAX, "INT32_MAX kept");
    CHECK(g_last_dy == 2147483648LL, "INT32_MIN Y widens before legacy adaptation");
}

static void test_unknown_and_zero() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, 0x0F, 99,
                     INPUT_EV_REL, INPUT_REL_X, 0,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_pub_count == 0, "zero motion does not publish");
}

static void test_no_stale_inherit() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    // Common-layer clear only. VirtIO's transport must additionally ignore
    // the remainder through SYN_REPORT; the complete queue fixture checks it.
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, 50,
                     INPUT_EV_SYN, INPUT_SYN_DROPPED, 0,
                     INPUT_EV_REL, INPUT_REL_X, 1);
    uint8_t syn[8];
    write_le_event(syn, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    feed_bytes(d, syn, 8);
    CHECK(g_last_dx == 1, "SYN_DROPPED discarded prior dx");
}

static void test_small_and_injected_large_reports() {
    InputDevice& d = make_rel_mouse();
    arm(d);
    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, 1,
                     INPUT_EV_REL, INPUT_REL_Y, 1,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_last_dx == 1, "small dx");
    CHECK(g_last_dy == -1, "small adapter dy");
    CHECK(g_last_dx != -27496, "not -27496");
    CHECK(g_last_dy != -44561, "not -44561");

    reset_capture();
    feed_triplet(d, INPUT_EV_REL, INPUT_REL_X, -27496,
                     INPUT_EV_REL, INPUT_REL_Y, 44561,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_last_dx == -27496, "exact bytes => exact dx");
    CHECK(g_last_dy == -44561, "exact bytes => adapter dy");
}

static void test_byte_pattern_of_observed() {
    uint8_t raw[8];
    write_le_event(raw, INPUT_EV_REL, INPUT_REL_X, -27496);
    CHECK(raw[0] == 0x02 && raw[1] == 0x00, "type EV_REL LE");
    CHECK(raw[2] == 0x00 && raw[3] == 0x00, "code REL_X LE");
    /* -27496 = 0xFFFF9498 */
    CHECK(raw[4] == 0x98 && raw[5] == 0x94 && raw[6] == 0xFF && raw[7] == 0xFF,
          "value bytes of -27496");
    const InputEvent ev = decode_le(raw);
    CHECK(input_signed_value(ev.value) == -27496, "decode -27496");
}

static void test_multiple_devices_isolated() {
    input_reset_all();
    InputDevice& a = make_rel_mouse();
    InputDevice& b = make_rel_mouse();
    std::snprintf(b.name, sizeof(b.name), "ps2-mouse");
    b.bus_type = INPUT_BUS_PS2;
    b.device_id = 1;
    input_register_device(&a);
    input_register_device(&b);
    a.pointer_state = INPUT_POINTER_PROVEN_ACTIVE;
    b.pointer_state = INPUT_POINTER_PROVEN_ACTIVE;
    input_set_pointer_ready(true);
    reset_capture();
    feed_triplet(a, INPUT_EV_REL, INPUT_REL_X, 5,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0,
                     INPUT_EV_REL, INPUT_REL_X, 0);
    feed_triplet(b, INPUT_EV_REL, INPUT_REL_X, 41,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0,
                     INPUT_EV_REL, INPUT_REL_X, 0);
    CHECK(g_last_dx == 41, "ps2 value not mixed with virtio");
}

static void test_keyboard_only_rel_does_not_accumulate() {
    input_reset_all();
    InputDevice k{};
    std::snprintf(k.name, sizeof(k.name), "QEMU Virtio Keyboard");
    k.kinds = INPUT_DEVICE_KIND_KEYBOARD;
    k.bus_type = INPUT_BUS_VIRTIO;
    k.device_id = 1792;
    /* Keyboard-only: register must NOT invent a relative pointer mode. */
    CHECK(input_register_device(&k) && k.registered, "keyboard registration succeeds without a pointer mode");
    CHECK(k.pointer_mode == INPUT_POINTER_UNSPECIFIED ||
          !(k.pointer_mode & INPUT_POINTER_RELATIVE),
          "keyboard-only is not a relative pointer");
    input_set_pointer_ready(true);
    reset_capture();
    feed_triplet(k, INPUT_EV_REL, INPUT_REL_X, 5000,
                     INPUT_EV_REL, INPUT_REL_Y, 5000,
                     INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    CHECK(g_pub_count == 0, "keyboard-only REL+SYN does not publish cursor");
    /* Second SYN must not dump a huge accumulated delta. */
    uint8_t syn[8];
    write_le_event(syn, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    feed_bytes(k, syn, 8);
    CHECK(g_pub_count == 0, "still no publication");
}

int main() {
    test_wire_layout();
    test_rel_x3_y_neg2();
    test_negative_xy();
    test_multi_rel_before_syn();
    test_report_isolation();
    test_extremes();
    test_unknown_and_zero();
    test_no_stale_inherit();
    test_small_and_injected_large_reports();
    test_byte_pattern_of_observed();
    test_multiple_devices_isolated();
    test_keyboard_only_rel_does_not_accumulate();
    std::printf("virtio-input-events: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
