#include "sterowniki/usb/xhci.h"
#include "sterowniki/usb/xhci_ring.h"
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "pamiec.h"
#include "pci.h"

void wypisz_log(const char* tekst);

namespace {
constexpr uint64_t MMIO_VIRTUAL_BASE = UINT64_C(0x142000000);
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
constexpr uint32_t STS_HCH = 1U, STS_HSE = 1U << 2, STS_CNR = 1U << 11;
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
struct PortInfo { Protocol* protocol; };

struct CommandResult { uint8_t code, slot; bool valid; };
struct TransferResult { uint8_t code, slot, endpoint; uint32_t residual;
    uint64_t pointer; bool valid; };
struct XhciDevice {
    uint8_t slot_id, root_port, speed_id; UsbSpeed logical_speed;
    Protocol* protocol; DmaBuffer output_context{}, input_context{};
    XhciProducerRing ep0{}; uint16_t ep0_mps{}; bool enabled{}, addressed{};
    XhciProducerRing hid_ring{}; DmaBuffer hid_raport{}; uint8_t hid_dci{};
    uint8_t hid_interfejs{}, hid_endpoint{}; uint16_t hid_mps{}; uint8_t hid_interwal{};
    uint8_t konfiguracja{}; bool hid_gotowy{}, hid_transfer_oczekiwany{};
    bool pierwszy_raport_zalogowany{};
    uint8_t poprzedni_raport[8]{};
};

struct PciDevice { uint8_t bus, slot, function; uint16_t vendor, device; };
struct ErstEntry { uint64_t address; uint32_t size; uint32_t reserved; };
static_assert(sizeof(ErstEntry) == 16, "ERST entry");

struct Controller {
    PciDevice pci{};
    uint64_t bar{};
    uint64_t bar_size{};
    bool bar64{};
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
    uint64_t wanted_transfer{}; TransferResult transfer_result{};
    bool ready{};
} xhc;

struct UsbSetupPacket { uint8_t bmRequestType, bRequest; uint16_t wValue,
    wIndex, wLength; } __attribute__((packed));
static_assert(sizeof(UsbSetupPacket)==8,"USB setup packet");
struct UsbDeviceDescriptor { uint8_t bLength,bDescriptorType; uint16_t bcdUSB;
    uint8_t bDeviceClass,bDeviceSubClass,bDeviceProtocol,bMaxPacketSize0;
    uint16_t idVendor,idProduct,bcdDevice; uint8_t iManufacturer,iProduct,
    iSerialNumber,bNumConfigurations; } __attribute__((packed));
static_assert(sizeof(UsbDeviceDescriptor)==18,"USB device descriptor");
struct XhciDevice;
bool usb_control_transfer(XhciDevice&,const UsbSetupPacket&,DmaBuffer*,uint16_t,uint32_t*);
uint32_t* context(DmaBuffer&,uint8_t);
uint8_t xhci_oblicz_dci(uint8_t,bool);

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
const char* speed_name(UsbSpeed s) { switch(s){case USB_LOW_SPEED:return "Low";
case USB_FULL_SPEED:return "Full";case USB_HIGH_SPEED:return "High";
case USB_SUPER_SPEED:return "Super";case USB_SUPER_SPEED_PLUS:return "SuperPlus";
default:return "Unknown";} }
void log_port_value(uint32_t port,const char* label,uint64_t value) { char s[128],*p=s,*e=s+sizeof(s);
 append(p,e,"[xHCI] Port ");append_dec(p,e,port);append(p,e,label);append_dec(p,e,value);*p=0;wypisz_log(s); }
void log_port_text(uint32_t port,const char* label,const char* value) { char s[128],*p=s,*e=s+sizeof(s);
 append(p,e,"[xHCI] Port ");append_dec(p,e,port);append(p,e,label);append(p,e,value);*p=0;wypisz_log(s); }
inline uint16_t le16(const void* p){const uint8_t* b=static_cast<const uint8_t*>(p);return static_cast<uint16_t>(b[0]|(b[1]<<8));}

inline uint32_t read32(const volatile uint8_t* base, uint32_t off) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    uint32_t v=*reinterpret_cast<const volatile uint32_t*>(base+off);
    __atomic_thread_fence(__ATOMIC_ACQUIRE); return v;
}
inline void write32(volatile uint8_t* base,uint32_t off,uint32_t v) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *reinterpret_cast<volatile uint32_t*>(base+off)=v;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
inline uint64_t read64(const volatile uint8_t* base,uint32_t off) {
    const uint32_t lo=read32(base,off), hi=read32(base,off+4U); return (static_cast<uint64_t>(hi)<<32)|lo;
}
inline void write64(volatile uint8_t* base,uint32_t off,uint64_t v) {
    write32(base,off,static_cast<uint32_t>(v)); write32(base,off+4U,static_cast<uint32_t>(v>>32));
}

bool wait_bits(const volatile uint8_t* base,uint32_t off,uint32_t mask,uint32_t expected) {
    if (!hpet_dostepny()) return false;
    const uint64_t start=czas_monotoniczny_ns();
    if (start > UINT64_MAX-WAIT_NS) return false;
    const uint64_t deadline=start+WAIT_NS;
    for (;;) {
        if ((read32(base,off)&mask)==expected) return true;
        if (czas_monotoniczny_ns()>=deadline) return false;
        asm volatile("pause" ::: "memory");
    }
}

bool parse_protocol(Controller& c,uint32_t off,uint32_t next) {
    if(c.protocol_count>=MAX_PROTOCOLS||off>c.bar_size-16U)return false;
    Protocol& p=c.protocols[c.protocol_count];const uint32_t d0=read32(c.mmio,off);
    const uint32_t d1=read32(c.mmio,off+4U),d2=read32(c.mmio,off+8U),
                   d3=read32(c.mmio,off+12U);
    p.minor=static_cast<uint8_t>(d0>>16);p.major=static_cast<uint8_t>(d0>>24);p.name=d1;
    p.port_offset=static_cast<uint8_t>(d2);p.port_count=static_cast<uint8_t>(d2>>8);
    p.psi_count=static_cast<uint8_t>((d3>>28)&15U);p.slot_type=static_cast<uint8_t>(d3&31U);
    p.usb2=p.major==2U;p.usb3=p.major==3U;
    if(p.port_offset==0||p.port_count==0||p.port_offset>c.max_ports||
       p.port_count>c.max_ports-p.port_offset+1U||p.psi_count>MAX_PSI)return false;
    const uint32_t cap_end=next?off+next*4U:static_cast<uint32_t>(c.bar_size);
    if(cap_end<off+16U||static_cast<uint64_t>(off)+16U+4U*p.psi_count>cap_end)return false;
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

bool find_controller(PciDevice* out) {
    for(uint16_t b=0;b<256;++b) for(uint8_t s=0;s<32;++s) for(uint8_t f=0;f<8;++f) {
        const uint16_t vendor=pci_vendor_id(static_cast<uint8_t>(b),s,f); if(vendor==PCI_VENDOR_BRAK) continue;
        const uint32_t c=pci_odczytaj_dword(static_cast<uint8_t>(b),s,f,PCI_OFFSET_CLASS_INFO);
        if(((c>>24)&0xFFU)==0x0CU&&((c>>16)&0xFFU)==0x03U&&((c>>8)&0xFFU)==0x30U) {
            out->bus=static_cast<uint8_t>(b);out->slot=s;out->function=f;out->vendor=vendor;
            out->device=static_cast<uint16_t>((pci_odczytaj_dword(out->bus,s,f,0)>>16)&0xFFFFU);
            return true;
        }
    } return false;
}

bool get_bar(Controller& c) {
    const uint8_t o=PCI_OFFSET_BAR0;
    const uint32_t low=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,o);
    if((low&1U)!=0) return false;
    const uint32_t type=(low>>1)&3U; if(type!=0U&&type!=2U) return false;
    c.bar64=(type==2U); uint32_t high=0;
    if(c.bar64) high=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,o+4U);
    c.bar=(static_cast<uint64_t>(high)<<32)|(low&0xFFFFFFF0U);
    if(c.bar==0||(c.bar&0xFU)!=0) return false;

    const uint32_t cs=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS,cs&~2U);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,o,UINT32_MAX);
    uint32_t masklo=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,o);
    uint32_t maskhi=UINT32_MAX;
    if(c.bar64){pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,o+4U,UINT32_MAX);maskhi=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,o+4U);}
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,o,low);
    if(c.bar64)pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,o+4U,high);
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS,cs);
    const uint64_t mask=(static_cast<uint64_t>(maskhi)<<32)|(masklo&0xFFFFFFF0U);
    c.bar_size=(~mask)+1U;
    return c.bar_size>=PAGE&&c.bar_size<=MAX_MMIO_SIZE&&
           (c.bar&(c.bar_size-1U))==0&&c.bar<=UINT64_MAX-c.bar_size&&
           c.bar+c.bar_size-1U<=MAX_PHYSICAL_ADDRESS;
}

bool enable_pci(Controller& c) {
    uint32_t v=pci_odczytaj_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    v|=PCI_COMMAND_MEMORY_SPACE|PCI_COMMAND_BUS_MASTER;
    pci_zapisz_dword(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS,v);
    const uint16_t check=pci_odczytaj_word(c.pci.bus,c.pci.slot,c.pci.function,PCI_OFFSET_COMMAND_STATUS);
    return (check&(PCI_COMMAND_MEMORY_SPACE|PCI_COMMAND_BUS_MASTER))==(PCI_COMMAND_MEMORY_SPACE|PCI_COMMAND_BUS_MASTER);
}

bool map_mmio(Controller& c) {
    const uint64_t first=c.bar&~(PAGE-1U), delta=c.bar-first;
    if(delta>UINT64_MAX-c.bar_size) return false;
    const uint64_t bytes=delta+c.bar_size;
    if(bytes>UINT64_MAX-(PAGE-1U))return false;
    const uint64_t pages=(bytes+PAGE-1U)/PAGE;
    for(uint64_t i=0;i<pages;++i) ZmapujStrone(reinterpret_cast<void*>(MMIO_VIRTUAL_BASE+i*PAGE),
        reinterpret_cast<void*>(first+i*PAGE),VMM_FLAGA_PRESENT|VMM_FLAGA_ZAPIS|VMM_FLAGA_PWT|VMM_FLAGA_PCD);
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax","memory");
    c.mmio=reinterpret_cast<volatile uint8_t*>(MMIO_VIRTUAL_BASE+delta); return true;
}

bool ext_capabilities(Controller& c,uint32_t hcc) {
    uint32_t off=((hcc>>16)&0xFFFFU)*4U; if(off==0){wypisz_log("[xHCI] Firmware handoff: not required");return true;}
    bool legacy=false;
    for(uint32_t count=0;count<MAX_EXT_CAPS;++count) {
        if((off&3U)!=0||off>c.bar_size-4U) return false;
        const uint32_t v=read32(c.mmio,off), id=v&0xFFU, next=(v>>8)&0xFFU;
        if(id==CAP_LEGACY_SUPPORT) {
            legacy=true; const uint32_t owned=read32(c.mmio,off);
            if((owned&(1U<<16))!=0) {
                write32(c.mmio,off,owned|(1U<<24));
                if(!wait_bits(c.mmio,off,1U<<16,0)){wypisz_log("[xHCI-ERR] Firmware nie przekazalo kontrolera.");return false;}
            } else write32(c.mmio,off,owned|(1U<<24));
            if(off>c.bar_size-8U)return false;
            const uint32_t ctl=read32(c.mmio,off+4U);
            const uint32_t enables=(1U<<0)|(1U<<4)|(1U<<13)|(1U<<14)|(1U<<15);
            write32(c.mmio,off+4U,(ctl&0xFFFFU)&~enables);
        }
        if(id==CAP_SUPPORTED_PROTOCOL&&!parse_protocol(c,off,next))return false;
        if(next==0)break;
        const uint32_t step=next*4U; if(step==0||off>UINT32_MAX-step)return false;
        const uint32_t newer=off+step; if(newer==off)return false; off=newer;
        if(count+1U==MAX_EXT_CAPS)return false;
    }
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
    if(!dma_allocate((static_cast<size_t>(c.enabled_slots)+1U)*sizeof(uint64_t),64U,&c.dcbaa))return false;
    uint64_t* dcbaa=static_cast<uint64_t*>(c.dcbaa.virtual_address);
    if(c.scratchpads!=0) {
        if(!dma_allocate(static_cast<size_t>(c.scratchpads)*sizeof(uint64_t),64U,&c.scratchpad_array)){release_structures(c);return false;}
        uint64_t* array=static_cast<uint64_t*>(c.scratchpad_array.virtual_address);
        for(uint32_t i=0;i<c.scratchpads;++i){if(!dma_allocate(c.page_size,c.page_size,&c.scratchpad_buffers[i])){release_structures(c);return false;}array[i]=c.scratchpad_buffers[i].physical_address;}
        dcbaa[0]=c.scratchpad_array.physical_address;
    }
    if(!xhci_command_ring_create(&c.command,COMMAND_TRBS)){release_structures(c);return false;}
    if(!dma_allocate(static_cast<size_t>(EVENT_TRBS)*sizeof(XhciTrb),64U,&c.event_segment)){release_structures(c);return false;}
    if(!dma_allocate(sizeof(ErstEntry),64U,&c.erst)){release_structures(c);return false;}
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

uint32_t port_write_base(uint32_t old) {
    /* Zachowaj tylko rzeczywiste pola RW; RW1C, PR/WPR, PED i LWS sa jawne. */
    return old&((1U<<9)|(3U<<14)|(7U<<25));
}
void port_set(uint8_t port,uint32_t bits){const uint32_t o=PORTSC_BASE+(port-1U)*16U;
 const uint32_t old=read32(xhc.operational,o);write32(xhc.operational,o,port_write_base(old)|bits);}
void port_clear_changes(uint8_t port,uint32_t changes){port_set(port,changes&PORT_RW1C);}

SpeedInfo xhci_pobierz_predkosc_portu(uint8_t port) {
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

bool before_deadline(uint64_t deadline){return czas_monotoniczny_ns()<deadline;}
uint64_t deadline(){const uint64_t n=czas_monotoniczny_ns();return n>UINT64_MAX-WAIT_NS?UINT64_MAX:n+WAIT_NS;}

bool reset_port(uint8_t port) {
    Protocol* p=xhc.ports[port].protocol;if(!p||(read32(xhc.operational,PORTSC_BASE+(port-1U)*16U)&PORT_CCS)==0)return false;
    log_port_text(port," reset...","");xhc.pending_port=port;xhc.port_event=false;
    const uint32_t reset_change=p->usb3?(1U<<19):PORT_PRC;
    port_clear_changes(port,reset_change);port_set(port,p->usb3?PORT_WPR:PORT_PR);
    const uint64_t end=deadline();uint32_t v=0;
    do{xhci_poll_events(32);v=read32(xhc.operational,PORTSC_BASE+(port-1U)*16U);
       if((v&PORT_CCS)==0)return false;
       if((v&(PORT_PR|PORT_WPR))==0&&(v&reset_change)!=0)break;
       asm volatile("pause":::"memory");}while(before_deadline(end));
    xhc.pending_port=0;if((v&(PORT_PR|PORT_WPR))!=0||(v&reset_change)==0)return false;
    port_clear_changes(port,reset_change);log_port_text(port," reset ","OK.");
    SpeedInfo s=xhci_pobierz_predkosc_portu(port);log_port_text(port," speed=",speed_name(s.logical));log_port_value(port," raw=",s.raw);
    log_port_value(port," PED=",(v&PORT_PED)?1:0);log_port_value(port," PLS=",(v>>PORT_PLS_SHIFT)&PORT_PLS_MASK);
    return (v&PORT_PED)!=0&&s.logical!=USB_SPEED_UNKNOWN;
}

CommandResult execute_command(XhciTrb trb) {
    uint64_t pa=0;CommandResult bad{};if(!xhci_command_ring_enqueue(&xhc.command,trb,&pa))return bad;
    xhc.wanted_command=pa;xhc.command_result={};__atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,0,0);
    const uint64_t end=deadline();while(before_deadline(end)){xhci_poll_events(64);if(xhc.command_result.valid){xhc.wanted_command=0;return xhc.command_result;}asm volatile("pause":::"memory");}
    xhc.wanted_command=0;return bad;
}
CommandResult simple_slot_command(uint32_t type,uint8_t slot){XhciTrb t{};t.dword3=xhci_trb::type_field(type)|(static_cast<uint32_t>(slot)<<24);return execute_command(t);}
bool disable_slot(XhciDevice& d){if(!d.enabled)return true;CommandResult r=simple_slot_command(xhci_trb::TYPE_DISABLE_SLOT,d.slot_id);if(!r.valid||r.code!=1)return false;d.enabled=false;return true;}
void free_device(XhciDevice& d){xhci_command_ring_destroy(&d.hid_ring);dma_release(&d.hid_raport);xhci_command_ring_destroy(&d.ep0);dma_release(&d.input_context);dma_release(&d.output_context);d={};}

struct hid_wynik_konfiguracji { uint8_t konfiguracja, interfejs, alternatywa, endpoint;
    uint16_t hid_bcd, raport_dlugosc, maksymalny_rozmiar; uint8_t interwal, dci; bool znaleziono; };
uint32_t* xhci_context_ptr(DmaBuffer& kontekst,uint8_t indeks){
    return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(kontekst.virtual_address)+static_cast<size_t>(indeks+1U)*xhc.context_size);
}
void xhci_log_cfg(const char* pole,uint64_t wartosc){char s[128],*p=s,*e=s+sizeof(s);append(p,e,"[xHCI-CFG] ");append(p,e,pole);append(p,e,"=");append_hex(p,e,wartosc);*p=0;wypisz_log(s);}
bool xhci_waliduj_configure_endpoint(XhciDevice& d,const hid_wynik_konfiguracji& h,uint32_t* kontrola,uint32_t* slot,uint32_t* ep){
    const uint32_t add=kontrola[1],drop=kontrola[0],entries=(slot[0]>>27)&31U,typ=(ep[1]>>3)&7U,stan=ep[0]&7U;
    const uint32_t interval=(ep[0]>>16)&255U,mps=(ep[1]>>16)&0x7FFU,burst=(ep[1]>>8)&255U,esit=ep[4]&0xFFFFU,pstreams=(ep[0]>>10)&31U;
    const uint64_t dequeue=(static_cast<uint64_t>(ep[3])<<32)|(ep[2]&~15U);bool ok=true;
    const uint64_t oczekiwany[]={3,9,0,3,0,7,6,8,0,8,0,1};const uint64_t faktyczny[]={d.hid_dci,add,drop,entries,stan,typ,interval,mps,burst,esit,pstreams,ep[2]&1U};
    const char* nazwy[]={"dci","add_flags","drop_flags","context_entries","ep_state","ep_type","interval","max_packet","max_burst","max_esit","max_pstreams","dcs"};
    for(size_t i=0;i<sizeof(faktyczny)/sizeof(faktyczny[0]);++i)if(faktyczny[i]!=oczekiwany[i]){char b[64],*p=b,*e=b+sizeof(b);append(p,e,"[xHCI-VALIDATE] FAIL pole=");append(p,e,nazwy[i]);append(p,e," value=");append_hex(p,e,faktyczny[i]);*p=0;wypisz_log(b);ok=false;}
    if(h.endpoint!=0x81||h.maksymalny_rozmiar!=8||h.interwal!=7||xhci_oblicz_dci(1,true)!=3){wypisz_log("[xHCI-VALIDATE] FAIL pole=descriptor_endpoint");ok=false;}
    if((reinterpret_cast<uintptr_t>(d.input_context.virtual_address)&63U)!=0||dequeue==0||(ep[2]&15U)!=1){wypisz_log("[xHCI-VALIDATE] FAIL pole=alignment_or_dequeue");ok=false;}
    return ok;
}
bool usb_sprawdz_deskryptor_konfiguracji(const uint8_t* dane,size_t dlugosc,hid_wynik_konfiguracji* wynik){
    if(!dane||!wynik||dlugosc<9||dane[0]<9||dane[1]!=2||le16(dane+2)<9)return false;
    *wynik={};wynik->konfiguracja=dane[5];const size_t calkowita=le16(dane+2);if(calkowita>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI||calkowita>dlugosc)return false;
    uint8_t interfejs=0,alternatywa=0;bool pasujacy=false,hid=false;
    for(size_t o=0;o<calkowita;){if(o+2>calkowita)return false;const uint8_t n=dane[o],t=dane[o+1];if(n<2||o+n>calkowita)return false;
      if(t==4&&n>=9){interfejs=dane[o+2];alternatywa=dane[o+3];pasujacy=dane[o+5]==3&&dane[o+6]==1&&dane[o+7]==1;hid=false;
        if(pasujacy&&alternatywa==0){wynik->interfejs=interfejs;wynik->alternatywa=alternatywa;wypisz_log("[USB] Interface HID Boot Keyboard znaleziona.");}}
      else if(t==0x21&&n>=9&&pasujacy&&alternatywa==0){wynik->hid_bcd=le16(dane+o+2);wynik->raport_dlugosc=le16(dane+o+7);hid=true;}
      else if(t==5&&n>=7&&pasujacy&&alternatywa==0&&hid&&dane[o+2]&0x80U&&((dane[o+3]&3U)==3U)&&dane[o+2]!=0x80U){
        wynik->endpoint=dane[o+2];wynik->maksymalny_rozmiar=static_cast<uint16_t>(le16(dane+o+4)&0x7FFU);wynik->interwal=dane[o+6];wynik->dci=xhci_oblicz_dci(static_cast<uint8_t>(dane[o+2]&0x0FU),true);wynik->znaleziono=true;
        wypisz_log("[USB-HID] Interrupt IN endpoint znaleziony.");}
      o+=n;}
    return wynik->znaleziono&&wynik->maksymalny_rozmiar!=0&&wynik->maksymalny_rozmiar<=1024;
}
bool usb_pobierz_konfiguracje(XhciDevice& d,DmaBuffer& dane,hid_wynik_konfiguracji* wynik){
    DmaBuffer naglowek{};if(!dma_allocate(9,64,&naglowek))return false;uint32_t got=0;UsbSetupPacket s{0x80,6,static_cast<uint16_t>(2U<<8),0,9};
    bool ok=usb_control_transfer(d,s,&naglowek,9,&got)&&got>=9&&naglowek.virtual_address;
    if(!ok){dma_release(&naglowek);return false;}const uint8_t* h=static_cast<const uint8_t*>(naglowek.virtual_address);const uint16_t calkowita=le16(h+2);
    if(h[0]<9||h[1]!=2||calkowita<9||calkowita>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI){dma_release(&naglowek);return false;}
    if(!dma_allocate(calkowita,64,&dane)){dma_release(&naglowek);return false;}s.wLength=calkowita;ok=usb_control_transfer(d,s,&dane,calkowita,&got)&&got>=9;
    dma_release(&naglowek);if(!ok||!usb_sprawdz_deskryptor_konfiguracji(static_cast<const uint8_t*>(dane.virtual_address),got,wynik)){dma_release(&dane);return false;}
    log_values("[USB] Configuration total_length=",calkowita);return true;
}
uint8_t xhci_oblicz_dci(uint8_t numer_endpointu,bool kierunek){if(numer_endpointu==0||numer_endpointu>15)return 0;return static_cast<uint8_t>(numer_endpointu*2U+(kierunek?1U:0U));}
uint8_t xhci_oblicz_interwal(UsbSpeed predkosc,uint8_t b_interval){
    if(b_interval==0||b_interval>16)return 0;
    if(predkosc==USB_HIGH_SPEED||predkosc==USB_SUPER_SPEED||predkosc==USB_SUPER_SPEED_PLUS)return static_cast<uint8_t>(b_interval-1U);
    const uint16_t wartosc=static_cast<uint16_t>(b_interval)+3U;return wartosc>15?15:static_cast<uint8_t>(wartosc);
}
bool xhci_skonfiguruj_endpoint(XhciDevice& d,const hid_wynik_konfiguracji& h){
    if(!h.znaleziono||h.dci<2||h.dci>31||h.maksymalny_rozmiar==0)return false;
    if(!xhci_command_ring_create(&d.hid_ring,HID_TRBS)||!dma_allocate(h.maksymalny_rozmiar,64,&d.hid_raport))return false;
    d.hid_dci=h.dci;d.hid_endpoint=h.endpoint;d.hid_mps=h.maksymalny_rozmiar;d.hid_interwal=xhci_oblicz_interwal(d.logical_speed,h.interwal);d.hid_interfejs=h.interfejs;
    if(!d.hid_interwal){xhci_command_ring_destroy(&d.hid_ring);dma_release(&d.hid_raport);return false;}
    uint32_t* icc=context(d.input_context,0);icc[0]=0;icc[1]=(1U<<0)|(1U<<d.hid_dci);uint32_t* sc=context(d.input_context,1);sc[0]=(sc[0]&~(31U<<27))|(3U<<27);
    uint32_t* ep=xhci_context_ptr(d.input_context,d.hid_dci);ep[0]=static_cast<uint32_t>(d.hid_interwal)<<16;ep[1]=(static_cast<uint32_t>(d.hid_mps)<<16)|(3U<<1)|(7U<<3);ep[2]=static_cast<uint32_t>(d.hid_ring.dma.physical_address)|1U;ep[3]=static_cast<uint32_t>(d.hid_ring.dma.physical_address>>32);ep[4]=8;
    xhci_log_cfg("slot",d.slot_id);xhci_log_cfg("speed",d.speed_id);xhci_log_cfg("ep_address",h.endpoint);xhci_log_cfg("dci",d.hid_dci);xhci_log_cfg("drop_flags",icc[0]);xhci_log_cfg("add_flags",icc[1]);xhci_log_cfg("context_entries",(sc[0]>>27)&31U);
    xhci_log_cfg("ep_state",ep[0]&7U);xhci_log_cfg("ep_type",(ep[1]>>3)&7U);xhci_log_cfg("descriptor_bInterval",h.interwal);xhci_log_cfg("interval",(ep[0]>>16)&255U);xhci_log_cfg("max_packet",(ep[1]>>16)&0x7FFU);xhci_log_cfg("max_burst",(ep[1]>>8)&255U);xhci_log_cfg("max_esit",ep[4]&0xFFFFU);xhci_log_cfg("cerr",(ep[1]>>1)&3U);xhci_log_cfg("max_pstreams",(ep[0]>>10)&31U);
    xhci_log_cfg("dequeue_phys",(static_cast<uint64_t>(ep[3])<<32)|(ep[2]&~15U));xhci_log_cfg("dcs",ep[2]&1U);xhci_log_cfg("input_context_phys",d.input_context.physical_address);xhci_log_cfg("input_context_alignment",reinterpret_cast<uintptr_t>(d.input_context.virtual_address)&63U);xhci_log_cfg("hid_ring_phys",d.hid_ring.dma.physical_address);xhci_log_cfg("hid_ring_alignment",d.hid_ring.dma.physical_address&15U);
    xhci_log_cfg("ep1_in_context_phys",d.input_context.physical_address+static_cast<uint64_t>(4U)*xhc.context_size);
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"input_control_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,context(d.input_context,0)[i]);}
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"input_slot_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,sc[i]);}
    for(uint8_t i=0;i<8;++i){char n[40],*p=n,*e=n+sizeof(n);append(p,e,"ep1_in_dword");append_dec(p,e,i);*p=0;xhci_log_cfg(n,ep[i]);}
    if(!xhci_waliduj_configure_endpoint(d,h,icc,sc,ep)){xhci_command_ring_destroy(&d.hid_ring);dma_release(&d.hid_raport);return false;}
    __atomic_thread_fence(__ATOMIC_RELEASE);XhciTrb t{};t.dword0=static_cast<uint32_t>(d.input_context.physical_address);t.dword1=static_cast<uint32_t>(d.input_context.physical_address>>32);t.dword3=xhci_trb::type_field(xhci_trb::TYPE_CONFIGURE_ENDPOINT)|(static_cast<uint32_t>(d.slot_id)<<24);
    xhci_log_cfg("configure_trb_dword0",t.dword0);xhci_log_cfg("configure_trb_dword1",t.dword1);xhci_log_cfg("configure_trb_dword2",t.dword2);xhci_log_cfg("configure_trb_dword3",t.dword3);
    CommandResult r=execute_command(t);if(!r.valid||r.code!=1){log_values("[xHCI] Configure Endpoint completion=",r.code);xhci_command_ring_destroy(&d.hid_ring);dma_release(&d.hid_raport);return false;}wypisz_log("[xHCI] Configure Endpoint: OK");log_values("[xHCI] DCI=",d.hid_dci);return true;
}
bool usb_ustaw_konfiguracje(XhciDevice& d,uint8_t wartosc){UsbSetupPacket s{0,9,wartosc,0,0};uint32_t got=0;return usb_control_transfer(d,s,nullptr,0,&got);}
bool hid_ustaw_protokol_boot(XhciDevice& d){UsbSetupPacket s{0x21,0x0B,0, d.hid_interfejs,0};uint32_t got=0;return usb_control_transfer(d,s,nullptr,0,&got);}
bool hid_ustaw_bezczynnosc(XhciDevice& d){UsbSetupPacket s{0x21,0x0A,0,d.hid_interfejs,0};uint32_t got=0;return usb_control_transfer(d,s,nullptr,0,&got);}
extern "C" void usb_wprowadz_raport_klawiatury(const uint8_t* raport);
bool xhci_zakolejkuj_transfer_interrupt_in(XhciDevice& d){if(!d.hid_gotowy||d.hid_transfer_oczekiwany)return false;XhciTrb t{};t.dword0=static_cast<uint32_t>(d.hid_raport.physical_address);t.dword1=static_cast<uint32_t>(d.hid_raport.physical_address>>32);t.dword2=d.hid_mps;t.dword3=xhci_trb::type_field(xhci_trb::TYPE_NORMAL)|(1U<<5);
    uint64_t ptr=0;if(!xhci_command_ring_enqueue(&d.hid_ring,t,&ptr))return false;__atomic_thread_fence(__ATOMIC_RELEASE);xhc.wanted_transfer=ptr;d.hid_transfer_oczekiwany=true;write32(xhc.doorbells,static_cast<uint32_t>(d.slot_id)*4U,d.hid_dci);if(!d.pierwszy_raport_zalogowany)wypisz_log("[USB-HID] First Interrupt IN transfer queued.");return true;}
void xhci_obsluz_hid(XhciDevice& d){if(!d.hid_gotowy)return;if(xhc.transfer_result.valid&&xhc.transfer_result.slot==d.slot_id&&xhc.transfer_result.endpoint==d.hid_dci){TransferResult r=xhc.transfer_result;xhc.transfer_result={};d.hid_transfer_oczekiwany=false;if(r.code==1||r.code==13){__atomic_thread_fence(__ATOMIC_ACQUIRE);const uint8_t* raport=static_cast<const uint8_t*>(d.hid_raport.virtual_address);if(!d.pierwszy_raport_zalogowany){log_values("[USB-HID] First report byte0=",raport[0]," byte2=",raport[2]);d.pierwszy_raport_zalogowany=true;}usb_wprowadz_raport_klawiatury(raport);xhci_ring_complete(&d.hid_ring,1);}}
    if(!d.hid_transfer_oczekiwany)xhci_zakolejkuj_transfer_interrupt_in(d);}

uint32_t* context(DmaBuffer& b,uint8_t index){return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(b.virtual_address)+static_cast<size_t>(index)*xhc.context_size);}
void zero_dma(DmaBuffer& b){volatile uint8_t* p=static_cast<volatile uint8_t*>(b.virtual_address);for(size_t i=0;i<b.size;++i)p[i]=0;}
bool prepare_device(XhciDevice& d,uint8_t slot,uint8_t port,SpeedInfo speed) {
    d.slot_id=slot;d.root_port=port;d.speed_id=speed.raw;d.logical_speed=speed.logical;d.protocol=xhc.ports[port].protocol;d.enabled=true;
    const size_t out_size=32U*xhc.context_size,in_size=33U*xhc.context_size;
    if(!dma_allocate(out_size,64,&d.output_context)||!dma_allocate(in_size,64,&d.input_context)||!xhci_command_ring_create(&d.ep0,EP0_TRBS))return false;
    uint64_t* dcbaa=static_cast<uint64_t*>(xhc.dcbaa.virtual_address);dcbaa[slot]=d.output_context.physical_address;
    uint32_t* icc=context(d.input_context,0);icc[1]=(1U<<0)|(1U<<1);
    uint32_t* sc=context(d.input_context,1);sc[0]=(static_cast<uint32_t>(speed.raw)<<20)|(1U<<27);sc[1]=static_cast<uint32_t>(port)<<16;
    switch(speed.logical){case USB_LOW_SPEED:case USB_FULL_SPEED:d.ep0_mps=8;break;case USB_HIGH_SPEED:d.ep0_mps=64;break;case USB_SUPER_SPEED:d.ep0_mps=512;break;default:return false;}
    log_values("[xHCI] EP0 initial MaxPacket=",d.ep0_mps);
    uint32_t* ep=context(d.input_context,2);ep[1]=(static_cast<uint32_t>(d.ep0_mps)<<16)|(4U<<3)|(3U<<1);
    ep[2]=static_cast<uint32_t>(d.ep0.dma.physical_address)|1U;ep[3]=static_cast<uint32_t>(d.ep0.dma.physical_address>>32);ep[4]=8;
    __atomic_thread_fence(__ATOMIC_RELEASE);log_values("[xHCI] Device Context address=",d.output_context.physical_address,nullptr,0,true);
    wypisz_log("[xHCI] Device Context OK.");log_values("[xHCI] EP0 Ring address=",d.ep0.dma.physical_address,nullptr,0,true);wypisz_log("[xHCI] EP0 Ring OK.");return true;
}

bool address_device(XhciDevice& d,bool bsr=false) {XhciTrb t{};t.dword0=static_cast<uint32_t>(d.input_context.physical_address);t.dword1=static_cast<uint32_t>(d.input_context.physical_address>>32);
 t.dword3=xhci_trb::type_field(xhci_trb::TYPE_ADDRESS_DEVICE)|(static_cast<uint32_t>(d.slot_id)<<24)|(bsr?(1U<<9):0U);
 log_values("[xHCI] Address Device slot=",d.slot_id);CommandResult r=execute_command(t);if(!r.valid||r.code!=1||r.slot!=d.slot_id)return false;
 d.addressed=true;wypisz_log("[xHCI] Address Device OK.");__atomic_thread_fence(__ATOMIC_ACQUIRE);uint32_t* sc=context(d.output_context,0);
 log_values("[xHCI] Device Address=",sc[3]&0xFFU);log_values("[xHCI] Slot State=",(sc[3]>>27)&31U);return true;}

bool evaluate_ep0(XhciDevice& d,uint16_t mps){zero_dma(d.input_context);uint32_t* icc=context(d.input_context,0);icc[1]=1U<<1;
 uint32_t* ep=context(d.input_context,2);ep[1]=static_cast<uint32_t>(mps)<<16;__atomic_thread_fence(__ATOMIC_RELEASE);
 XhciTrb t{};t.dword0=static_cast<uint32_t>(d.input_context.physical_address);t.dword1=static_cast<uint32_t>(d.input_context.physical_address>>32);
 t.dword3=xhci_trb::type_field(xhci_trb::TYPE_EVALUATE_CONTEXT)|(static_cast<uint32_t>(d.slot_id)<<24);
 CommandResult r=execute_command(t);if(!r.valid||r.code!=1||r.slot!=d.slot_id)return false;d.ep0_mps=mps;return true;}

bool usb_control_transfer(XhciDevice& d,const UsbSetupPacket& setup,DmaBuffer* data,
                          uint16_t length,uint32_t* actual) {
    if(length&&(!data||data->size<length))return false;
    XhciTrb s{},dt{},st{};
    const uint8_t* q=reinterpret_cast<const uint8_t*>(&setup);s.dword0=static_cast<uint32_t>(q[0])|(static_cast<uint32_t>(q[1])<<8)|(static_cast<uint32_t>(q[2])<<16)|(static_cast<uint32_t>(q[3])<<24);
    s.dword1=static_cast<uint32_t>(q[4])|(static_cast<uint32_t>(q[5])<<8)|(static_cast<uint32_t>(q[6])<<16)|(static_cast<uint32_t>(q[7])<<24);
    s.dword2=8;s.dword3=xhci_trb::type_field(xhci_trb::TYPE_SETUP_STAGE)|(1U<<6)|
      ((length?((setup.bmRequestType&0x80U)?3U:2U):0U)<<16)|(1U<<4);
    uint64_t ignore=0,last=0;if(!xhci_command_ring_enqueue(&d.ep0,s,&ignore))return false;
    uint16_t count=1;if(length){dt.dword0=static_cast<uint32_t>(data->physical_address);dt.dword1=static_cast<uint32_t>(data->physical_address>>32);dt.dword2=length;
      dt.dword3=xhci_trb::type_field(xhci_trb::TYPE_DATA_STAGE)|((setup.bmRequestType&0x80U)?(1U<<16):0U)|(1U<<4)|(1U<<2);
      if(!xhci_command_ring_enqueue(&d.ep0,dt,&ignore))return false;
      ++count;}
    st.dword3=xhci_trb::type_field(xhci_trb::TYPE_STATUS_STAGE)|(1U<<5)|((length&&(setup.bmRequestType&0x80U))?0U:(1U<<16));
    if(!xhci_command_ring_enqueue(&d.ep0,st,&last))return false;
    ++count;
    xhc.wanted_transfer=last;xhc.transfer_result={};__atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,static_cast<uint32_t>(d.slot_id)*4U,1U);
    const uint64_t end=deadline();while(before_deadline(end)){xhci_poll_events(64);if((read32(xhc.operational,PORTSC_BASE+(d.root_port-1U)*16U)&PORT_CCS)==0)break;
      if(xhc.transfer_result.valid){TransferResult r=xhc.transfer_result;xhc.wanted_transfer=0;xhci_ring_complete(&d.ep0,count);
        if(r.slot!=d.slot_id||r.endpoint!=1||r.residual>length||(r.code!=1&&r.code!=13))return false;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);if(actual)*actual=length-r.residual;return true;}asm volatile("pause":::"memory");}
    xhc.wanted_transfer=0;return false;
}

bool get_descriptor(XhciDevice& d,uint16_t length,DmaBuffer& data,uint32_t* actual){UsbSetupPacket s{0x80,6,static_cast<uint16_t>(1U<<8),0,length};return usb_control_transfer(d,s,&data,length,actual);}
bool valid_mps(XhciDevice& d,uint8_t raw,uint16_t* mps){uint16_t n=raw;if(d.logical_speed==USB_SUPER_SPEED){if(raw>15)return false;n=static_cast<uint16_t>(1U<<raw);}
 if(d.logical_speed==USB_LOW_SPEED&&n!=8)return false;
 if(d.logical_speed==USB_HIGH_SPEED&&n!=64)return false;
 if(d.logical_speed==USB_SUPER_SPEED&&n!=512)return false;
 if(d.logical_speed==USB_FULL_SPEED&&(n!=8&&n!=16&&n!=32&&n!=64))return false;
 *mps=n;return true;}

bool enumerate_port(uint8_t port) {
    Protocol* proto=xhc.ports[port].protocol;if(!proto){log_port_text(port," protocol=","unknown");return false;}
    log_port_text(port," protocol=",proto->usb2?"USB2":proto->usb3?"USB3":"unknown");if(!proto->usb2&&!proto->usb3)return false;
    if(!reset_port(port))return false;
    SpeedInfo speed=xhci_pobierz_predkosc_portu(port);
    XhciTrb enable{};enable.dword3=xhci_trb::type_field(xhci_trb::TYPE_ENABLE_SLOT)|(static_cast<uint32_t>(proto->slot_type)<<16);wypisz_log("[xHCI] Enable Slot...");
    CommandResult er=execute_command(enable);if(!er.valid||er.code!=1||er.slot==0||er.slot>xhc.enabled_slots){log_values("[xHCI-ERR] Enable Slot completion=",er.code);return false;}
    log_values("[xHCI] Enable Slot OK: slot=",er.slot);XhciDevice& d=xhc.devices[er.slot];d.enabled=true;d.slot_id=er.slot;
    if(!prepare_device(d,er.slot,port,speed)||!address_device(d)){if(disable_slot(d))free_device(d);return false;}
    DmaBuffer data{};if(!dma_allocate(18,64,&data)){if(disable_slot(d))free_device(d);return false;}uint32_t got=0;
    if(!get_descriptor(d,8,data,&got)||got<8){dma_release(&data);if(disable_slot(d))free_device(d);return false;}wypisz_log("[USB] GET_DESCRIPTOR(8) OK.");
    const uint8_t raw=static_cast<uint8_t*>(data.virtual_address)[7];uint16_t mps=0;if(!valid_mps(d,raw,&mps)){dma_release(&data);if(disable_slot(d))free_device(d);return false;}
    log_values("[USB] EP0 MaxPacket=",mps);if(mps!=d.ep0_mps){if(!evaluate_ep0(d,mps)){dma_release(&data);if(disable_slot(d))free_device(d);return false;}wypisz_log("[xHCI] Evaluate Context OK.");}
    else wypisz_log("[xHCI] Evaluate Context not required.");
    zero_dma(data);if(!get_descriptor(d,18,data,&got)||got<18){dma_release(&data);if(disable_slot(d))free_device(d);return false;}
    const uint8_t* b=static_cast<const uint8_t*>(data.virtual_address);if(b[0]<18||b[1]!=1){dma_release(&data);if(disable_slot(d))free_device(d);return false;}
    wypisz_log("[USB] GET_DESCRIPTOR(18) OK.");
    wypisz_log("[USB] Device Descriptor:");log_values("[USB] bcdUSB=",le16(b+2),nullptr,0,true);log_values("[USB] VID=",le16(b+8),nullptr,0,true);log_values("[USB] PID=",le16(b+10),nullptr,0,true);
    log_values("[USB] class=",b[4],nullptr,0,true);log_values("[USB] subclass=",b[5],nullptr,0,true);log_values("[USB] protocol=",b[6],nullptr,0,true);log_values("[USB] configurations=",b[17]);
    DmaBuffer konfiguracja{};hid_wynik_konfiguracji hid{};
    if(usb_pobierz_konfiguracje(d,konfiguracja,&hid)&&hid.znaleziono){
      log_values("[USB-HID] bcdHID=",hid.hid_bcd,nullptr,0,true);log_values("[USB-HID] Report Descriptor length=",hid.raport_dlugosc);
      log_values("[USB-HID] endpoint=",hid.endpoint,nullptr,0,true);log_values("[USB-HID] wMaxPacketSize=",hid.maksymalny_rozmiar);log_values("[USB-HID] bInterval=",hid.interwal);
      if(xhci_skonfiguruj_endpoint(d,hid)&&usb_ustaw_konfiguracje(d,hid.konfiguracja)){
        d.konfiguracja=hid.konfiguracja;wypisz_log("[USB] SET_CONFIGURATION: OK");
        if(hid_ustaw_protokol_boot(d)){wypisz_log("[USB-HID] SET_PROTOCOL BOOT: OK");if(hid_ustaw_bezczynnosc(d)){
          wypisz_log("[USB-HID] SET_IDLE: OK");d.hid_gotowy=true;xhci_zakolejkuj_transfer_interrupt_in(d);
        }else wypisz_log("[USB-HID] SET_IDLE: STALL/BLAD");}else wypisz_log("[USB-HID] SET_PROTOCOL BOOT: STALL/BLAD");
      }else wypisz_log("[USB] Konfiguracja HID nieudana.");
    }else wypisz_log("[USB] Brak obslugiwanej klawiatury HID Boot.");
    dma_release(&konfiguracja);wypisz_log(d.hid_gotowy?"[USB-HID] Klawiatura Boot HID skonfigurowana.":"[USB] Urzadzenie wykryte; klasy jeszcze nie konfiguruje.");dma_release(&data);
    if(hid.znaleziono&&!d.hid_gotowy){if(disable_slot(d))free_device(d);return false;}
    return true;
}

void ports(Controller& c) {
    for(uint32_t p=1;p<=c.max_ports;++p){const uint32_t v=read32(c.operational,PORTSC_BASE+(p-1U)*16U);char s[128],*q=s,*e=s+sizeof(s);append(q,e,"[xHCI] Port ");append_dec(q,e,p);append(q,e,": ");
        if((v&PORT_CCS)==0)append(q,e,"disconnected");else{append(q,e,"connected speed=");append_dec(q,e,(v>>PORT_SPEED_SHIFT)&PORT_SPEED_MASK);append(q,e," state=");append_dec(q,e,(v>>PORT_PLS_SHIFT)&PORT_PLS_MASK);append(q,e," PORTSC=");append_hex(q,e,v);}*q='\0';wypisz_log(s);}
}

void enumerate_connected_ports(){for(uint8_t p=1;p<=xhc.max_ports;++p){const uint32_t v=read32(xhc.operational,PORTSC_BASE+(p-1U)*16U);
 if(v&PORT_CCS){log_port_text(p,": ","connected.");enumerate_port(p);}}}
}

uint32_t xhci_read32(const volatile void* base,uint32_t offset){return read32(static_cast<const volatile uint8_t*>(base),offset);}
void xhci_write32(volatile void* base,uint32_t offset,uint32_t value){write32(static_cast<volatile uint8_t*>(base),offset,value);}
uint64_t xhci_read64(const volatile void* base,uint32_t offset){return read64(static_cast<const volatile uint8_t*>(base),offset);}
void xhci_write64(volatile void* base,uint32_t offset,uint64_t value){write64(static_cast<volatile uint8_t*>(base),offset,value);}

bool xhci_inicjalizuj_pierwszy() {
    xhc={}; if(!hpet_dostepny()){wypisz_log("[xHCI-ERR] HPET niedostepny; bezpieczne timeouty niemozliwe.");return false;}
    if(!find_controller(&xhc.pci))return false;
    wypisz_log("[xHCI] PCI controller znaleziony.");log_values("[xHCI] vendor=",xhc.pci.vendor," device=",xhc.pci.device,true);
    if(!get_bar(xhc)){wypisz_log("[xHCI-ERR] Niepoprawny Memory BAR.");return false;}
    log_values("[xHCI] BAR=",xhc.bar,xhc.bar64?" (64-bit), size=":" (32-bit), size=",xhc.bar_size,true);
    if(!enable_pci(xhc)){wypisz_log("[xHCI-ERR] PCI Memory/Bus Master nieaktywne.");return false;}
    if(!map_mmio(xhc))return false;
    const uint8_t caplength=*reinterpret_cast<volatile uint8_t*>(xhc.mmio);
    xhc.version=static_cast<uint16_t>((read32(xhc.mmio,0)>>16)&0xFFFFU);
    const uint32_t hcs1=read32(xhc.mmio,0x04),hcs2=read32(xhc.mmio,0x08),hcs3=read32(xhc.mmio,0x0C),hcc=read32(xhc.mmio,0x10);
    const uint32_t dboff=read32(xhc.mmio,0x14)&~3U,rtsoff=read32(xhc.mmio,0x18)&~0x1FU;
    (void)hcs3;
    xhc.max_slots=hcs1&0xFFU;xhc.max_intrs=(hcs1>>8)&0x7FFU;xhc.max_ports=(hcs1>>24)&0xFFU;
    xhc.scratchpads=(((hcs2>>21)&0x1FU)<<5)|((hcs2>>27)&0x1FU);
    xhc.context_size=(hcc&(1U<<2))?64U:32U;
    if(caplength<0x20U||xhc.max_slots==0||xhc.max_ports==0||xhc.max_ports>BURSZTYN_XHCI_MAX_PORTS||xhc.scratchpads>MAX_SCRATCHPADS||
       caplength> xhc.bar_size||dboff>xhc.bar_size-4U||rtsoff>xhc.bar_size-0x40U)return false;
    xhc.operational=xhc.mmio+caplength;xhc.runtime=xhc.mmio+rtsoff;xhc.doorbells=xhc.mmio+dboff;
    log_values("[xHCI] version=",xhc.version,nullptr,0,true);log_values("[xHCI] max_slots=",xhc.max_slots);log_values("[xHCI] max_ports=",xhc.max_ports);log_values("[xHCI] max_intrs=",xhc.max_intrs);
    log_values("[xHCI] context_size=",xhc.context_size);log_values("[xHCI] scratchpads=",xhc.scratchpads);
    if(!ext_capabilities(xhc,hcc))return false;
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)&~CMD_RS);
    if(!wait_bits(xhc.operational,USBSTS,STS_HCH,STS_HCH)){wypisz_log("[xHCI-ERR] Timeout halt.");return false;}wypisz_log("[xHCI] Halt: OK");
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)|CMD_HCRST);
    if(!wait_bits(xhc.operational,USBCMD,CMD_HCRST,0)||!wait_bits(xhc.operational,USBSTS,STS_CNR,0)){wypisz_log("[xHCI-ERR] Timeout HCRST/CNR.");return false;}wypisz_log("[xHCI] Reset: OK");
    const uint32_t page_mask=read32(xhc.operational,PAGESIZE);log_values("[xHCI] pagesize mask=",page_mask,nullptr,0,true);
    if((page_mask&1U)==0){wypisz_log("[xHCI-ERR] Kontroler nie wspiera wymaganej wielkosci strony.");return false;}xhc.page_size=4096;
    xhc.enabled_slots=xhc.max_slots<BURSZTYN_XHCI_MAX_SLOTS?xhc.max_slots:BURSZTYN_XHCI_MAX_SLOTS;
    if(!allocate_structures(xhc)){wypisz_log("[xHCI-ERR] Brak pamieci DMA.");return false;}
    configure(xhc);wypisz_log("[xHCI] DCBAA: OK");wypisz_log("[xHCI] Command Ring: OK");wypisz_log("[xHCI] Event Ring: OK");
    write32(xhc.operational,USBCMD,read32(xhc.operational,USBCMD)|CMD_RS);
    if(!wait_bits(xhc.operational,USBSTS,STS_HCH,0)){wypisz_log("[xHCI-ERR] Timeout Run.");return false;}
    const uint32_t status=read32(xhc.operational,USBSTS);if((status&(STS_HSE|STS_CNR))!=0){wypisz_log("[xHCI-ERR] Krytyczny USBSTS po Run.");return false;}
    xhc.ready=true;wypisz_log("[xHCI] Run: OK");ports(xhc);enumerate_connected_ports();return true;
}

size_t xhci_poll_events(size_t budget) {
    if(!xhc.ready||budget==0)return 0;
    if(budget>256U)budget=256U;
    XhciTrb* ring=static_cast<XhciTrb*>(xhc.event_segment.virtual_address);size_t done=0;
    while(done<budget){XhciTrb& t=ring[xhc.event_index];const bool cycle=(t.dword3&xhci_trb::CYCLE)!=0;if(cycle!=xhc.event_cycle)break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);const uint32_t type=xhci_trb::type(t.dword3);
        if(type==xhci_trb::TYPE_COMMAND_COMPLETION){const uint64_t ptr=(static_cast<uint64_t>(t.dword1)<<32)|(t.dword0&~15U);const uint8_t code=static_cast<uint8_t>(t.dword2>>24),slot=static_cast<uint8_t>(t.dword3>>24);
          const uint64_t begin=xhc.command.dma.physical_address,end=begin+static_cast<uint64_t>(xhc.command.trb_count-1U)*sizeof(XhciTrb);
          if(ptr>=begin&&ptr<end&&((ptr-begin)%sizeof(XhciTrb))==0){xhci_command_ring_complete(&xhc.command);if(ptr==xhc.wanted_command)xhc.command_result={code,slot,true};}}
        else if(type==xhci_trb::TYPE_PORT_STATUS_CHANGE){const uint8_t port=static_cast<uint8_t>(t.dword0>>24);if(port>=1&&port<=xhc.max_ports){xhc.port_event=true;if(xhc.pending_port==port)xhc.port_event=true;}}
        else if(type==xhci_trb::TYPE_TRANSFER_EVENT){const uint64_t ptr=(static_cast<uint64_t>(t.dword1)<<32)|(t.dword0&~15U);const uint8_t code=static_cast<uint8_t>(t.dword2>>24),ep=static_cast<uint8_t>((t.dword3>>16)&31U),slot=static_cast<uint8_t>(t.dword3>>24);const uint32_t residual=t.dword2&0xFFFFFFU;
          if(slot>=1&&slot<=xhc.enabled_slots&&ep>=1&&ep<=31&&xhc.devices[slot].enabled){XhciDevice& d=xhc.devices[slot];
            const bool hid=ep==d.hid_dci&&d.hid_gotowy;const uint64_t base=hid?d.hid_ring.dma.physical_address:d.ep0.dma.physical_address;
            const uint16_t liczba=hid?d.hid_ring.trb_count:d.ep0.trb_count;const uint64_t end=base+static_cast<uint64_t>(liczba-1U)*sizeof(XhciTrb);
            if((hid||ep==1)&&ptr>=base&&ptr<end&&((ptr-base)%sizeof(XhciTrb))==0&&xhc.wanted_transfer!=0)xhc.transfer_result={code,slot,ep,residual,ptr,true};}}
        ++xhc.event_index;if(xhc.event_index==EVENT_TRBS){xhc.event_index=0;xhc.event_cycle=!xhc.event_cycle;}++done;}
    if(done){const uint64_t erdp=xhc.event_segment.physical_address+static_cast<uint64_t>(xhc.event_index)*sizeof(XhciTrb);write64(xhc.runtime+0x20U,0x18,erdp|(1U<<3));}
    return done;
}

void usb_obsluz(){if(!xhc.ready)return;xhci_poll_events(16);uint32_t budzet=4;for(uint32_t s=1;s<=xhc.enabled_slots&&budzet;++s)if(xhc.devices[s].hid_gotowy){xhci_obsluz_hid(xhc.devices[s]);--budzet;}}
