#pragma once
#include <stddef.h>
#include <stdint.h>
#include "input_device.h"

constexpr size_t HID_POINTER_DESCRIPTOR_MAX=4096;
constexpr size_t HID_POINTER_REPORT_MAX=512;
constexpr size_t HID_POINTER_FIELDS_MAX=64;
constexpr size_t HID_POINTER_IDS_MAX=16;
struct HidPointerField {
    uint16_t offset{}, page{}, usage{};
    uint8_t size{}, report_id{};
    bool relative{};
    int32_t minimum{}, maximum{};
};
struct HidPointerLayout {
    HidPointerField fields[HID_POINTER_FIELDS_MAX]{};
    uint16_t bits[HID_POINTER_IDS_MAX]{};
    uint8_t ids[HID_POINTER_IDS_MAX]{}, id_count{}, field_count{};
    bool numbered{}, absolute{}, contact{};
    InputAbsInfo x{}, y{};
};
struct UsbHidInterface {
    uint8_t configuration{}, interface_number{}, alternate{}, subclass{}, protocol{}, endpoint{}, interval{};
    uint16_t packet_size{}, report_length{}, hid_bcd{};
    uint8_t transactions{}, max_burst{};
    uint16_t bytes_per_interval{}; bool companion{};
};
enum HidReportRole : uint8_t { HID_ROLE_UNKNOWN=0,HID_ROLE_KEYBOARD=1,HID_ROLE_POINTER=2,HID_ROLE_CONSUMER=4,HID_ROLE_OTHER=8 };
// Usage hints only; report decoding still requires a compiled supported layout.
uint8_t hid_report_roles(const uint8_t*,size_t);
constexpr size_t USB_HID_INTERFACES_MAX=16;
struct UsbHidConfiguration {
    UsbHidInterface interfaces[USB_HID_INTERFACES_MAX]{};
    size_t count{};
    uint8_t configuration{}, declared_interfaces{};
    bool truncated{};
};
bool usb_hid_parse_configuration(const uint8_t*,size_t,UsbHidConfiguration&);
// Pure bounded parsers. No allocation, I/O or device registration.
bool usb_hid_find_interface(const uint8_t*,size_t,UsbHidInterface&);
bool hid_pointer_compile(const uint8_t*,size_t,HidPointerLayout&);
bool hid_pointer_decode(const HidPointerLayout&,const uint8_t*,size_t,InputEvent*,size_t,size_t&);
bool hid_pointer_register(InputDevice&,const HidPointerLayout&,uint32_t);
// Called only by a serialized USB service, after real interrupt transfer success.
bool hid_pointer_publish(InputDevice&,const HidPointerLayout&,const uint8_t*,size_t,uint32_t);
void hid_pointer_remove(InputDevice&);

// Boot protocol is selected explicitly; report protocol continues to use its descriptor.
bool hid_boot_mouse_layout(HidPointerLayout&, bool wheel=false);

bool hid_boot_mouse_has_wheel(const HidPointerLayout&);
