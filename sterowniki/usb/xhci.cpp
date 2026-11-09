#include "diagnostyka_systemu.h"
#include "sterowniki/usb/xhci.h"
#include "sterowniki/usb/xhci_ring.h"
#include "sterowniki/usb/xhci_hw.h"
#include "boot_phase_timing.h"
#include "sterowniki/usb/xhci_physical_diag.h"
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "pamiec.h"
#include "pci.h"
#include "idt.h"
#include "apic.h"
#include "xhci_pointing_diag.h"
#include "mysz_input.h"
#include "input_device.h"
#include "sterowniki/usb/hid_pointer.h"
#include "sterowniki/usb/ohci.h"
#include "sterowniki/usb/usb_hid.h"

// Construct interface metadata in DMA-backed kernel storage without a heap
// allocation or C++ runtime dependency, as in the graphics driver.
#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <new>
#else
inline void* operator new[](unsigned long,void* p) { return p; }
#endif

#ifndef BURSZTYN_DEBUG_GUI_PERF
#define BURSZTYN_DEBUG_GUI_PERF 0
#endif

#ifndef BURSZTYN_DEBUG_XHCI
#define BURSZTYN_DEBUG_XHCI 0
#endif

void wypisz_log(const char* tekst);

namespace {
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0x2000000000);
constexpr uint64_t PAGE = 4096;
constexpr uint64_t MAX_MMIO_SIZE = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t MAX_PHYSICAL_ADDRESS = UINT64_C(0x000FFFFFFFFFFFFF);
constexpr uint64_t WAIT_NS = UINT64_C(1000000000);
constexpr uint32_t MAX_EXT_CAPS = 256;
constexpr uint32_t MAX_SCRATCHPADS = 1023;
constexpr uint16_t COMMAND_TRBS = 256;
constexpr uint16_t EVENT_TRBS = 256;
constexpr uint16_t EP0_TRBS = 64;
constexpr uint16_t HID_TRBS = 64;
constexpr uint32_t USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI = 4096U;
constexpr uint32_t CAP_SUPPORTED_PROTOCOL = 2U;
constexpr uint32_t MAX_PROTOCOLS = 16U, MAX_PSI = 16U;

constexpr uint32_t USBCMD = 0x00, USBSTS = 0x04, PAGESIZE = 0x08;
constexpr uint32_t CRCR = 0x18, DCBAAP = 0x30, CONFIG = 0x38, PORTSC_BASE = 0x400;
constexpr uint32_t CMD_RS = 1U, CMD_HCRST = 1U << 1;
constexpr uint32_t STS_HCH = 1U, STS_HSE = 1U << 2, STS_CNR = 1U << 11, STS_HCE = 1U << 12;
constexpr uint32_t PORT_CCS = 1U, PORT_SPEED_SHIFT = 10, PORT_SPEED_MASK = 0xFU;
constexpr uint32_t PORT_PED = 1U << 1, PORT_PR = 1U << 4, PORT_WPR = 1U << 31;
constexpr uint32_t PORT_PLS_SHIFT = 5, PORT_PLS_MASK = 0xFU;
constexpr uint32_t PORT_RW1C = (1U<<17)|(1U<<18)|(1U<<19)|(1U<<20)|
                              (1U<<21)|(1U<<22)|(1U<<23);
constexpr uint32_t PORT_PRC = 1U << 21;
constexpr uint32_t CAP_LEGACY_SUPPORT = 1U;

enum UsbSpeed { USB_LOW_SPEED, USB_FULL_SPEED, USB_HIGH_SPEED,
    USB_SUPER_SPEED, USB_SUPER_SPEED_PLUS, USB_SPEED_UNKNOWN };
struct SpeedInfo { UsbSpeed logical; uint64_t bitrate; uint8_t raw; };
struct Psi { uint8_t id, exponent, type; uint16_t mantissa; };
struct Protocol { uint8_t major, minor, port_offset, port_count, psi_count,
    slot_type; uint32_t name; Psi psi[MAX_PSI]; bool usb2, usb3; };
enum class PortState : uint8_t { Empty, Queued, Enumerating, Active, Failed, Retiring };
struct PortInfo { Protocol* protocol{};bool connection_seen{};
    PortState state=PortState::Empty;uint8_t slot=0;bool reconnect=false,scan_pending=false;uint32_t generation=0; };

struct CommandResult { uint8_t code, slot; bool valid; };
struct TransferResult { uint8_t code, slot, endpoint; uint32_t residual;
    uint64_t pointer; bool valid; };
enum UsbDeviceType : uint8_t { USB_DEVICE_UNKNOWN, USB_HID_BOOT_KEYBOARD,
    USB_HID_BOOT_MOUSE, USB_HID_POINTER };
// The first interface is the base subobject for source compatibility with the
// existing single-interface host fixtures; every interface owns its DMA/state.
struct XhciHid {
    UsbHidInterface descriptor{};uint8_t report_roles{};
    UsbDeviceType type{USB_DEVICE_UNKNOWN};
    XhciProducerRing hid_ring{}; DmaBuffer hid_raport{}; uint8_t hid_dci{};
    uint8_t hid_interfejs{}, hid_endpoint{}; uint16_t hid_mps{}; uint8_t hid_interwal{};
    uint8_t konfiguracja{}; bool hid_gotowy{}, hid_transfer_oczekiwany{},configuration_failed{};
    bool pierwszy_raport_zalogowany{},timing_active{};
    TransferResult hid_result{};
    size_t hid_dma_length{};
    uint8_t mouse_buttons{};
    HidPointerLayout pointer_layout{};
    InputDevice pointer_device{};
    bool boot_mouse{}, boot_wheel{};
    unsigned diagnostic_reports{}, diagnostic_transfers{};
    uint64_t hid_transfer_pointer{};
    uint8_t poprzedni_raport[8]{};
    InputDevice keyboard_device{};
};
struct XhciDevice : XhciHid {
    uint32_t diag_id{};
    uint8_t slot_id{}, root_port{}, speed_id{}; UsbSpeed logical_speed{};
    uint8_t device_address{},device_class{}; uint16_t vendor_id{}, product_id{};
    Protocol* protocol{}; DmaBuffer output_context{}, input_context{};
    XhciProducerRing ep0{}; uint16_t ep0_mps{}; bool enabled{}, addressed{};
    bool control_failed{},retiring{};unsigned diagnostic_controls{};
    XhciHid* extra_hid{};DmaBuffer interface_storage{};
    uint8_t hid_count{},hid_service_interface{},diag_interfaces{},active_configuration{};
};
XhciHid& hid_at(XhciDevice& d,size_t i) {
    return i==0?static_cast<XhciHid&>(d):d.extra_hid[i-1];
}

struct PciDevice { uint8_t bus, slot, function; uint16_t vendor, device; };

struct ErstEntry { uint64_t address; uint32_t size; uint32_t reserved; };
static_assert(sizeof(ErstEntry) == 16, "ERST entry");

struct hid_wynik_konfiguracji { uint8_t konfiguracja, interfejs, alternatywa, endpoint;
    uint16_t hid_bcd, raport_dlugosc, maksymalny_rozmiar, calkowita_dlugosc;
    uint8_t interwal, dci, subclass, protocol, burst, transactions; uint16_t bytes_per_interval; bool companion; UsbDeviceType type; bool znaleziono; };
enum class EnumStep : uint8_t {
    Idle, Reset, ResetWait, Enable, EnableWait, Address, AddressWait,
    FirstDescriptor, FirstDescriptorDone, Evaluate, EvaluateWait,
    DeviceDescriptor, DeviceDescriptorDone, ConfigHeader, ConfigHeaderDone,
    ConfigBody, ConfigBodyDone, Configure, ConfigureWait, SetConfig, SetConfigDone,
    Protocol, ProtocolDone, ReportDescriptor, ReportDescriptorDone,
    ReportProtocol, ReportProtocolDone, IdleRequest, IdleDone, Activate,
    ControlWait, RecoverReset, RecoverResetWait, RecoverDequeue, RecoverDequeueWait,
    Retire, RetireWait
};
struct Enumeration {
    EnumStep step=EnumStep::Idle,next=EnumStep::Idle;
    uint8_t port=0,slot=0,code=0,command_type=0;uint16_t length=0,mps=0;
    uint32_t actual=0,reset_change=0,generation=0;uint64_t due=0;
    uint32_t diagnostic_status=DIAG_INIT_FAILED;
    bool ok=false; DmaBuffer data{};hid_wynik_konfiguracji hid{};
    UsbHidConfiguration configuration{};uint8_t interface_index{};
};


struct Controller {
    uint32_t diag_id{};bool diag_timeout{};
    Enumeration enumeration{};
    uint8_t index{};
    PciDevice pci{};
    uint64_t bar{};
    uint64_t bar_size{};
    bool bar64{}, ac64{true}, port_power_control{};
    volatile uint8_t* mmio{};
    volatile uint8_t* operational{};
    volatile uint8_t* runtime{};
    volatile uint8_t* doorbells{};
    uint32_t max_slots{}, enabled_slots{}, max_ports{}, max_intrs{}, scratchpads{};
    uint32_t page_size{};
    uint16_t version{};
    uint8_t context_size{};
    DmaBuffer dcbaa{}, scratchpad_array{}, scratchpad_buffers[MAX_SCRATCHPADS]{};
    XhciProducerRing command{};
    DmaBuffer event_segment{}, erst{};
    uint16_t event_index{};
    bool event_cycle{true};
    Protocol protocols[MAX_PROTOCOLS]{}; uint8_t protocol_count{};
    PortInfo ports[BURSZTYN_XHCI_MAX_PORTS+1]{};
    XhciDevice devices[BURSZTYN_XHCI_MAX_SLOTS+1]{};
    uint8_t pending_port{}; bool port_event{};
    uint64_t wanted_command{}; CommandResult command_result{};
    uint64_t wanted_transfer{},wanted_setup{},wanted_data{};
    uint32_t control_residual{};uint8_t control_slot{};
    TransferResult transfer_result{};
    uint64_t usb_service_calls{},usb_events_processed{},mouse_reports_processed{},keyboard_reports_processed{};
    size_t max_events_per_service{};
    int irq_vector{-1};
    uint8_t irq_mode{}; // 1 MSI, 2 MSI-X; no legacy shared INTx routing
    PciMsiCapability msi{};
    PciMsixCapability msix{};
    volatile PciMsixTableEntry* msix_entry{};
    uint16_t saved_command{};
    uint32_t irq_pending{};
    bool attempted{},ready{},servicing{},discovery_pending{};
    uint16_t discovery_port{1};
    uint32_t hid_service_slot{1};bool enumeration_started{};
};
constexpr size_t MAX_CONTROLLERS=8;
Controller controllers[MAX_CONTROLLERS]{};
size_t controller_count{};
bool controllers_scanned{};
#ifdef BURSZTYN_XHCI_HOST_TEST
Controller& xhc=controllers[0];
Enumeration& enumeration=controllers[0].enumeration;
#endif

size_t poll_controller_events(Controller&,size_t);
struct UsbSetupPacket { uint8_t bmRequestType, bRequest; uint16_t wValue,
    wIndex, wLength; } __attribute__((packed));
static_assert(sizeof(UsbSetupPacket)==8,"USB setup packet");
struct UsbDeviceDescriptor { uint8_t bLength,bDescriptorType; uint16_t bcdUSB;
    uint8_t bDeviceClass,bDeviceSubClass,bDeviceProtocol,bMaxPacketSize0;
    uint16_t idVendor,idProduct,bcdDevice; uint8_t iManufacturer,iProduct,
    iSerialNumber,bNumConfigurations; } __attribute__((packed));
static_assert(sizeof(UsbDeviceDescriptor)==18,"USB device descriptor");
struct XhciDevice;
#ifdef BURSZTYN_XHCI_HOST_TEST
bool usb_control_transfer(Controller& xhc,XhciDevice&,const UsbSetupPacket&,DmaBuffer*,uint16_t,uint32_t*,uint8_t* = nullptr);
#endif
uint32_t* context(Controller& xhc,DmaBuffer&,uint8_t);
uint8_t xhci_oblicz_dci(uint8_t,bool);
void zero_dma(DmaBuffer&);

void append(char*& p, char* end, const char* s) { while (*s && p + 1 < end) *p++ = *s++; }
void append_hex(char*& p, char* end, uint64_t v) {
    static const char d[] = "0123456789ABCDEF"; append(p,end,"0x");
    bool seen=false; for (int i=60;i>=0;i-=4) { unsigned n=static_cast<unsigned>((v>>i)&15U); if(n||seen||i==0){seen=true;if(p+1<end)*p++=d[n];} }
}
void append_dec(char*& p, char* end, uint64_t v) {
    char t[24]; size_t n=0; do { t[n++]=static_cast<char>('0'+v%10U); v/=10U; } while(v&&n<sizeof(t));
    while(n&&p+1<end)*p++=t[--n];
}
void log_values(const char* prefix, uint64_t a, const char* middle=nullptr,
                uint64_t b=0, bool hex=false) {
    char s[160]; char* p=s; char* e=s+sizeof(s); append(p,e,prefix);
    if(hex) append_hex(p,e,a); else append_dec(p,e,a);
    if(middle){append(p,e,middle);if(hex)append_hex(p,e,b);else append_dec(p,e,b);} *p='\0'; wypisz_log(s);
}
void diag_xhci(Controller& c,uint32_t status,const char* message);
void diag_usb(const Controller& c,const XhciDevice& d,uint32_t status,const char* message) {
    auto v=diag_device(DIAG_USB,"Urządzenie USB","usb",status,true,message);
    v.parent=c.diag_id;v.transport=DIAG_TRANSPORT_USB;v.controller=c.index;
    v.port=d.root_port;v.slot=d.slot_id;v.vendor=d.vendor_id;v.device=d.product_id;
    v.usb_class=d.device_class;v.configuration=d.active_configuration;v.value=d.konfiguracja;v.hid_interfaces=d.hid_count;v.interfaces=d.diag_interfaces;
    if(status==DIAG_INIT_FAILED||status==DIAG_TIMEOUT)v.code=DIAG_CODE_HID_INIT;
    diag_publish((uint64_t(c.index)<<32)|d.slot_id,v);
}
void diag_hid(const Controller& c,const XhciDevice& d,const XhciHid& hi,uint32_t status,const char* message) {
    auto v=diag_device(DIAG_HID,hi.type==USB_HID_BOOT_KEYBOARD||(hi.report_roles&HID_ROLE_KEYBOARD)||(hi.descriptor.subclass==1&&hi.descriptor.protocol==1)?"HID Keyboard":
        hi.type==USB_HID_POINTER||(hi.report_roles&HID_ROLE_POINTER)||(hi.descriptor.subclass==1&&hi.descriptor.protocol==2)?"HID Pointer":(hi.report_roles&HID_ROLE_CONSUMER)?"HID Consumer Control":"HID — inny interfejs",
        "usbhid",status,true,status==DIAG_OK?"Interfejs HID gotowy; odbiór raportów uruchomiony":
        status==DIAG_INIT_FAILED?"Transmisja HID zatrzymana; sprawdź log USB":
        status==DIAG_UNSUPPORTED||status==DIAG_DETECTED_UNSUPPORTED?"Interfejs HID wykryty; brak obsługi formatu lub konfiguracji":message);
    v.parent=d.diag_id;v.transport=DIAG_TRANSPORT_USB;v.controller=c.index;
    v.port=d.root_port;v.slot=d.slot_id;v.vendor=d.vendor_id;v.device=d.product_id;
    v.usb_class=d.device_class;v.configuration=d.active_configuration;v.value=hi.konfiguracja;
    v.interface_number=hi.descriptor.interface_number;v.alternate=hi.descriptor.alternate;
    v.subclass=hi.descriptor.subclass;v.protocol=hi.descriptor.protocol;
    v.endpoint=hi.descriptor.endpoint;v.dci=hi.hid_dci?hi.hid_dci:uint32_t((hi.descriptor.endpoint&15U)*2U+1U);
    v.packet_size=hi.descriptor.packet_size;v.interval=hi.descriptor.interval; // USB bInterval; encoded xHCI interval remains in serial logs.
    v.role=hi.report_roles|(hi.type==USB_HID_BOOT_KEYBOARD||(hi.descriptor.subclass==1&&hi.descriptor.protocol==1)?uint32_t(HID_ROLE_KEYBOARD):hi.type==USB_HID_POINTER||(hi.descriptor.subclass==1&&hi.descriptor.protocol==2)?uint32_t(HID_ROLE_POINTER):0U);
    if(status==DIAG_UNSUPPORTED||status==DIAG_INIT_FAILED||status==DIAG_TIMEOUT)v.code=DIAG_CODE_HID_INIT;
    diag_publish((uint64_t(c.index)<<32)|(uint32_t(d.slot_id)<<16)|v.interface_number,v);
}
void log_controller(const Controller& c,const char* result){
    char line[256],*p=line,*e=line+sizeof(line);
    append(p,e,"[xHCI controller] index=");append_dec(p,e,c.index);
    append(p,e," BDF=");append_dec(p,e,c.pci.bus);append(p,e,":");append_dec(p,e,c.pci.slot);append(p,e,".");append_dec(p,e,c.pci.function);
    append(p,e," vendor=");append_hex(p,e,c.pci.vendor);append(p,e," device=");append_hex(p,e,c.pci.device);
    append(p,e," BAR=");append_hex(p,e,c.bar);append(p,e," ports=");append_dec(p,e,c.max_ports);
    append(p,e," slots=");append_dec(p,e,c.max_slots);append(p,e," version=");append_hex(p,e,c.version);
    append(p,e," result=");append(p,e,result);*p=0;wypisz_log(line);
}
void log_hid(const Controller& c,const XhciDevice& d,const XhciHid& hi,const char* result,uint32_t status=UINT32_MAX){
    if(status!=UINT32_MAX)diag_hid(c,d,hi,status,result);
    char line[256],*p=line,*e=line+sizeof(line);
    append(p,e,"[USB HID] controller=");append_dec(p,e,c.index);append(p,e," port=");append_dec(p,e,d.root_port);
    append(p,e," slot=");append_dec(p,e,d.slot_id);append(p,e," interface=");append_dec(p,e,hi.hid_interfejs);
    append(p,e," endpoint=");append_hex(p,e,hi.hid_endpoint);append(p,e," DCI=");append_dec(p,e,hi.hid_dci);
    append(p,e," MPS=");append_dec(p,e,hi.hid_mps);append(p,e," interval=");append_dec(p,e,hi.hid_interwal);
    append(p,e," role=");append(p,e,hi.type==USB_HID_BOOT_KEYBOARD?"boot-keyboard":hi.boot_mouse?"boot-mouse":hi.hid_gotowy&&hi.type==USB_HID_POINTER?"report-pointer":(hi.report_roles&HID_ROLE_CONSUMER)?"consumer-control":(hi.report_roles&HID_ROLE_KEYBOARD)?"report-keyboard-unsupported":"other/unknown");
    append(p,e," result=");append(p,e,result);*p=0;wypisz_log(line);
}
void log_usb_configuration(const Controller& c,XhciDevice& d,const UsbHidConfiguration& cfg,const uint8_t* raw,size_t length){
    d.konfiguracja=cfg.configuration;d.diag_interfaces=cfg.declared_interfaces;
    auto v=diag_device(DIAG_USB,"Urządzenie USB","usb",DIAG_INACTIVE,true,"Odczytano konfigurację USB");
    v.parent=c.diag_id;v.transport=DIAG_TRANSPORT_USB;v.controller=c.index;v.port=d.root_port;
    v.slot=d.slot_id;v.vendor=d.vendor_id;v.device=d.product_id;v.usb_class=d.device_class;
    v.configuration=d.active_configuration;v.value=cfg.configuration;v.interfaces=cfg.declared_interfaces;v.hid_interfaces=cfg.count;
    d.diag_id=diag_publish((uint64_t(c.index)<<32)|d.slot_id,v);
    char line[256],*p=line,*e=line+sizeof(line);
    append(p,e,"[USB device] controller=");append_dec(p,e,c.index);append(p,e," port=");append_dec(p,e,d.root_port);
    append(p,e," slot=");append_dec(p,e,d.slot_id);append(p,e," VID=");append_hex(p,e,d.vendor_id);append(p,e," PID=");append_hex(p,e,d.product_id);
    append(p,e," class=");append_dec(p,e,d.device_class);append(p,e," configuration=");append_dec(p,e,cfg.configuration);
    append(p,e," interfaces=");append_dec(p,e,cfg.declared_interfaces);append(p,e," HID interfaces=");append_dec(p,e,cfg.count);
    if(cfg.truncated)append(p,e," capacity exceeded");
    *p=0;wypisz_log(line);
    // The parser validated the complete descriptor chain before this log walk.
    const size_t declared=uint16_t(raw[2])|(uint16_t(raw[3])<<8);
    const size_t total=declared<length?declared:length;
    for(size_t offset=9;offset<total;offset+=raw[offset])if(raw[offset+1]==4){p=line;
        append(p,e,"[USB interface] controller=");append_dec(p,e,c.index);append(p,e," slot=");append_dec(p,e,d.slot_id);
        append(p,e," interface=");append_dec(p,e,raw[offset+2]);append(p,e," alternate=");append_dec(p,e,raw[offset+3]);
        append(p,e," endpoints=");append_dec(p,e,raw[offset+4]);append(p,e," class=");append_dec(p,e,raw[offset+5]);
        append(p,e," subclass=");append_dec(p,e,raw[offset+6]);append(p,e," protocol=");append_dec(p,e,raw[offset+7]);*p=0;wypisz_log(line);}
    for(size_t i=0;i<cfg.count;++i){const auto& u=cfg.interfaces[i];p=line;
        append(p,e,"[USB HID descriptor] controller=");append_dec(p,e,c.index);append(p,e," slot=");append_dec(p,e,d.slot_id);
        append(p,e," interface=");append_dec(p,e,u.interface_number);append(p,e," alternate=");append_dec(p,e,u.alternate);
        append(p,e," subclass=");append_dec(p,e,u.subclass);append(p,e," protocol=");append_dec(p,e,u.protocol);
        append(p,e," endpoint=");append_hex(p,e,u.endpoint);append(p,e," MPS=");append_dec(p,e,u.packet_size);
        append(p,e," bInterval=");append_dec(p,e,u.interval);append(p,e," report-length=");append_dec(p,e,u.report_length);*p=0;wypisz_log(line);}
}
[[maybe_unused]] const char* speed_name(UsbSpeed s) { switch(s){case USB_LOW_SPEED:return "Low";
case USB_FULL_SPEED:return "Full";case USB_HIGH_SPEED:return "High";
case USB_SUPER_SPEED:return "Super";case USB_SUPER_SPEED_PLUS:return "SuperPlus";
default:return "Unknown";} }
[[maybe_unused]] void log_port_value(uint32_t port,const char* label,uint64_t value) { char s[128],*p=s,*e=s+sizeof(s);
 append(p,e,"[xHCI] Port ");append_dec(p,e,port);append(p,e,label);append_dec(p,e,value);*p=0;wypisz_log(s); }
[[maybe_unused]] void log_port_text(uint32_t port,const char* label,const char* value) { char s[128],*p=s,*e=s+sizeof(s);
 append(p,e,"[xHCI] Port ");append_dec(p,e,port);append(p,e,label);append(p,e,value);*p=0;wypisz_log(s); }
inline uint16_t le16(const void* p){const uint8_t* b=static_cast<const uint8_t*>(p);return static_cast<uint16_t>(b[0]|(b[1]<<8));}

#ifdef BURSZTYN_XHCI_HOST_TEST
extern uint32_t xhci_host_read(const volatile uint8_t*,uint32_t);
extern void xhci_host_write(volatile uint8_t*,uint32_t,uint32_t);
#endif
inline uint32_t read32(const volatile uint8_t* base, uint32_t off) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    #ifdef BURSZTYN_XHCI_HOST_TEST
    uint32_t v=xhci_host_read(base,off);
#else
    uint32_t v=*reinterpret_cast<const volatile uint32_t*>(base+off);
#endif
    __atomic_thread_fence(__ATOMIC_ACQUIRE); return v;
}
void diag_xhci(Controller& c,uint32_t status,const char* message) {
    auto d=diag_device(DIAG_XHCI,"Kontroler xHCI","xhci",status,true,message);
    d.parent=diag_pci_parent(c.pci.bus,c.pci.slot,c.pci.function);
    d.transport=DIAG_TRANSPORT_PCI;d.vendor=c.pci.vendor;d.device=c.pci.device;
    d.bus=c.pci.bus;d.pci_device=c.pci.slot;d.function=c.pci.function;d.pci_valid=1;
    d.bar=c.bar;d.controller=c.index;d.ports=c.max_ports;d.max_slots=c.max_slots;d.xhci_version=c.version;
    if(c.operational)d.hardware_status=read32(c.operational,USBSTS);
    if(status==DIAG_INIT_FAILED||status==DIAG_TIMEOUT||status==DIAG_CRITICAL)d.code=DIAG_CODE_XHCI_INIT;
    c.diag_id=diag_publish(c.index,d);
}
inline void write32(volatile uint8_t* base,uint32_t off,uint32_t v) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    #ifdef BURSZTYN_XHCI_HOST_TEST
    xhci_host_write(base,off,v);
#else
    *reinterpret_cast<volatile uint32_t*>(base+off)=v;
#endif
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
inline uint64_t read64(const volatile uint8_t* base,uint32_t off) {
    const uint32_t lo=read32(base,off), hi=read32(base,off+4U); return (static_cast<uint64_t>(hi)<<32)|lo;
}
inline void write64(volatile uint8_t* base,uint32_t off,uint64_t v) {
    write32(base,off,static_cast<uint32_t>(v)); write32(base,off+4U,static_cast<uint32_t>(v>>32));
}

[[maybe_unused]] const char* completion_name(uint8_t code) {
    switch(code){case 1:return "Success";case 2:return "DataBufferError";case 3:return "Babble";
    case 4:return "USBTransactionError";case 5:return "TRBError";case 6:return "Stall";
    case 13:return "ShortPacket";case 19:return "ContextStateError";default:return "OtherOrTimeout";}
}
void physical_completion(const char* stage,uint8_t code) {
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    if(!xhci_physical_log_allowed())return;
    char line[192],*p=line,*e=line+sizeof(line);append(p,e,"[XHCI-PHYSICAL] ");append(p,e,stage);
    append(p,e," completion=");append_dec(p,e,code);append(p,e," ");append(p,e,completion_name(code));*p=0;wypisz_log(line);
#else
    (void)stage;(void)code;
#endif
}
bool addressable(Controller& xhc,const DmaBuffer& b) {
    const uint64_t limit=xhc.ac64?MAX_PHYSICAL_ADDRESS:UINT32_MAX;
    return b.virtual_address && b.size && b.physical_address && b.physical_address<=limit && b.size-1<=limit-b.physical_address;
}
bool allocate_dma(Controller& xhc,size_t size,size_t alignment,DmaBuffer* b) {
    if(!dma_allocate(size,alignment,b))return false;
    if(addressable(xhc,*b) && !(b->physical_address&(alignment-1)))return true;
    dma_release(b);wypisz_log("[xHCI-ERR] DMA outside controller address width/alignment.");return false;
}
bool create_ring(Controller& xhc,XhciProducerRing* ring,uint16_t count) {
    if(!xhci_command_ring_create(ring,count))return false;
    if(addressable(xhc,ring->dma)&&!(ring->dma.physical_address&63U)&&
       (ring->dma.physical_address&65535U)+ring->dma.size<=65536U)return true;
    xhci_command_ring_destroy(ring);wypisz_log("[xHCI-ERR] Ring outside controller address width.");return false;
}

bool wait_bits(const volatile uint8_t* base,uint32_t off,uint32_t mask,uint32_t expected) {
    struct TimeoutLog {const volatile uint8_t* base;uint32_t offset,mask,expected;bool ok{};~TimeoutLog(){if(!ok){
        log_values("[xHCI timeout] register=",offset," mask=",mask,true);
        log_values("[xHCI timeout] expected=",expected,nullptr,0,true);
        for(auto& c:controllers)if(c.operational==base||c.mmio==base)c.diag_timeout=true;
    }}} result{base,off,mask,expected};
    if (!hpet_dostepny()) return false;
    const uint64_t start=czas_monotoniczny_ns();
    if (start > UINT64_MAX-WAIT_NS) return false;
    const uint64_t deadline=start+WAIT_NS;
    for (unsigned spins=0;spins<10000000;++spins) {
        if ((read32(base,off)&mask)==expected) {result.ok=true;return true;}
        if (czas_monotoniczny_ns()>=deadline) return false;
        asm volatile("pause" ::: "memory");
    }
    return false;
}

bool parse_protocol(Controller& c,uint32_t off,uint32_t next) {
    if(c.protocol_count>=MAX_PROTOCOLS||off>c.bar_size-16U)return false;
    Protocol& p=c.protocols[c.protocol_count];const uint32_t d0=read32(c.mmio,off);
    const uint32_t d1=read32(c.mmio,off+4U),d2=read32(c.mmio,off+8U),
                   d3=read32(c.mmio,off+12U);
    p.minor=static_cast<uint8_t>(d0>>16);p.major=static_cast<uint8_t>(d0>>24);p.name=d1;
    p.port_offset=static_cast<uint8_t>(d2);p.port_count=static_cast<uint8_t>(d2>>8);
    p.psi_count=static_cast<uint8_t>((d2>>28)&15U);p.slot_type=static_cast<uint8_t>(d3&31U);
    p.usb2=p.major==2U;p.usb3=p.major==3U;
    if(p.port_offset==0||p.port_count==0||p.port_offset>c.max_ports||
       p.port_count>c.max_ports-p.port_offset+1U||p.psi_count>MAX_PSI)return false;
    const uint32_t cap_end=next?off+next*4U:static_cast<uint32_t>(c.bar_size);
    if(uint64_t(off)+16U+4U*p.psi_count>c.bar_size||cap_end<off+16U||static_cast<uint64_t>(off)+16U+4U*p.psi_count>cap_end)return false;
    for(uint8_t i=0;i<p.psi_count;++i){const uint32_t v=read32(c.mmio,off+16U+4U*i);
      p.psi[i]={static_cast<uint8_t>(v&15U),static_cast<uint8_t>((v>>4)&3U),
                static_cast<uint8_t>((v>>6)&3U),static_cast<uint16_t>(v>>16)};
      char z[128],*a=z,*e=z+sizeof(z);append(a,e,"[xHCI] PSI id=");append_dec(a,e,p.psi[i].id);
      append(a,e," mantissa=");append_dec(a,e,p.psi[i].mantissa);append(a,e," exponent=");append_dec(a,e,p.psi[i].exponent);
      append(a,e," type=");append_dec(a,e,p.psi[i].type);*a=0;wypisz_log(z);}
    for(uint32_t n=p.port_offset;n<static_cast<uint32_t>(p.port_offset)+p.port_count;++n){
      if(c.ports[n].protocol)return false;
      c.ports[n].protocol=&p;}
    char name[5]={static_cast<char>(d1),static_cast<char>(d1>>8),static_cast<char>(d1>>16),static_cast<char>(d1>>24),0};
    char s[192],*q=s,*e=s+sizeof(s);append(q,e,"[xHCI] Protocol ");append(q,e,name);append(q,e," rev=");append_dec(q,e,p.major);append(q,e,".");append_dec(q,e,p.minor);
    append(q,e," ports=");append_dec(q,e,p.port_offset);append(q,e,"..");append_dec(q,e,p.port_offset+p.port_count-1U);append(q,e," PSIC=");append_dec(q,e,p.psi_count);append(q,e," slot_type=");append_dec(q,e,p.slot_type);*q=0;wypisz_log(s);
    if(p.psi_count==0&&p.usb2)wypisz_log("[xHCI] Speed IDs USB2 implied: 1=Full 2=Low 3=High.");
    if(p.psi_count==0&&p.usb3)wypisz_log("[xHCI] Speed IDs USB3 implied: 4=Super.");
    ++c.protocol_count;return true;
}

bool probe_bar(Controller& c,uint8_t bir,uint64_t& address,uint64_t& size) {
    if(bir>=6)return false;
    // A BIR must name the low DWORD of a memory BAR, never a 64-bit upper half.
    for(unsigned n=0;n<bir;++n) {
        const uint32_t v=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,uint8_t(0x10+n*4));
        if(!(v&1U) && ((v>>1)&3U)==2U){if(n+1==bir)return false;++n;}
    }
    const uint8_t off=uint8_t(0x10+bir*4);
    const uint32_t low=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,off);
    const unsigned type=(low>>1)&3U;if((low&1U)||(type!=0&&type!=2)||(type==2&&bir==5))return false;
    const bool wide=type==2;
    const uint32_t high=wide?pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,off+4):0;
    address=(uint64_t(high)<<32)|(low&~15U);
    if(!address || address>MAX_PHYSICAL_ADDRESS)return false;
    const uint16_t cmd=uint16_t(pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,4));
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,4,cmd&~PCI_COMMAND_MEMORY_SPACE);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,off,UINT32_MAX);
    if(wide)pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,off+4,UINT32_MAX);
    const uint32_t masklow=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,off);
    const uint32_t maskhigh=wide?pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,off+4):UINT32_MAX;
    if(wide)pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,off+4,high);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,off,low);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,4,cmd); // status RW1C stays zero
    const uint64_t mask=(uint64_t(maskhigh)<<32)|(masklow&~15U);size=~mask+1;
    return size>=PAGE && size<=MAX_MMIO_SIZE && !(size&(size-1)) && !(address&(size-1)) &&
        size-1<=MAX_PHYSICAL_ADDRESS-address;
}
bool get_bar(Controller& c) {
    // Firmware DMA must not run while BARs are probed or OS structures are built.
    const uint16_t command=uint16_t(pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,4));
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,4,command&~PCI_COMMAND_BUS_MASTER);
    c.bar64=((pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,0x10)>>1)&3U)==2;
    return probe_bar(c,0,c.bar,c.bar_size);
}

bool enable_pci(Controller& c,bool bus_master=true) {
    uint32_t v=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    const uint16_t required=PCI_COMMAND_MEMORY_SPACE|(bus_master?PCI_COMMAND_BUS_MASTER:0);
    v|=required;if(!bus_master)v&=~PCI_COMMAND_BUS_MASTER;
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS,v&0xFFFFU);
    const uint16_t check=pci_odczytaj_word(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    return (check&required)==required && (bus_master || !(check&PCI_COMMAND_BUS_MASTER));
}

bool map_mmio(Controller& c) {
#ifdef BURSZTYN_XHCI_HOST_TEST
    extern volatile uint8_t* xhci_host_map_bar(uint8_t,uint64_t,uint64_t);
    c.mmio=xhci_host_map_bar(0,c.bar,c.bar_size);return c.mmio!=nullptr;
#else
    const uint64_t first=c.bar&~(PAGE-1U), delta=c.bar-first;
    if(delta>UINT64_MAX-c.bar_size) return false;
    const uint64_t bytes=delta+c.bar_size;
    if(bytes>UINT64_MAX-(PAGE-1U))return false;
    const uint64_t pages=(bytes+PAGE-1U)/PAGE;
    for(uint64_t i=0;i<pages;++i){
        const uint64_t va=MMIO_VIRTUAL_BASE+uint64_t(c.index)*8*MAX_MMIO_SIZE+i*PAGE;
        if(!ZmapujStroneSprawdzona(reinterpret_cast<void*>(va),reinterpret_cast<void*>(first+i*PAGE),VMM_FLAGA_PRESENT|VMM_FLAGA_ZAPIS|VMM_FLAGA_PWT|VMM_FLAGA_PCD)){wypisz_log("[xHCI-ERR] BAR mapping failed");return false;}
    }
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax","memory");
    c.mmio=reinterpret_cast<volatile uint8_t*>(MMIO_VIRTUAL_BASE+uint64_t(c.index)*8*MAX_MMIO_SIZE+delta); return true;
#endif
}

bool ext_capabilities(Controller& c,uint32_t hcc) {
    uint32_t off=((hcc>>16)&0xFFFFU)*4U; if(off==0){wypisz_log("[xHCI] Firmware handoff: not required");return true;}
    bool legacy=false;
    for(uint32_t count=0;count<MAX_EXT_CAPS;++count) {
        if((off&3U)!=0||off>c.bar_size-4U) return false;
        const uint32_t v=read32(c.mmio,off), id=v&0xFFU, next=(v>>8)&0xFFU;
        if(id==CAP_LEGACY_SUPPORT) {
            if(off>c.bar_size-8U || (next && next<2))return false;
            BootTiming::Scope timing(BootTiming::Legacy);
            legacy=true;const uint32_t owned=read32(c.mmio,off),ctl=read32(c.mmio,off+4U);
            const int64_t initial[]={off,owned,ctl};
            xhci_physical_diag("[XHCI-PHYSICAL legacy initial] offset/USBLEGSUP/USBLEGCTLSTS",initial,3);
            write32(c.mmio,off,xhci_legacy_os_owned(owned));
            if(!wait_bits(c.mmio,off,1U<<16,0)) {
                c.diag_timeout=true;physical_completion("BIOS ownership timeout",0);return false;
            }
            write32(c.mmio,off+4U,xhci_legacy_disable_smi(ctl));
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
            const int64_t final[]={read32(c.mmio,off),read32(c.mmio,off+4U)};
            xhci_physical_diag("[XHCI-PHYSICAL legacy final] USBLEGSUP/USBLEGCTLSTS",final,2);
#endif
        }
        if(id==CAP_SUPPORTED_PROTOCOL&&!parse_protocol(c,off,next))return false;
        if(next==0)break;
        const uint32_t step=next*4U; if(step==0||off>UINT32_MAX-step)return false;
        const uint32_t newer=off+step; if(newer==off)return false; off=newer;
        if(count+1U==MAX_EXT_CAPS)return false;
    }
    const int64_t legacy_present[]={legacy};xhci_physical_diag("[XHCI-PHYSICAL legacy present]",legacy_present,1);
    wypisz_log(legacy?"[xHCI] Firmware handoff: OK":"[xHCI] Firmware handoff: not required"); return true;
}

void release_structures(Controller& c) {
    dma_release(&c.erst);
    dma_release(&c.event_segment);
    xhci_command_ring_destroy(&c.command);
    for (uint32_t i=0;i<c.scratchpads;++i) dma_release(&c.scratchpad_buffers[i]);
    dma_release(&c.scratchpad_array);
    dma_release(&c.dcbaa);
}

bool allocate_structures(Controller& c) {
    {BootTiming::Scope timing(BootTiming::Dcbaa);
    if(!allocate_dma(c,(static_cast<size_t>(c.enabled_slots)+1U)*sizeof(uint64_t),64U,&c.dcbaa))return false;}
    uint64_t* dcbaa=static_cast<uint64_t*>(c.dcbaa.virtual_address);
    if(c.scratchpads!=0) {
        BootTiming::Scope timing(BootTiming::Scratchpads);
        if(!allocate_dma(c,static_cast<size_t>(c.scratchpads)*sizeof(uint64_t),64U,&c.scratchpad_array)){release_structures(c);return false;}
        uint64_t* array=static_cast<uint64_t*>(c.scratchpad_array.virtual_address);
        for(uint32_t i=0;i<c.scratchpads;++i){if(!allocate_dma(c,c.page_size,c.page_size,&c.scratchpad_buffers[i])){release_structures(c);return false;}array[i]=c.scratchpad_buffers[i].physical_address;}
        dcbaa[0]=c.scratchpad_array.physical_address;
    }
    {BootTiming::Scope timing(BootTiming::CommandRing);
    if(!create_ring(c,&c.command,COMMAND_TRBS)){release_structures(c);return false;}}
    BootTiming::Scope timing(BootTiming::EventRing);
    if(!allocate_dma(c,static_cast<size_t>(EVENT_TRBS)*sizeof(XhciTrb),64U,&c.event_segment)){release_structures(c);return false;}
    if(!allocate_dma(c,sizeof(ErstEntry),64U,&c.erst)){release_structures(c);return false;}
    ErstEntry* e=static_cast<ErstEntry*>(c.erst.virtual_address);e->address=c.event_segment.physical_address;e->size=EVENT_TRBS;e->reserved=0;
    return true;
}

void configure(Controller& c) {
    write64(c.operational,DCBAAP,c.dcbaa.physical_address);
    write64(c.operational,CRCR,c.command.dma.physical_address|1U);
    volatile uint8_t* intr=c.runtime+0x20U;
    write32(intr,0x00,0); /* IP jest RW1C; zero zachowuje IP i wylacza IE. */
    write32(intr,0x04,0);
    write32(intr,0x08,1);
    write64(intr,0x10,c.erst.physical_address);
    write64(intr,0x18,c.event_segment.physical_address);
    write32(c.operational,CONFIG,c.enabled_slots);
}

// The IRQ never takes InputDevice/PCI locks or consumes rings. Deferred service
// owns ring state. Mask only this interrupter until its events have been drained.
void xhci_irq(void* argument) {
    auto& c=*static_cast<Controller*>(argument);
    pointing_count(pointing_counters.xhci_irq);
    write32(c.runtime+0x20U,0,1U); // acknowledge IP, clear IE
    write32(c.operational,USBSTS,1U<<3); // EINT RW1C
    __atomic_store_n(&c.irq_pending,1U,__ATOMIC_RELEASE);
}
void release_interrupt(Controller& c) {
    if(c.irq_vector<0)return;
    write32(c.runtime+0x20U,0,1U);
    write32(c.operational,USBCMD,read32(c.operational,USBCMD)&~(1U<<2));
    if(c.irq_mode==2) {
        c.msix_entry->vector_ctrl=1;
        pci_msix_disable(c.pci.bus,c.pci.slot,c.pci.function,&c.msix);
    } else if(c.irq_mode==1) pci_msi_disable(c.pci.bus,c.pci.slot,c.pci.function,&c.msi);
    idt_wyrejestruj_irq(uint8_t(c.irq_vector));
    idt_zwolnij_wektor_irq(uint8_t(c.irq_vector));
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS,c.saved_command);
    c.irq_vector=-1;c.irq_mode=0;
}
bool interrupt_bar(Controller& c,uint8_t bir,uint64_t& size,volatile uint8_t*& mapped) {
    if(bir==0){size=c.bar_size;mapped=c.mmio;return true;}
    uint64_t physical=0;if(!probe_bar(c,bir,physical,size))return false;
#ifdef BURSZTYN_XHCI_HOST_TEST
    extern volatile uint8_t* xhci_host_map_bar(uint8_t,uint64_t,uint64_t);
    mapped=xhci_host_map_bar(bir,physical,size);return mapped!=nullptr;
#else
    const uint64_t va=MMIO_VIRTUAL_BASE+(uint64_t(c.index)*8+uint64_t(bir)+1)*MAX_MMIO_SIZE;
    for(uint64_t i=0;i<size;i+=PAGE){
        if(!ZmapujStroneSprawdzona(reinterpret_cast<void*>(va+i),reinterpret_cast<void*>(physical+i),VMM_FLAGA_PRESENT|VMM_FLAGA_ZAPIS|VMM_FLAGA_PWT|VMM_FLAGA_PCD)){wypisz_log("[xHCI-ERR] MSI-X BAR mapping failed");return false;}
    }
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax","memory");
    mapped=reinterpret_cast<volatile uint8_t*>(va);return true;
#endif
}
bool setup_interrupt(Controller& c) {
    BootTiming::Scope timing(BootTiming::IrqSetup);
    c.saved_command=pci_odczytaj_word(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    uint64_t table_size=0,pba_size=0;volatile uint8_t *table=nullptr,*pba=nullptr;
    const bool msix=pci_find_msix(c.pci.bus,c.pci.slot,c.pci.function,&c.msix) &&
        interrupt_bar(c,c.msix.table_bir,table_size,table) &&
        interrupt_bar(c,c.msix.pba_bir,pba_size,pba) &&
        pci_msix_validate_bar_bounds(&c.msix,table_size,pba_size);
    const bool msi=pci_find_msi(c.pci.bus,c.pci.slot,c.pci.function,&c.msi);
    if(!msix&&!msi)return false;
    c.irq_vector=idt_alokuj_wektor_irq();
    if(c.irq_vector<0)return false;
    if(!idt_zarejestruj_irq(uint8_t(c.irq_vector),xhci_irq,&c)) { release_interrupt(c);return false; }
    if(msix) {
        c.msix_entry=reinterpret_cast<volatile PciMsixTableEntry*>(table+c.msix.table_offset);
        c.msix_entry->vector_ctrl=1;
        c.msix_entry->msg_addr_low=0xFEE00000U|(uint32_t(apic_pobierz_lapic_id())<<12);
        c.msix_entry->msg_addr_high=0;c.msix_entry->msg_data=uint32_t(c.irq_vector);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        c.irq_mode=2;
        if(pci_msix_enable(c.pci.bus,c.pci.slot,c.pci.function,&c.msix) &&
           (pci_odczytaj_word(c.pci.bus,c.pci.slot,c.pci.function,uint8_t(c.msix.cap_offset+2))&0xC000U)==0x8000U) {
            c.msix_entry->vector_ctrl=0;return true;
        }
        pci_msix_disable(c.pci.bus,c.pci.slot,c.pci.function,&c.msix);
    }
    if(msi) {
        c.irq_mode=1;
        if(pci_msi_enable(c.pci.bus,c.pci.slot,c.pci.function,&c.msi,
            apic_pobierz_lapic_id(),uint8_t(c.irq_vector)) &&
           (pci_odczytaj_word(c.pci.bus,c.pci.slot,c.pci.function,uint8_t(c.msi.cap_offset+2))&1U))return true;
    }
    release_interrupt(c);return false;
}

uint32_t port_write_base(uint32_t old) {
    /* Zachowaj tylko rzeczywiste pola RW; RW1C, PR/WPR, PED i LWS sa jawne. */
    return old&((1U<<9)|(3U<<14)|(7U<<25));
}
void port_set(Controller& xhc,uint8_t port,uint32_t bits){const uint32_t o=PORTSC_BASE+(port-1U)*16U;
 const uint32_t old=read32(xhc.operational,o);write32(xhc.operational,o,port_write_base(old)|bits);}
void port_clear_changes(Controller& xhc,uint8_t port,uint32_t changes){port_set(xhc,port,changes&PORT_RW1C);}

SpeedInfo xhci_pobierz_predkosc_portu(Controller& xhc,uint8_t port) {
    SpeedInfo r{USB_SPEED_UNKNOWN,0,0};if(port==0||port>xhc.max_ports)return r;
    r.raw=static_cast<uint8_t>((read32(xhc.operational,PORTSC_BASE+(port-1U)*16U)>>10)&15U);
    Protocol* p=xhc.ports[port].protocol;if(!p)return r;
    if(p->psi_count){for(uint8_t i=0;i<p->psi_count;++i)if(p->psi[i].id==r.raw){
      uint64_t scale=1;for(uint8_t n=0;n<p->psi[i].exponent;++n)scale*=1000U;
      r.bitrate=static_cast<uint64_t>(p->psi[i].mantissa)*scale;
      if(r.bitrate<=1500000ULL)r.logical=USB_LOW_SPEED;
      else if(r.bitrate<=12000000ULL)r.logical=USB_FULL_SPEED;
      else if(r.bitrate<=480000000ULL)r.logical=USB_HIGH_SPEED;
      else if(r.bitrate<=5000000000ULL)r.logical=USB_SUPER_SPEED;
      else r.logical=USB_SUPER_SPEED_PLUS;
      return r;}return r;}
    if(p->usb2){if(r.raw==1)r.logical=USB_FULL_SPEED;else if(r.raw==2)r.logical=USB_LOW_SPEED;else if(r.raw==3)r.logical=USB_HIGH_SPEED;}
    else if(p->usb3){if(r.raw==4)r.logical=USB_SUPER_SPEED;else if(r.raw>=5)r.logical=USB_SUPER_SPEED_PLUS;}
    return r;
}

[[maybe_unused]] bool before_deadline(uint64_t deadline){return czas_monotoniczny_ns()<deadline;}
uint64_t deadline(){const uint64_t n=czas_monotoniczny_ns();return n>UINT64_MAX-WAIT_NS?UINT64_MAX:n+WAIT_NS;}

#ifdef BURSZTYN_XHCI_HOST_TEST
bool reset_port(Controller& xhc,uint8_t port) {
    BootTiming::Scope timing(BootTiming::PortReset,port);
    Protocol* p=xhc.ports[port].protocol;if(!p||(read32(xhc.operational,PORTSC_BASE+(port-1U)*16U)&PORT_CCS)==0)return false;
    const uint32_t initial=read32(xhc.operational,PORTSC_BASE+(port-1U)*16U);
    const int64_t pv[]={port,p->major,p->minor,initial};
    xhci_physical_diag("[XHCI-PHYSICAL port before] port/protocolMajor/minor/PORTSC",pv,4);
    // USB3 performs link training itself. An enabled U0 port needs no reset.
    if(p->usb3 && (initial&PORT_PED) && !(initial&(PORT_PR|PORT_WPR)) &&
       ((initial>>PORT_PLS_SHIFT)&PORT_PLS_MASK)==0) {
        port_clear_changes(xhc,port,initial&PORT_RW1C);physical_completion("USB3 already enabled U0",1);return true;
    }
    log_port_text(port," reset...","");xhc.pending_port=port;xhc.port_event=false;
    const unsigned link=(initial>>PORT_PLS_SHIFT)&PORT_PLS_MASK;
    const bool warm=p->usb3 && (link==6 || link==10 || link==8); // Inactive, Compliance, Recovery
    const uint32_t reset_change=warm?(1U<<19):PORT_PRC;
    port_clear_changes(xhc,port,initial&PORT_RW1C);port_set(xhc,port,warm?PORT_WPR:PORT_PR);
    const uint64_t end=deadline();uint32_t v=0;
    unsigned reset_spins=0;
    do{poll_controller_events(xhc,32);v=read32(xhc.operational,PORTSC_BASE+(port-1U)*16U);
       if((v&PORT_CCS)==0){xhc.pending_port=0;return false;}
       if((v&(PORT_PR|PORT_WPR))==0&&(v&reset_change)!=0)break;
       asm volatile("pause":::"memory");}while(++reset_spins<10000000&&before_deadline(end));
    xhc.pending_port=0;
    const int64_t result[]={port,warm,v};xhci_physical_diag("[XHCI-PHYSICAL port after] port/warmReset/PORTSC",result,3);
    if((v&(PORT_PR|PORT_WPR))!=0||(v&reset_change)==0){physical_completion("port reset timeout",0);return false;}
    physical_completion("port reset completed",1);
    port_clear_changes(xhc,port,reset_change);log_port_text(port," reset ","OK.");
    SpeedInfo s=xhci_pobierz_predkosc_portu(xhc,port);log_port_text(port," speed=",speed_name(s.logical));log_port_value(port," raw=",s.raw);
    log_port_value(port," PED=",(v&PORT_PED)?1:0);log_port_value(port," PLS=",(v>>PORT_PLS_SHIFT)&PORT_PLS_MASK);
    return (v&PORT_PED)!=0&&s.logical!=USB_SPEED_UNKNOWN;
}
#endif


#ifdef BURSZTYN_XHCI_HOST_TEST
CommandResult execute_command(Controller& xhc,XhciTrb trb) {
    BootTiming::Scope timing(xhci_trb::type(trb.dword3)==9?BootTiming::EnableSlot:(xhci_trb::type(trb.dword3)==11?BootTiming::AddressDevice:BootTiming::Command),xhci_trb::type(trb.dword3));
    uint64_t pa=0;CommandResult bad{};if(!xhci_command_ring_enqueue(&xhc.command,trb,&pa))return bad;
    xhc.wanted_command=pa;xhc.command_result={};__atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,0,0);
    const uint64_t end=deadline();for(unsigned spins=0;spins<10000000&&before_deadline(end);++spins){poll_controller_events(xhc,64);if(xhc.command_result.valid){xhc.wanted_command=0;
        const int64_t v[]={xhci_trb::type(trb.dword3),int64_t(pa),xhc.command_result.slot,xhc.command_result.code};
        xhci_physical_diag("[XHCI-PHYSICAL command] type/TRB/slot/code",v,4);
        physical_completion("command",xhc.command_result.code);return xhc.command_result;}asm volatile("pause":::"memory");}
    xhc.wanted_command=0;physical_completion("command timeout",0);return bad;
}
#endif

#ifdef BURSZTYN_XHCI_HOST_TEST
CommandResult simple_slot_command(Controller& xhc,uint32_t type,uint8_t slot){XhciTrb t{};t.dword3=xhci_trb::type_field(type)|(static_cast<uint32_t>(slot)<<24);return execute_command(xhc,t);}
#endif

#ifdef BURSZTYN_XHCI_HOST_TEST
bool disable_slot(Controller& xhc,XhciDevice& d){if(!d.enabled)return true;CommandResult r=simple_slot_command(xhc,xhci_trb::TYPE_DISABLE_SLOT,d.slot_id);if(!r.valid||r.code!=1||r.slot!=d.slot_id)return false;d.enabled=false;return true;}
#endif

void remove_hid(XhciHid& hi) {
    hid_pointer_remove(hi.pointer_device);
    if(hi.keyboard_device.registered){
        const uint8_t released[8]{};
        usb_hid_keyboard_report(hi.keyboard_device,hi.poprzedni_raport,released,8);
        input_unregister_device(&hi.keyboard_device);
    }
}
void release_mouse_buttons(XhciDevice& d){
    for(size_t i=0;i<(d.hid_count?d.hid_count:1U);++i)remove_hid(hid_at(d,i));
}
void fail_controller(Controller& xhc) {
    xhc.ready=false;
    write32(xhc.runtime+0x20U,0,1U);
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)&~(CMD_RS|(1U<<2)));
    for(auto& d:xhc.devices) {
        if(d.diag_id)diag_withdraw_tree(d.diag_id);
        for(size_t i=0;i<(d.hid_count?d.hid_count:1U);++i){auto& hi=hid_at(d,i);hi.hid_gotowy=false;hi.hid_result={};}
        release_mouse_buttons(d);
    }
    release_interrupt(xhc);
    // Without a successful halt/Disable Slot, retain every DMA allocation.
    pointing_snapshot("xhci-controller-error-dma-quarantined");
}
void free_device(Controller& xhc,XhciDevice& d){
    if(d.enabled)return; // DMA may be released only after Disable Slot.
    if(d.diag_id&&d.root_port<=xhc.max_ports&&xhc.ports[d.root_port].state!=PortState::Failed)diag_withdraw_tree(d.diag_id);
    release_mouse_buttons(d);
    if(xhc.dcbaa.virtual_address && d.slot_id && d.slot_id<=xhc.enabled_slots) {
        static_cast<uint64_t*>(xhc.dcbaa.virtual_address)[d.slot_id]=0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }
    for(size_t i=0;i<(d.hid_count?d.hid_count:1U);++i){auto& hi=hid_at(d,i);xhci_command_ring_destroy(&hi.hid_ring);dma_release(&hi.hid_raport);}
    dma_release(&d.interface_storage);xhci_command_ring_destroy(&d.ep0);dma_release(&d.input_context);dma_release(&d.output_context);d=XhciDevice{};
}


uint32_t* xhci_context_ptr(Controller& xhc,DmaBuffer& kontekst,uint8_t indeks){
    return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(kontekst.virtual_address)+static_cast<size_t>(indeks+1U)*xhc.context_size);
}
void xhci_log_cfg(const char* pole,uint64_t wartosc){
#if BURSZTYN_DEBUG_XHCI
    static unsigned config_lines=0;if(config_lines>=64)return;++config_lines;
    char s[128],*p=s,*e=s+sizeof(s);append(p,e,"[xHCI-CFG] ");append(p,e,pole);append(p,e,"=");append_hex(p,e,wartosc);*p=0;wypisz_log(s);
#else
    (void)pole;(void)wartosc;
#endif
}
bool xhci_waliduj_configure_endpoint(Controller& xhc,XhciDevice& d,const hid_wynik_konfiguracji& h,uint32_t* kontrola,uint32_t* slot,uint32_t* ep,XhciHid* interface=nullptr){
    auto& hi=interface?*interface:static_cast<XhciHid&>(d);

    const uint32_t add=kontrola[1],drop=kontrola[0],entries=(slot[0]>>27)&31U,typ=(ep[1]>>3)&7U,stan=ep[0]&7U;
    const uint32_t interval=(ep[0]>>16)&255U,mps=(ep[1]>>16)&0x7FFU,burst=(ep[1]>>8)&255U,esit=ep[4]>>16,pstreams=(ep[0]>>10)&31U;
    const uint64_t dequeue=(static_cast<uint64_t>(ep[3])<<32)|(ep[2]&~15U);bool ok=true;
    const bool super=d.logical_speed==USB_SUPER_SPEED||d.logical_speed==USB_SUPER_SPEED_PLUS;
    const uint64_t payload=super?h.bytes_per_interval:h.maksymalny_rozmiar*(1U+h.transactions);
    const uint64_t expected_burst=super?h.burst:h.transactions;
    const uint64_t oczekiwany[]={h.dci,(1U<<0)|(1U<<h.dci),0,uint64_t(h.dci>((context(xhc,d.output_context,0)[0]>>27)&31U)?h.dci:((context(xhc,d.output_context,0)[0]>>27)&31U)),0,7,hi.hid_interwal,h.maksymalny_rozmiar,expected_burst,payload,0,1};const uint64_t faktyczny[]={hi.hid_dci,add,drop,entries,stan,typ,interval,mps,burst,esit,pstreams,ep[2]&1U};
    const char* nazwy[]={"dci","add_flags","drop_flags","context_entries","ep_state","ep_type","interval","max_packet","max_burst","max_esit","max_pstreams","dcs"};
    for(size_t i=0;i<sizeof(faktyczny)/sizeof(faktyczny[0]);++i)if(faktyczny[i]!=oczekiwany[i]){char b[64],*p=b,*e=b+sizeof(b);append(p,e,"[xHCI-VALIDATE] FAIL pole=");append(p,e,nazwy[i]);append(p,e," value=");append_hex(p,e,faktyczny[i]);*p=0;wypisz_log(b);ok=false;}
    if((h.endpoint&0x80U)==0||h.dci!=xhci_oblicz_dci(static_cast<uint8_t>(h.endpoint&15U),true)){wypisz_log("[xHCI-VALIDATE] FAIL pole=descriptor_endpoint");ok=false;}
    if((reinterpret_cast<uintptr_t>(d.input_context.virtual_address)&63U)!=0||dequeue==0||(ep[2]&15U)!=1){wypisz_log("[xHCI-VALIDATE] FAIL pole=alignment_or_dequeue");ok=false;}
    return ok;
}
void hid_configuration(const UsbHidInterface& u,hid_wynik_konfiguracji* h){
    *h={};h->konfiguracja=u.configuration;h->interfejs=u.interface_number;h->alternatywa=u.alternate;
    h->endpoint=u.endpoint;h->hid_bcd=u.hid_bcd;h->raport_dlugosc=u.report_length;
    h->maksymalny_rozmiar=u.packet_size;h->interwal=u.interval;
    h->subclass=u.subclass;h->protocol=u.protocol;h->burst=u.max_burst;h->transactions=u.transactions;h->bytes_per_interval=u.bytes_per_interval;h->companion=u.companion;
    h->dci=uint8_t((u.endpoint&15)*2+1);h->znaleziono=true;
    h->type=u.subclass==1&&u.protocol==1?USB_HID_BOOT_KEYBOARD:USB_HID_POINTER;
}
[[maybe_unused]] bool usb_sprawdz_deskryptor_konfiguracji(const uint8_t* data,size_t length,hid_wynik_konfiguracji* h){
    BootTiming::Scope timing(BootTiming::ConfigParse);
    UsbHidInterface u{};
    if(!h||!usb_hid_find_interface(data,length,u))return false;
    hid_configuration(u,h);
    return true;
}


uint8_t xhci_oblicz_dci(uint8_t numer_endpointu,bool kierunek){if(numer_endpointu==0||numer_endpointu>15)return 0;return static_cast<uint8_t>(numer_endpointu*2U+(kierunek?1U:0U));}
uint8_t xhci_oblicz_interwal(UsbSpeed predkosc,uint8_t b_interval){
    if(b_interval==0)return 0;
    if((predkosc==USB_HIGH_SPEED||predkosc==USB_SUPER_SPEED||predkosc==USB_SUPER_SPEED_PLUS)&&b_interval>16)return 0;
    if(predkosc==USB_HIGH_SPEED||predkosc==USB_SUPER_SPEED||predkosc==USB_SUPER_SPEED_PLUS)return static_cast<uint8_t>(b_interval-1U);
    uint8_t potega=0;uint16_t okres=1;while(okres*2U<=b_interval){okres=uint16_t(okres<<1U);++potega;}return static_cast<uint8_t>(potega+3U);
}
bool prepare_hid_endpoint(Controller& xhc,XhciDevice& d,const hid_wynik_konfiguracji& h,XhciTrb& command,XhciHid* interface=nullptr){
    auto& hi=interface?*interface:static_cast<XhciHid&>(d);

    BootTiming::Scope timing(BootTiming::HidSetup,d.slot_id);
    if(!h.znaleziono||h.dci<2||h.dci>31||h.maksymalny_rozmiar==0)return false;
    if((d.logical_speed==USB_LOW_SPEED&&h.maksymalny_rozmiar>8)||
       (d.logical_speed==USB_FULL_SPEED&&h.maksymalny_rozmiar>64))return false;
    const bool super=d.logical_speed==USB_SUPER_SPEED||d.logical_speed==USB_SUPER_SPEED_PLUS;
    if((super && (!h.companion || !h.bytes_per_interval || h.bytes_per_interval>uint32_t(h.maksymalny_rozmiar)*(h.burst+1U))) ||
       (!super && h.burst) || h.transactions>2 || (d.logical_speed!=USB_HIGH_SPEED && h.transactions))return false;
    const uint16_t esit=super?h.bytes_per_interval:uint16_t(h.maksymalny_rozmiar*(1U+h.transactions));
    const uint8_t burst=super?h.burst:h.transactions;
    const size_t dma_length=esit;
    // One Normal TRB may not cross a 64 KiB boundary (xHCI 4.11.7).
    // This bounded HID path needs no scatter/gather or streams.
    if(!create_ring(xhc,&hi.hid_ring,HID_TRBS)||!allocate_dma(xhc,dma_length,64,&hi.hid_raport))return false;
    if((hi.hid_raport.physical_address&65535U)+dma_length>65536U) {
        xhci_command_ring_destroy(&hi.hid_ring);dma_release(&hi.hid_raport);
        physical_completion("HID DMA crosses 64KiB boundary",0);return false;
    }
    hi.hid_dma_length=dma_length;hi.type=h.type;hi.report_roles=h.subclass==1&&h.protocol==1?HID_ROLE_KEYBOARD:h.subclass==1&&h.protocol==2?HID_ROLE_POINTER:HID_ROLE_UNKNOWN;
    const int64_t values[]={d.slot_id,h.konfiguracja,h.interfejs,h.subclass,h.protocol,h.endpoint,h.maksymalny_rozmiar,
        h.interwal,h.dci,xhci_oblicz_interwal(d.logical_speed,h.interwal),esit,esit,burst};
    xhci_physical_diag("[XHCI-PHYSICAL HID endpoint] slot/config/interface/subclass/protocol/address/MPS/bInterval/DCI/interval/avgTRB/MaxESIT/burst",values,13);
    hi.hid_dci=h.dci;hi.hid_endpoint=h.endpoint;hi.hid_mps=h.maksymalny_rozmiar;hi.hid_interwal=xhci_oblicz_interwal(d.logical_speed,h.interwal);hi.hid_interfejs=h.interfejs;
    if(!h.interwal || d.logical_speed==USB_SPEED_UNKNOWN ||
       ((d.logical_speed==USB_HIGH_SPEED||d.logical_speed==USB_SUPER_SPEED||d.logical_speed==USB_SUPER_SPEED_PLUS)&&h.interwal>16)){xhci_command_ring_destroy(&hi.hid_ring);dma_release(&hi.hid_raport);return false;}
    zero_dma(d.input_context);uint32_t* icc=context(xhc,d.input_context,0);icc[0]=0;icc[1]=(1U<<0)|(1U<<hi.hid_dci);uint32_t* sc=context(xhc,d.input_context,1);const uint32_t* obecny_slot=context(xhc,d.output_context,0);for(uint8_t i=0;i<xhc.context_size/4U;++i)sc[i]=obecny_slot[i];sc[0]=(sc[0]&~(31U<<27))|(static_cast<uint32_t>(hi.hid_dci> ((sc[0]>>27)&31U)?hi.hid_dci:((sc[0]>>27)&31U))<<27);
    uint32_t* ep=xhci_context_ptr(xhc,d.input_context,hi.hid_dci);ep[0]=static_cast<uint32_t>(hi.hid_interwal)<<16;ep[1]=(static_cast<uint32_t>(hi.hid_mps)<<16)|(uint32_t(burst)<<8)|(3U<<1)|(7U<<3);ep[2]=static_cast<uint32_t>(hi.hid_ring.dma.physical_address)|1U;ep[3]=static_cast<uint32_t>(hi.hid_ring.dma.physical_address>>32);ep[4]=uint32_t(esit)|(uint32_t(esit)<<16);
    xhci_log_cfg("slot",d.slot_id);xhci_log_cfg("speed",d.speed_id);xhci_log_cfg("ep_address",h.endpoint);xhci_log_cfg("dci",hi.hid_dci);xhci_log_cfg("drop_flags",icc[0]);xhci_log_cfg("add_flags",icc[1]);xhci_log_cfg("context_entries",(sc[0]>>27)&31U);
    xhci_log_cfg("ep_state",ep[0]&7U);xhci_log_cfg("ep_type",(ep[1]>>3)&7U);xhci_log_cfg("descriptor_bInterval",h.interwal);xhci_log_cfg("interval",(ep[0]>>16)&255U);xhci_log_cfg("max_packet",(ep[1]>>16)&0x7FFU);xhci_log_cfg("max_burst",(ep[1]>>8)&255U);xhci_log_cfg("max_esit",ep[4]>>16);xhci_log_cfg("cerr",(ep[1]>>1)&3U);xhci_log_cfg("max_pstreams",(ep[0]>>10)&31U);
    xhci_log_cfg("dequeue_phys",(static_cast<uint64_t>(ep[3])<<32)|(ep[2]&~15U));xhci_log_cfg("dcs",ep[2]&1U);xhci_log_cfg("input_context_phys",d.input_context.physical_address);xhci_log_cfg("input_context_alignment",reinterpret_cast<uintptr_t>(d.input_context.virtual_address)&63U);xhci_log_cfg("hid_ring_phys",hi.hid_ring.dma.physical_address);xhci_log_cfg("hid_ring_alignment",hi.hid_ring.dma.physical_address&15U);
    xhci_log_cfg("interrupt_in_context_phys",d.input_context.physical_address+static_cast<uint64_t>(hi.hid_dci+1U)*xhc.context_size);
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"input_control_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,context(xhc,d.input_context,0)[i]);}
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"input_slot_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,sc[i]);}
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"interrupt_in_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,ep[i]);}
    if(!xhci_waliduj_configure_endpoint(xhc,d,h,icc,sc,ep,&hi)){xhci_command_ring_destroy(&hi.hid_ring);dma_release(&hi.hid_raport);return false;}
    __atomic_thread_fence(__ATOMIC_RELEASE);XhciTrb t{};t.dword0=static_cast<uint32_t>(d.input_context.physical_address);t.dword1=static_cast<uint32_t>(d.input_context.physical_address>>32);t.dword3=xhci_trb::type_field(xhci_trb::TYPE_CONFIGURE_ENDPOINT)|(static_cast<uint32_t>(d.slot_id)<<24);
    xhci_log_cfg("configure_trb_dword0",t.dword0);xhci_log_cfg("configure_trb_dword1",t.dword1);xhci_log_cfg("configure_trb_dword2",t.dword2);xhci_log_cfg("configure_trb_dword3",t.dword3);
    command=t;return true;
}
#ifdef BURSZTYN_XHCI_HOST_TEST
bool xhci_skonfiguruj_endpoint(Controller& xhc,XhciDevice& d,const hid_wynik_konfiguracji& h){
    XhciTrb t{};if(!prepare_hid_endpoint(xhc,d,h,t))return false;
    const auto r=execute_command(xhc,t);return r.valid&&r.code==1&&r.slot==d.slot_id;
}
#endif







#include "sterowniki/usb/usb_hid.h"
bool xhci_zakolejkuj_transfer_interrupt_in(Controller& xhc,XhciDevice& d,XhciHid* interface=nullptr){
    auto& hi=interface?*interface:static_cast<XhciHid&>(d);
BootTiming::Scope timing(BootTiming::FirstReceive,d.slot_id,!hi.pierwszy_raport_zalogowany);if(!d.enabled||d.retiring||!hi.hid_gotowy||hi.hid_transfer_oczekiwany||!hi.hid_raport.virtual_address)return false;zero_dma(hi.hid_raport);XhciTrb t{};t.dword0=static_cast<uint32_t>(hi.hid_raport.physical_address);t.dword1=static_cast<uint32_t>(hi.hid_raport.physical_address>>32);t.dword2=static_cast<uint32_t>(hi.hid_dma_length);t.dword3=xhci_trb::type_field(xhci_trb::TYPE_NORMAL)|(1U<<5)|(1U<<2);
    uint64_t ptr=0;if(!xhci_command_ring_enqueue(&hi.hid_ring,t,&ptr))return false;__atomic_thread_fence(__ATOMIC_RELEASE);hi.hid_transfer_oczekiwany=true;hi.hid_transfer_pointer=ptr;
    if(hi.type==USB_HID_POINTER)input_arm_pointer(&hi.pointer_device);
    if(!hi.pierwszy_raport_zalogowany)pointing_snapshot("first-interrupt-in-submitted");
    if(!hi.pierwszy_raport_zalogowany && hi.type==USB_HID_POINTER){const int64_t v[]={d.slot_id,hi.hid_dci,int64_t(ptr),int64_t(hi.hid_dma_length)};
        xhci_physical_diag("[XHCI-PHYSICAL first receive] slot/DCI/TRB/length",v,4);}
    write32(xhc.doorbells,static_cast<uint32_t>(d.slot_id)*4U,hi.hid_dci);if(!hi.pierwszy_raport_zalogowany)wypisz_log(hi.type==USB_HID_BOOT_MOUSE?"[USB-MYSZ] First Interrupt IN transfer queued.":"[USB-HID] First Interrupt IN transfer queued.");return true;}
void xhci_obsluz_hid(Controller& xhc,XhciDevice& d,XhciHid* interface=nullptr) {
    auto& hi=interface?*interface:static_cast<XhciHid&>(d);

    if(!d.enabled||d.retiring||!hi.hid_gotowy)return;
    if(hi.hid_result.valid) {
        const TransferResult r=hi.hid_result;
        if(!hi.hid_transfer_oczekiwany || r.slot!=d.slot_id || r.endpoint!=hi.hid_dci ||
           r.pointer!=hi.hid_transfer_pointer) { hi.hid_result={}; return; }
#if BURSZTYNDIAGXHCIPOINTING
        if(!hi.pierwszy_raport_zalogowany || (r.code!=1&&r.code!=13)) {
            const int64_t values[]={r.slot,r.endpoint,r.code,int64_t(r.pointer),r.residual,int64_t(hi.hid_dma_length)};
            pointing_diag("[XHCI completion] slot/endpoint/code/TRB/residual/requested",values,6);
        }
#endif
        if(hi.type==USB_HID_POINTER && hi.diagnostic_transfers<16){
            const int64_t v[]={int64_t(r.pointer),int64_t(hi.hid_transfer_pointer),r.slot,r.endpoint,r.code,r.residual,
                r.residual<=hi.hid_dma_length?int64_t(hi.hid_dma_length-r.residual):-1};
            xhci_physical_diag("[XHCI-PHYSICAL transfer] TRB/expected/slot/DCI/code/residual/actual",v,7);
            physical_completion("interrupt IN",r.code);
        }
        if(hi.diagnostic_reports<4){
            log_hid(xhc,d,hi,"interrupt completion");
            log_values("[USB HID completion] code=",r.code," residual=",r.residual);
        }
        pointing_count(pointing_counters.hid_completions);
        hi.hid_result={};hi.hid_transfer_oczekiwany=false;hi.hid_transfer_pointer=0;
        xhci_ring_complete(&hi.hid_ring,1);
        if((r.code==1||r.code==13)&&r.residual<=hi.hid_dma_length) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            const size_t actual=hi.hid_dma_length-r.residual;
            const uint8_t* report=static_cast<const uint8_t*>(hi.hid_raport.virtual_address);
            if(hi.type==USB_HID_POINTER) {
                HidPointerLayout wheel_layout{};
                const HidPointerLayout* layout=&hi.pointer_layout;
                if(hi.boot_mouse && hi.boot_wheel && actual>=4 && hid_boot_mouse_layout(wheel_layout,true))layout=&wheel_layout;
                const bool accepted=hid_pointer_publish(hi.pointer_device,*layout,report,actual,(uint32_t(xhc.index)<<24)|(uint32_t(hi.hid_interfejs)<<16)|d.slot_id);
                if(accepted&&!hi.timing_active){hi.timing_active=true;BootTiming::mark("first HID input completion",d.slot_id);BootTiming::mark("xHCI device active",d.slot_id);}
                if(hi.diagnostic_reports<16){++hi.diagnostic_reports;
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
                    for(size_t offset=0;offset<actual && offset<64;offset+=8){
                        int64_t bytes[10]={int64_t(actual),int64_t(offset)};size_t n=2;
                        for(size_t i=offset;i<actual && i<offset+8;++i)bytes[n++]=report[i];
                        xhci_physical_diag("[XHCI-PHYSICAL raw mouse bytes] actual/offset/bytes(max64)",bytes,n);
                    }
#endif
                    // Never log keyboard reports; these are only pointer endpoint bytes.
                    const int64_t raw[]={d.slot_id,int64_t(actual),actual>0?report[0]:0,actual>1?report[1]:0,
                        actual>2?report[2]:0,actual>3?report[3]:0,accepted,hi.pointer_device.pointer_mode,
                        hi.pointer_device.pointer_state,input_get_pointer_owner()?int64_t(input_get_pointer_owner()->device_id):-1};
                    xhci_physical_diag("[XHCI-PHYSICAL mouse report] slot/actual/first4bytes/valid/mode/state/owner",raw,10);
                }
                if(accepted){
                    if(hi.diagnostic_reports<=4)log_hid(xhc,d,hi,"pointer report decoded");
                    ++xhc.mouse_reports_processed;
                }
            } else if(hi.type==USB_HID_BOOT_KEYBOARD&&actual>=8) {
                if(hi.diagnostic_reports<4){++hi.diagnostic_reports;log_hid(xhc,d,hi,"keyboard report decoded");}
                hi.keyboard_device.device_id=(uint32_t(xhc.index)<<24)|(uint32_t(hi.hid_interfejs)<<16)|d.slot_id;
                ++xhc.keyboard_reports_processed;usb_hid_keyboard_report(hi.keyboard_device,hi.poprzedni_raport,report,actual);
                if(!hi.timing_active){hi.timing_active=true;BootTiming::mark("first HID input completion",d.slot_id);BootTiming::mark("xHCI device active",d.slot_id);}
            } else if(hi.type==USB_HID_BOOT_KEYBOARD&&hi.diagnostic_reports<4){
                ++hi.diagnostic_reports;log_hid(xhc,d,hi,"short keyboard report discarded");
            }
            hi.pierwszy_raport_zalogowany=true;
        } else {
            // A stalled/halted endpoint is not a completed reusable transfer.
            // Retain DMA until Disable Slot confirms quiescence on disconnect.
            log_hid(xhc,d,hi,"interrupt transfer failed",DIAG_INIT_FAILED);log_values("[USB HID error] completion=",r.code," residual=",r.residual);
            hi.hid_gotowy=false;remove_hid(hi);pointing_snapshot("xhci-transfer-error");return;
        }
    }
    if(!hi.hid_transfer_oczekiwany && !xhci_zakolejkuj_transfer_interrupt_in(xhc,d,&hi)) {
        log_hid(xhc,d,hi,"interrupt IN requeue failed",DIAG_INIT_FAILED);
        hi.hid_gotowy=false; remove_hid(hi); pointing_snapshot("xhci-requeue-failed");
    }
    if(hi.type==USB_HID_POINTER && hi.diagnostic_transfers<16){++hi.diagnostic_transfers;
        const int64_t v[]={d.slot_id,hi.hid_dci,int64_t(hi.hid_transfer_pointer),hi.hid_transfer_oczekiwany};
        xhci_physical_diag("[XHCI-PHYSICAL receive queued] slot/DCI/TRB/outstanding",v,4);
    }
}

uint32_t* context(Controller& xhc,DmaBuffer& b,uint8_t index){return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(b.virtual_address)+static_cast<size_t>(index)*xhc.context_size);}
void zero_dma(DmaBuffer& b){volatile uint8_t* p=static_cast<volatile uint8_t*>(b.virtual_address);for(size_t i=0;i<b.size;++i)p[i]=0;}
bool prepare_device(Controller& xhc,XhciDevice& d,uint8_t slot,uint8_t port,SpeedInfo speed) {
    d.slot_id=slot;d.root_port=port;d.speed_id=speed.raw;d.logical_speed=speed.logical;d.protocol=xhc.ports[port].protocol;d.enabled=true;
    const size_t out_size=32U*xhc.context_size,in_size=33U*xhc.context_size;
    if(!allocate_dma(xhc,out_size,64,&d.output_context)||!allocate_dma(xhc,in_size,64,&d.input_context)||!create_ring(xhc,&d.ep0,EP0_TRBS))return false;
    uint64_t* dcbaa=static_cast<uint64_t*>(xhc.dcbaa.virtual_address);dcbaa[slot]=d.output_context.physical_address;
    uint32_t* icc=context(xhc,d.input_context,0);icc[1]=(1U<<0)|(1U<<1);
    uint32_t* sc=context(xhc,d.input_context,1);sc[0]=(static_cast<uint32_t>(speed.raw)<<20)|(1U<<27);sc[1]=static_cast<uint32_t>(port)<<16;
    switch(speed.logical){case USB_LOW_SPEED:case USB_FULL_SPEED:d.ep0_mps=8;break;case USB_HIGH_SPEED:d.ep0_mps=64;break;case USB_SUPER_SPEED:case USB_SUPER_SPEED_PLUS:d.ep0_mps=512;break;default:return false;}
    log_values("[xHCI] EP0 initial MaxPacket=",d.ep0_mps);
    uint32_t* ep=context(xhc,d.input_context,2);ep[1]=(static_cast<uint32_t>(d.ep0_mps)<<16)|(4U<<3)|(3U<<1);
    ep[2]=static_cast<uint32_t>(d.ep0.dma.physical_address)|1U;ep[3]=static_cast<uint32_t>(d.ep0.dma.physical_address>>32);ep[4]=8;
    __atomic_thread_fence(__ATOMIC_RELEASE);log_values("[xHCI] Device Context address=",d.output_context.physical_address,nullptr,0,true);
    wypisz_log("[xHCI] Device Context OK.");log_values("[xHCI] EP0 Ring address=",d.ep0.dma.physical_address,nullptr,0,true);wypisz_log("[xHCI] EP0 Ring OK.");return true;
}







#ifdef BURSZTYN_XHCI_HOST_TEST
bool usb_control_transfer(Controller& xhc,XhciDevice& d,const UsbSetupPacket& setup,DmaBuffer* data,
                          uint16_t length,uint32_t* actual,uint8_t* completion) {
    BootTiming::Scope timing(BootTiming::Control,(uint32_t(setup.bRequest)<<16)|setup.wValue);
    if(d.control_failed||!d.enabled||setup.wLength!=length|| (length&&(!data||data->size<length||!addressable(xhc,*data))))return false;
    XhciTrb s{},dt{},st{};
    xhci_control_trbs(reinterpret_cast<const uint8_t*>(&setup),data?data->physical_address:0,length,s,dt,st);
    // Reserve the whole sequence before publishing any stage.
    if(d.ep0.outstanding+unsigned(length?3:2)>unsigned(d.ep0.trb_count-2))return false;
    uint64_t setup_ptr=0,data_ptr=0,last=0;
    const bool setup_cycle=d.ep0.producer_cycle;
    if(!xhci_command_ring_enqueue(&d.ep0,s,&setup_ptr,false))return false;
    uint16_t count=1;if(length){
      if(!xhci_command_ring_enqueue(&d.ep0,dt,&data_ptr))return false;
      ++count;}
    if(!xhci_command_ring_enqueue(&d.ep0,st,&last))return false;
    ++count;
    const size_t setup_index=size_t((setup_ptr-d.ep0.dma.physical_address)/sizeof(XhciTrb));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&d.ep0.trbs[setup_index].dword3,s.dword3|(setup_cycle?1U:0U),__ATOMIC_RELEASE);
    xhc.wanted_transfer=last;xhc.wanted_setup=setup_ptr;xhc.wanted_data=data_ptr;
    xhc.control_slot=d.slot_id;xhc.control_residual=0;xhc.transfer_result={};__atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,static_cast<uint32_t>(d.slot_id)*4U,1U);
    const uint64_t end=deadline();for(unsigned spins=0;spins<10000000&&before_deadline(end);++spins){poll_controller_events(xhc,64);if((read32(xhc.operational,PORTSC_BASE+(d.root_port-1U)*16U)&PORT_CCS)==0)break;
      if(xhc.transfer_result.valid){TransferResult r=xhc.transfer_result;xhc.wanted_transfer=0;xhci_ring_complete(&d.ep0,count);if(completion)*completion=r.code;
        if(d.diagnostic_controls<32){++d.diagnostic_controls;
            const int64_t v[]={d.slot_id,setup.bRequest,setup.wValue,setup.wIndex,length,r.code,
                r.residual<=length?int64_t(length-r.residual):-1};
            xhci_physical_diag("[XHCI-PHYSICAL control] slot/request/value/index/requested/code/actual",v,7);
            physical_completion("control",r.code);}
        if(r.code==6) { // EP0 is halted after Stall; skip the rest of this TD before reuse.
            XhciTrb reset{};reset.dword3=xhci_trb::type_field(14)|(1U<<16)|(uint32_t(d.slot_id)<<24);
            CommandResult rr=execute_command(xhc,reset);
            if(rr.valid&&rr.code==1){const uint64_t next=d.ep0.dma.physical_address+uint64_t(d.ep0.producer_index)*16;
                reset={uint32_t(next)|(d.ep0.producer_cycle?1U:0U),uint32_t(next>>32),0,
                    xhci_trb::type_field(16)|(1U<<16)|(uint32_t(d.slot_id)<<24)};
                rr=execute_command(xhc,reset);}
            d.control_failed=!rr.valid||rr.code!=1;
        }
        if(r.slot!=d.slot_id||r.endpoint!=1||r.residual>length||(r.code!=1&&r.code!=13))return false;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);if(actual)*actual=length-r.residual;return true;}asm volatile("pause":::"memory");}
    xhc.wanted_transfer=0;d.control_failed=true;physical_completion("control timeout/disconnect",0);
    // Never release a buffer while the controller may still DMA to it.
    if(!disable_slot(xhc,d)&&data)*data={}; // intentional DMA quarantine
    return false;
}
#endif




bool valid_mps(XhciDevice& d,uint8_t raw,uint16_t* mps){uint16_t n=raw;if((d.logical_speed==USB_SUPER_SPEED||d.logical_speed==USB_SUPER_SPEED_PLUS)){if(raw>15)return false;n=static_cast<uint16_t>(1U<<raw);}
 if(d.logical_speed==USB_LOW_SPEED&&n!=8)return false;
 if(d.logical_speed==USB_HIGH_SPEED&&n!=64)return false;
 if((d.logical_speed==USB_SUPER_SPEED||d.logical_speed==USB_SUPER_SPEED_PLUS)&&n!=512)return false;
 if(d.logical_speed==USB_FULL_SPEED&&(n!=8&&n!=16&&n!=32&&n!=64))return false;
 *mps=n;return true;}




bool submit_control(Controller& xhc,XhciDevice& d,const UsbSetupPacket& setup,DmaBuffer* data,
                          uint16_t length) {
    BootTiming::Scope timing(BootTiming::Control,(uint32_t(setup.bRequest)<<16)|setup.wValue);
    if(d.control_failed||!d.enabled||setup.wLength!=length|| (length&&(!data||data->size<length||!addressable(xhc,*data))))return false;
    XhciTrb s{},dt{},st{};
    xhci_control_trbs(reinterpret_cast<const uint8_t*>(&setup),data?data->physical_address:0,length,s,dt,st);
    // Reserve the whole sequence before publishing any stage.
    if(d.ep0.outstanding+unsigned(length?3:2)>unsigned(d.ep0.trb_count-2))return false;
    uint64_t setup_ptr=0,data_ptr=0,last=0;
    const bool setup_cycle=d.ep0.producer_cycle;
    if(!xhci_command_ring_enqueue(&d.ep0,s,&setup_ptr,false))return false;
    uint16_t count=1;if(length){
      if(!xhci_command_ring_enqueue(&d.ep0,dt,&data_ptr))return false;
      ++count;}
    if(!xhci_command_ring_enqueue(&d.ep0,st,&last))return false;
    ++count;
    const size_t setup_index=size_t((setup_ptr-d.ep0.dma.physical_address)/sizeof(XhciTrb));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&d.ep0.trbs[setup_index].dword3,s.dword3|(setup_cycle?1U:0U),__ATOMIC_RELEASE);
    xhc.wanted_transfer=last;xhc.wanted_setup=setup_ptr;xhc.wanted_data=data_ptr;
    xhc.control_slot=d.slot_id;xhc.control_residual=0;xhc.transfer_result={};__atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,static_cast<uint32_t>(d.slot_id)*4U,1U);
    BootTiming::mark("control submit",(uint32_t(setup.bRequest)<<16)|setup.wValue);
    return true;
}

#include "sterowniki/usb/xhci_enumeration.inc"




}

uint32_t xhci_read32(const volatile void* base,uint32_t offset){return read32(static_cast<const volatile uint8_t*>(base),offset);}
void xhci_write32(volatile void* base,uint32_t offset,uint32_t value){write32(static_cast<volatile uint8_t*>(base),offset,value);}
uint64_t xhci_read64(const volatile void* base,uint32_t offset){return read64(static_cast<const volatile uint8_t*>(base),offset);}
void xhci_write64(volatile void* base,uint32_t offset,uint64_t value){write64(static_cast<volatile uint8_t*>(base),offset,value);}


namespace {
bool initialize_controller(Controller& xhc) {
    if(xhc.ready)return true;
    if(xhc.attempted)return false; // Never zero live/quarantined DMA or allocate twice.
    xhc.attempted=true;
    struct InitGuard {
        Controller& xhc;
        uint64_t initial_flags() { uint64_t f;asm volatile("pushfq; popq %0":"=r"(f));return f; }
        uint64_t flags_before=initial_flags();
        uint32_t before=pointing_read(pointing_counters.ps2_irq);
        ~InitGuard() {
            if(!xhc.ready) {
                if(xhc.irq_vector>=0) {
                    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)&~(CMD_RS|(1U<<2)));
                    wait_bits(xhc.operational,USBSTS,STS_HCH,STS_HCH);
                }
                release_interrupt(xhc);
            }
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
            uint64_t current_flags;asm volatile("pushfq; popq %0":"=r"(current_flags));
            const int64_t v[]={xhc.ready,xhc.irq_mode,xhc.irq_vector,int64_t((current_flags>>9)&1),
                int64_t((flags_before>>9)&1),xhc.operational?read32(xhc.operational,USBSTS):0};
            xhci_physical_diag("[XHCI-PHYSICAL init final] ready/IRQmode(1=MSI,2=MSIX)/vector/IFafter/IFbefore/USBSTS",v,6);
#endif
#if BURSZTYNDIAGXHCIPOINTING
            uint64_t flags;asm volatile("pushfq; popq %0":"=r"(flags));
            const int64_t changed_if[]={int64_t((flags^flags_before)&512U)};
            pointing_diag("[XHCI INIT] IF changed",changed_if,1);
            const auto* owner=input_get_pointer_owner();
            char line[320],*p=line,*e=line+sizeof(line);
            append(p,e,"[XHCI INIT] PS2IRQ12BEFORE=");append_dec(p,e,before);
            append(p,e," PS2IRQ12AFTER=");append_dec(p,e,pointing_read(pointing_counters.ps2_irq));
            append(p,e," CURRENTPOINTEROWNER=");append(p,e,owner?owner->name:"none");
            append(p,e," XHCIVECTOR=");append_dec(p,e,xhc.irq_vector<0?0:uint32_t(xhc.irq_vector));
            append(p,e," PS2VECTOR=44 XHCIIRQMODE=");append(p,e,xhc.irq_mode==2?"MSI-X":xhc.irq_mode==1?"MSI":"none");
            append(p,e," IFSTATE=");append_dec(p,e,(flags>>9)&1);*p=0;wypisz_log(line);
            pointing_snapshot(xhc.ready?"xhci-ready":"xhci-init-error");
#endif
        }
    } guard{ xhc };
 if(!hpet_dostepny()){wypisz_log("[xHCI-ERR] HPET niedostepny; bezpieczne timeouty niemozliwe.");return false;}

    wypisz_log("[xHCI] PCI controller znaleziony.");log_values("[xHCI] vendor=",xhc.pci.vendor," device=",xhc.pci.device,true);
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const uint32_t cmd_before=pci_odczytaj_dword(xhc.pci.bus,xhc.pci.slot,xhc.pci.function,4)&65535;
#endif
    if(!get_bar(xhc)){wypisz_log("[xHCI-ERR] Niepoprawny Memory BAR.");return false;}
    log_values("[xHCI] BAR=",xhc.bar,xhc.bar64?" (64-bit), size=":" (32-bit), size=",xhc.bar_size,true);
    if(!enable_pci(xhc,false)){wypisz_log("[xHCI-ERR] PCI Memory/Bus Master nieaktywne.");return false;}
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const int64_t command_diag[]={cmd_before,pci_odczytaj_dword(xhc.pci.bus,xhc.pci.slot,xhc.pci.function,4)&65535};
    xhci_physical_diag("[XHCI-PHYSICAL PCI command] before/after",command_diag,2);
#endif
    if(!map_mmio(xhc))return false;
    const uint8_t caplength=*reinterpret_cast<volatile uint8_t*>(xhc.mmio);
    xhc.version=static_cast<uint16_t>((read32(xhc.mmio,0)>>16)&0xFFFFU);
    const uint32_t hcs1=read32(xhc.mmio,0x04),hcs2=read32(xhc.mmio,0x08),hcs3=read32(xhc.mmio,0x0C),hcc=read32(xhc.mmio,0x10);
    (void)hcs3;
    const uint32_t dboff=read32(xhc.mmio,0x14)&~3U,rtsoff=read32(xhc.mmio,0x18)&~0x1FU;
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const int64_t caps[]={xhc.pci.bus,xhc.pci.slot,xhc.pci.function,xhc.pci.vendor,xhc.pci.device,
        pci_odczytaj_dword(xhc.pci.bus,xhc.pci.slot,xhc.pci.function,8)&255U,
        int64_t(xhc.bar),int64_t(xhc.bar_size),xhc.version,caplength,hcs1,hcs2,hcs3,hcc,
        xhc.version>=0x110?read32(xhc.mmio,0x1c):0,dboff,rtsoff};
    xhci_physical_diag("[XHCI-PHYSICAL controller] BDF/vendor/device/revision/BAR/size/version/CAPLENGTH/HCS1/HCS2/HCS3/HCC1/HCC2/DBOFF/RTSOFF",caps,17);
#endif
    xhc.max_slots=hcs1&0xFFU;xhc.max_intrs=(hcs1>>8)&0x7FFU;xhc.max_ports=(hcs1>>24)&0xFFU;
    xhc.scratchpads=xhci_scratchpad_count(hcs2);xhc.ac64=hcc&1U;xhc.port_power_control=hcc&8U;
    xhc.context_size=xhci_context_bytes(hcc);
    if(xhc.version<0x90U||xhc.version>0x120U||caplength<0x20U||(caplength&3U)||dboff<0x20||rtsoff<0x20||xhc.max_slots==0||xhc.max_ports==0||xhc.max_ports>BURSZTYN_XHCI_MAX_PORTS||xhc.scratchpads>MAX_SCRATCHPADS||
       uint64_t(caplength)+PORTSC_BASE+uint64_t(xhc.max_ports)*16U>xhc.bar_size||
       uint64_t(dboff)+(uint64_t(xhc.max_slots)+1)*4U>xhc.bar_size||rtsoff>xhc.bar_size-0x40U||!xhc.max_intrs)return false;
    xhc.operational=xhc.mmio+caplength;xhc.runtime=xhc.mmio+rtsoff;xhc.doorbells=xhc.mmio+dboff;
    log_values("[xHCI] version=",xhc.version,nullptr,0,true);log_values("[xHCI] max_slots=",xhc.max_slots);log_values("[xHCI] max_ports=",xhc.max_ports);log_values("[xHCI] max_intrs=",xhc.max_intrs);
    log_values("[xHCI] context_size=",xhc.context_size);log_values("[xHCI] scratchpads=",xhc.scratchpads);
    if(!ext_capabilities(xhc,hcc))return false;
    {BootTiming::Scope timing(BootTiming::Cnr,0);
    if(!wait_bits(xhc.operational,USBSTS,STS_CNR,0)){physical_completion("pre-halt CNR timeout",0);return false;}}
    {BootTiming::Scope timing(BootTiming::Halt);
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)&~(CMD_RS|(1U<<2)));
    if(!wait_bits(xhc.operational,USBSTS,STS_HCH,STS_HCH)){wypisz_log("[xHCI-ERR] Timeout halt.");return false;}wypisz_log("[xHCI] Halt: OK");}
    {BootTiming::Scope timing(BootTiming::Reset);
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)|CMD_HCRST);
    if(!wait_bits(xhc.operational,USBCMD,CMD_HCRST,0)){wypisz_log("[xHCI-ERR] Timeout HCRST.");return false;}}
    {BootTiming::Scope timing(BootTiming::Cnr,1);
    if(!wait_bits(xhc.operational,USBSTS,STS_CNR,0)){wypisz_log("[xHCI-ERR] Timeout CNR.");return false;}}
    wypisz_log("[xHCI] Reset: OK");
    const uint32_t page_mask=read32(xhc.operational,PAGESIZE);log_values("[xHCI] pagesize mask=",page_mask,nullptr,0,true);
    if((page_mask&1U)==0){wypisz_log("[xHCI-ERR] Kontroler nie wspiera wymaganej wielkosci strony.");return false;}xhc.page_size=4096;
    xhc.enabled_slots=xhc.max_slots<BURSZTYN_XHCI_MAX_SLOTS?xhc.max_slots:BURSZTYN_XHCI_MAX_SLOTS;
    if(!allocate_structures(xhc)){wypisz_log("[xHCI-ERR] Brak pamieci DMA.");return false;}
    const int64_t structures[]={page_mask,xhc.page_size,xhc.context_size,xhc.max_slots,xhc.max_ports,xhc.max_intrs,
        xhc.scratchpads,xhc.ac64,int64_t(xhc.dcbaa.physical_address),int64_t(xhc.command.dma.physical_address),
        int64_t(xhc.event_segment.physical_address),int64_t(xhc.erst.physical_address)};
    xhci_physical_diag("[XHCI-PHYSICAL DMA] PAGESIZE/selected/context/slots/ports/interrupters/scratchpads/AC64/DCBAA/command/event/ERST",structures,12);
    // ERSTBA publication may cause an immediate controller DMA read even
    // while halted. Enable DMA only after reset and valid OS allocations.
    if(!enable_pci(xhc,true)){physical_completion("PCI Bus Master enable failed",0);return false;}
    configure(xhc);wypisz_log("[xHCI] DCBAA: OK");wypisz_log("[xHCI] Command Ring: OK");wypisz_log("[xHCI] Event Ring: OK");
    if(!setup_interrupt(xhc)){wypisz_log("[xHCI-ERR] No safe MSI/MSI-X interrupt route.");return false;}
    if(!enable_pci(xhc,true)){physical_completion("PCI Bus Master enable failed",0);return false;}
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const int64_t final_command[]={pci_odczytaj_dword(xhc.pci.bus,xhc.pci.slot,xhc.pci.function,4)&65535};
    xhci_physical_diag("[XHCI-PHYSICAL PCI command final]",final_command,1);
#endif
    write32(xhc.runtime+0x20U,0,3U); // IE and IP acknowledge
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)|CMD_RS|(1U<<2));
    if(!wait_bits(xhc.operational,USBSTS,STS_HCH,0)){wypisz_log("[xHCI-ERR] Timeout Run.");return false;}
    const uint32_t status=read32(xhc.operational,USBSTS);if((status&(STS_HSE|STS_HCE|STS_CNR))!=0){wypisz_log("[xHCI-ERR] Krytyczny USBSTS po Run.");return false;}
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    const int64_t reset_state[]={read32(xhc.operational,USBCMD),read32(xhc.operational,USBSTS)};
    xhci_physical_diag("[XHCI-PHYSICAL run after halt/HCRST/CNR] USBCMD/USBSTS",reset_state,2);
#endif
    xhc.ready=true;BootTiming::mark("xHCI controller ready");
    for(uint32_t p=1;p<=xhc.max_ports;++p)if(xhc.port_power_control)port_set(xhc,uint8_t(p),1U<<9);
    pointing_counters.ps2_irq_at_activation=pointing_read(pointing_counters.ps2_irq);pointing_counters.xhci_active=true;wypisz_log("[xHCI] Run: OK");xhc.discovery_pending=true;return true;
}

size_t poll_controller_events(Controller& xhc,size_t budget) {
    if(!xhc.ready||budget==0)return 0;
    if(budget>256U)budget=256U;
    XhciTrb* ring=static_cast<XhciTrb*>(xhc.event_segment.virtual_address);size_t done=0;
    while(done<budget){XhciTrb& t=ring[xhc.event_index];const bool cycle=(__atomic_load_n(&t.dword3,__ATOMIC_ACQUIRE)&xhci_trb::CYCLE)!=0;if(cycle!=xhc.event_cycle)break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);const uint32_t type=xhci_trb::type(t.dword3);
        pointing_count(pointing_counters.events);
        if(type==xhci_trb::TYPE_COMMAND_COMPLETION){pointing_count(pointing_counters.commands);const uint64_t ptr=(static_cast<uint64_t>(t.dword1)<<32)|t.dword0;const uint8_t code=static_cast<uint8_t>(t.dword2>>24),slot=static_cast<uint8_t>(t.dword3>>24);
          if(code!=1)pointing_count(pointing_counters.bad_completions);
          const uint64_t begin=xhc.command.dma.physical_address,end=begin+static_cast<uint64_t>(xhc.command.trb_count-1U)*sizeof(XhciTrb);
          if(ptr>=begin&&ptr<end&&((ptr-begin)%sizeof(XhciTrb))==0&&ptr==xhc.wanted_command&&!xhc.command_result.valid){xhci_command_ring_complete(&xhc.command);if(ptr==xhc.wanted_command)xhc.command_result={code,slot,true};}}
        else if(type==xhci_trb::TYPE_PORT_STATUS_CHANGE){const uint8_t port=static_cast<uint8_t>(t.dword0>>24);if(port>=1&&port<=xhc.max_ports){xhc.port_event=true;xhc.ports[port].scan_pending=true;}}
        else if(type==xhci_trb::TYPE_TRANSFER_EVENT){
          pointing_count(pointing_counters.transfers);
          if(pointing_counters.transfers==1)pointing_snapshot("first-transfer-event");
          const uint64_t ptr=(static_cast<uint64_t>(t.dword1)<<32)|t.dword0;const uint8_t code=static_cast<uint8_t>(t.dword2>>24),ep=static_cast<uint8_t>((t.dword3>>16)&31U),slot=static_cast<uint8_t>(t.dword3>>24);const uint32_t residual=t.dword2&0xFFFFFFU;
          if(code!=1&&code!=13)pointing_count(pointing_counters.bad_completions);
          if(!(t.dword3&(1U<<2))&&slot>=1&&slot<=xhc.enabled_slots&&ep>=1&&ep<=31&&xhc.devices[slot].enabled){XhciDevice& d=xhc.devices[slot];
            XhciHid* matched=nullptr;
            for(size_t i=0;i<(d.hid_count?d.hid_count:1U);++i){auto& hi=hid_at(d,i);if(ep==hi.hid_dci&&hi.hid_gotowy){matched=&hi;break;}}
            auto& hi=matched?*matched:static_cast<XhciHid&>(d);
            const bool hid=matched!=nullptr;const uint64_t base=hid?hi.hid_ring.dma.physical_address:d.ep0.dma.physical_address;
            const uint16_t liczba=hid?hi.hid_ring.trb_count:d.ep0.trb_count;const uint64_t end=base+static_cast<uint64_t>(liczba?liczba-1U:0U)*sizeof(XhciTrb);
            if((hid||ep==1)&&ptr>=base&&ptr<end&&((ptr-base)%sizeof(XhciTrb))==0){
              if(hid&&hi.hid_transfer_oczekiwany&&ptr==hi.hid_transfer_pointer&&!hi.hid_result.valid)hi.hid_result={code,slot,ep,residual,ptr,true};
              else if(ep==1&&xhc.wanted_transfer&&slot==xhc.control_slot){
                if(ptr==xhc.wanted_data&&code==13)xhc.control_residual=residual;
                else if(ptr==xhc.wanted_transfer)xhc.transfer_result={code,slot,ep,xhc.control_residual?xhc.control_residual:residual,ptr,true};
                else if((ptr==xhc.wanted_data||ptr==xhc.wanted_setup)&&code!=1)xhc.transfer_result={code,slot,ep,residual,ptr,true};
              }}}}
        ++xhc.event_index;if(xhc.event_index==EVENT_TRBS){xhc.event_index=0;xhc.event_cycle=!xhc.event_cycle;}++done;}
    if(done){const uint64_t erdp=xhc.event_segment.physical_address+static_cast<uint64_t>(xhc.event_index)*sizeof(XhciTrb);write64(xhc.runtime+0x20U,0x18,erdp|(1U<<3));}
    return done;
}

void service_controller(Controller& xhc){
    if(!xhc.ready || __atomic_exchange_n(&xhc.servicing,true,__ATOMIC_ACQUIRE))return;
    struct ServiceGuard {Controller& xhc;~ServiceGuard(){__atomic_store_n(&xhc.servicing,false,__ATOMIC_RELEASE);BootTiming::dump();}} service_guard{ xhc };
    const uint32_t status=read32(xhc.operational,USBSTS);
    if(status&(STS_HSE|STS_HCE|STS_HCH)){
        log_controller(xhc,"fatal controller status");log_values("[xHCI-ERR] USBSTS=",status,nullptr,0,true);
        diag_xhci(xhc,DIAG_CRITICAL,"Kontroler zatrzymany; błąd USBSTS");
        fail_controller(xhc);return;
    }
    ++xhc.usb_service_calls;
    // A fixed event batch, one root-port observation and one enumeration step.
    // Polling already-completed ring entries never waits for future events.
    const size_t events=poll_controller_events(xhc,32);
    xhc.usb_events_processed+=events;
    if(events>xhc.max_events_per_service)xhc.max_events_per_service=events;
    if(events==32)__atomic_store_n(&xhc.irq_pending,1U,__ATOMIC_RELEASE);
    else {__atomic_store_n(&xhc.irq_pending,0U,__ATOMIC_RELEASE);write32(xhc.runtime+0x20U,0,2U);}
    // Mapping alone cannot release this barrier. The splash renderer sets it
    // only after a successful copy to the selected visible graphics backend.
    if(BootTiming::enumeration_publication_ready()){
        if(!xhc.enumeration_started){xhc.enumeration_started=true;BootTiming::mark("xHCI deferred enumeration begin");}
        inspect_one_port(xhc);enumeration_step(xhc);
    }
    // Also bound active HID work independently of cold enumeration.
    for(unsigned budget=xhc.enabled_slots,reports=0;budget && reports<8;--budget){
        if(xhc.hid_service_slot>xhc.enabled_slots)xhc.hid_service_slot=1;
        auto& d=xhc.devices[xhc.hid_service_slot++];
        const size_t count=d.hid_count?d.hid_count:1U;
        for(size_t visited=0;visited<count&&reports<8;++visited){
            if(d.hid_service_interface>=count)d.hid_service_interface=0;
            auto& hi=hid_at(d,d.hid_service_interface++);
            if(hi.hid_gotowy&&hi.hid_result.valid){xhci_obsluz_hid(xhc,d,&hi);++reports;}}

    }
}

} // namespace

bool xhci_dostepny(){for(size_t i=0;i<controller_count;++i)if(controllers[i].ready)return true;return false;}
bool xhci_inicjalizuj_wszystkie() {
    if(controllers_scanned)return xhci_dostepny();
    controllers_scanned=true;
    for(uint16_t bus=0;bus<256;++bus)for(uint8_t device=0;device<32;++device)for(uint8_t function=0;function<8;++function){
        const uint16_t vendor=pci_vendor_id(uint8_t(bus),device,function);if(vendor==PCI_VENDOR_BRAK)continue;
        const uint32_t info=pci_odczytaj_dword(uint8_t(bus),device,function,PCI_OFFSET_CLASS_INFO);
        if((info&0xffffff00U)!=0x0c033000U)continue;
        PciDevice pci{uint8_t(bus),device,function,vendor,uint16_t(pci_odczytaj_dword(uint8_t(bus),device,function,0)>>16)};
        if(controller_count==MAX_CONTROLLERS){
            log_values("[xHCI-ERR] controller capacity exceeded BDF=",(uint32_t(bus)<<16)|(uint32_t(device)<<8)|function,nullptr,0,true);continue;}
        auto& c=controllers[controller_count];c.index=uint8_t(controller_count++);c.pci=pci;
        log_controller(c,"detected");diag_xhci(c,DIAG_INACTIVE,"Kontroler PCI wykryty; inicjalizacja");
        const bool ok=initialize_controller(c);log_controller(c,ok?"ready":"initialization failed");
        diag_xhci(c,ok?DIAG_OK:c.diag_timeout?DIAG_TIMEOUT:DIAG_INIT_FAILED,
            ok?"Kontroler gotowy":"Inicjalizacja kontrolera nie powiodła się; sprawdź log USB");
    }
    return xhci_dostepny();
}
bool xhci_inicjalizuj_pierwszy(){return xhci_inicjalizuj_wszystkie();}
size_t xhci_poll_events(size_t budget){
    size_t done=0;for(size_t i=0;i<controller_count&&done<budget;++i)done+=poll_controller_events(controllers[i],budget-done);return done;
}
void usb_obsluz(){ohci_service();for(size_t i=0;i<controller_count;++i)service_controller(controllers[i]);}

#ifdef BURSZTYN_XHCI_HOST_TEST
namespace {
[[maybe_unused]] bool addressable(const DmaBuffer& b){return addressable(xhc,b);}
[[maybe_unused]] bool allocate_dma(size_t size,size_t alignment,DmaBuffer* b){return allocate_dma(xhc,size,alignment,b);}
[[maybe_unused]] int command_ready(uint8_t expected_slot=0){return command_ready(xhc,expected_slot);}
[[maybe_unused]] uint32_t* context(DmaBuffer& b,uint8_t index){return context(xhc,b,index);}
[[maybe_unused]] bool create_ring(XhciProducerRing* ring,uint16_t count){return create_ring(xhc,ring,count);}
[[maybe_unused]] bool disable_slot(XhciDevice& d){return disable_slot(xhc,d);}
[[maybe_unused]] bool enum_buffer(size_t bytes){return enum_buffer(xhc,bytes);}
[[maybe_unused]] bool enum_control(UsbSetupPacket setup,EnumStep next){return enum_control(xhc,setup,next);}
[[maybe_unused]] void enumeration_fail(){return enumeration_fail(xhc);}
[[maybe_unused]] void enumeration_idle(bool safe_to_free){return enumeration_idle(xhc,safe_to_free);}
[[maybe_unused]] void enumeration_step(){return enumeration_step(xhc);}
[[maybe_unused]] CommandResult execute_command(XhciTrb trb){return execute_command(xhc,trb);}
[[maybe_unused]] void fail_controller(){return fail_controller(xhc);}
[[maybe_unused]] void free_device(XhciDevice& d){return free_device(xhc,d);}
[[maybe_unused]] void inspect_one_port(){return inspect_one_port(xhc);}
[[maybe_unused]] void port_clear_changes(uint8_t port,uint32_t changes){return port_clear_changes(xhc,port,changes);}
[[maybe_unused]] void port_set(uint8_t port,uint32_t bits){return port_set(xhc,port,bits);}
[[maybe_unused]] bool prepare_device(XhciDevice& d,uint8_t slot,uint8_t port,SpeedInfo speed){return prepare_device(xhc,d,slot,port,speed);}
[[maybe_unused]] bool prepare_hid_endpoint(XhciDevice& d,const hid_wynik_konfiguracji& h,XhciTrb& command,XhciHid* interface=nullptr){return prepare_hid_endpoint(xhc,d,h,command,interface);}
[[maybe_unused]] bool reset_port(uint8_t port){return reset_port(xhc,port);}
[[maybe_unused]] CommandResult simple_slot_command(uint32_t type,uint8_t slot){return simple_slot_command(xhc,type,slot);}
[[maybe_unused]] bool submit_command(XhciTrb trb,EnumStep wait){return submit_command(xhc,trb,wait);}
[[maybe_unused]] bool submit_control(XhciDevice& d,const UsbSetupPacket& setup,DmaBuffer* data,
                          uint16_t length){return submit_control(xhc,d,setup,data,length);}
[[maybe_unused]] bool usb_control_transfer(XhciDevice& d,const UsbSetupPacket& setup,DmaBuffer* data,
                          uint16_t length,uint32_t* actual,uint8_t* completion=nullptr){return usb_control_transfer(xhc,d,setup,data,length,actual,completion);}
[[maybe_unused]] uint32_t* xhci_context_ptr(DmaBuffer& kontekst,uint8_t indeks){return xhci_context_ptr(xhc,kontekst,indeks);}
[[maybe_unused]] void xhci_obsluz_hid(XhciDevice& d,XhciHid* interface=nullptr){return xhci_obsluz_hid(xhc,d,interface);}
[[maybe_unused]] SpeedInfo xhci_pobierz_predkosc_portu(uint8_t port){return xhci_pobierz_predkosc_portu(xhc,port);}
[[maybe_unused]] bool xhci_skonfiguruj_endpoint(XhciDevice& d,const hid_wynik_konfiguracji& h){return xhci_skonfiguruj_endpoint(xhc,d,h);}
[[maybe_unused]] bool xhci_waliduj_configure_endpoint(XhciDevice& d,const hid_wynik_konfiguracji& h,uint32_t* kontrola,uint32_t* slot,uint32_t* ep,XhciHid* interface=nullptr){return xhci_waliduj_configure_endpoint(xhc,d,h,kontrola,slot,ep,interface);}
[[maybe_unused]] bool xhci_zakolejkuj_transfer_interrupt_in(XhciDevice& d,XhciHid* interface=nullptr){return xhci_zakolejkuj_transfer_interrupt_in(xhc,d,interface);}
}
#endif
