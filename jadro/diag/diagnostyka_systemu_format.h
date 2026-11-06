#pragma once
#include "diagnostyka_systemu_abi.h"
#include "wersja_systemu.h"
struct DiagPreferences {bool autostart=true, problems=true;};
constexpr const char* DIAG_CONFIG_PATH="/uzytkownicy/DIAG.CFG";
constexpr const char* DIAG_REPORT_PATH="/logi/DIAG.TXT";
constexpr const char* DIAG_BOOT_PATH="/logi/BOOTLOG.TXT";
constexpr const char* DIAG_APP_PATH="/programy/diagnostyka.cebula/diagnostyka.bur";
inline DiagPreferences diag_parse_preferences(const char* text,size_t length){
 DiagPreferences p{};if(!text||length>128)return p;
 // Exact known keys only, bounded line parsing. Invalid/unknown values retain defaults.
 for(size_t at=0;at<length;){size_t end=at;while(end<length&&text[end]!='\n')++end;
  size_t n=end-at;if(n&&text[at+n-1]=='\r')--n;
  const char* keys[]={"autostart=","problems="};
  for(unsigned k=0;k<2;++k){size_t j=0;while(keys[k][j]&&j<n&&keys[k][j]==text[at+j])++j;
   if(!keys[k][j]&&n==j+1&&(text[at+j]=='0'||text[at+j]=='1')){
    if(k==0)p.autostart=text[at+j]=='1';else p.problems=text[at+j]=='1';}}
  at=end<length?end+1:end;
 }return p;
}
inline bool diag_should_autostart(const DiagPreferences& p,const DiagSummary& s,uint64_t elapsed_ns){
 return (p.autostart&&elapsed_ns>=1000000000ULL)||
  (!p.autostart&&p.problems&&s.serious&&elapsed_ns>=15000000000ULL);
}
inline const char* diag_status_text(uint32_t s){switch(s){
 case DIAG_OK:return "Sprawny";case DIAG_INACTIVE:return "Nieaktywny";
 case DIAG_NOT_FOUND:return "Nie znaleziono";case DIAG_UNSUPPORTED:return "Nieobsługiwany";
 case DIAG_WARNING:return "Ostrzeżenie";case DIAG_INIT_FAILED:return "Błąd inicjalizacji";
 case DIAG_TIMEOUT:return "Przekroczony czas";case DIAG_DRIVER_MISSING:return "Brak sterownika";
 case DIAG_DETECTED_UNSUPPORTED:return "Wykryto, nieobsługiwany";case DIAG_CRITICAL:return "Błąd krytyczny";
 default:return "Brak danych";}}
inline const char* diag_code_text(uint32_t c){switch(c){
 case DIAG_CODE_XHCI_INIT:return "INPUT-USB-XHCI-0001";case DIAG_CODE_HID_INIT:return "INPUT-HID-0002";
 case DIAG_CODE_HDA_INIT:return "AUDIO-HDA-0001";case DIAG_CODE_AHCI_INIT:return "STORAGE-AHCI-0001";
 case DIAG_CODE_NO_KEYBOARD:return "INPUT-KBD-0001";case DIAG_CODE_NO_POINTER:return "INPUT-PTR-0001";
 case DIAG_CODE_FILESYSTEM:return "STORAGE-PSF-0001";case DIAG_CODE_STORE_FULL:return "SYSTEM-DIAG-0001";
 case DIAG_CODE_I2C_DRIVER:return "INPUT-I2C-0001";default:return "";}}
inline const char* diag_transport_text(uint32_t t){const char* n[]={"—","PCI","USB","PS/2","VirtIO","I2C","RAM","ACPI"};return t<8?n[t]:"?";}
inline const char* diag_category_text(uint32_t c){const char* n[]={"System","Procesor","Pamięć","ACPI","APIC","HPET","PCI","Grafika","Dyski","AHCI","VirtIO Block","System plików","USB","xHCI","OHCI","PS/2","Klawiatura","Mysz","Touchpad","Dźwięk HDA","Sieć","VirtIO Input","VirtIO Net","HID"};return c<DIAG_CATEGORY_COUNT?n[c]:"?";}
struct DiagText {
 char* data;size_t capacity,used=0;bool truncated=false;
 DiagText(char* d,size_t c):data(d),capacity(c){if(c)data[0]=0;}
 void add(const char* s){if(!s)return;for(size_t i=0;s[i];++i){if(used+1>=capacity){truncated=true;return;}data[used++]=s[i];data[used]=0;}}
 void number(uint64_t n,uint32_t base=10){char b[32];size_t i=0;if(base<2||base>16)return;do{b[i++]="0123456789ABCDEF"[n%base];n/=base;}while(n&&i<sizeof(b));while(i){char c[]={b[--i],0};add(c);}}
 void scalar(const char* n,uint64_t v,uint32_t base=10){add(n);number(v,base);}
};
inline void diag_report_summary(DiagText& t,const DiagSummary& s){
 t.add(BURSZTYN_NAZWA_WERSJI "\nDiagnostyka systemu — ABI 1\nStan: ");t.add(diag_status_text(s.status));
 t.scalar("\nBłędy: ",s.errors);t.scalar("  Ostrzeżenia: ",s.warnings);
 t.add("\nKlawiatura: ");t.add(s.keyboard?"Sprawny":"Nie znaleziono INPUT-KBD-0001");
 t.add("\nMysz / wskaźnik: ");t.add(s.pointer?"Sprawny":"Nie znaleziono INPUT-PTR-0001");
 t.scalar("\nPominięte urządzenia: ",s.dropped_devices);t.scalar("  Pominięte logi: ",s.dropped_logs);t.add("\n");
}
inline void diag_report_device(DiagText& t,const DiagDevice& d){
 t.scalar("\n#",d.id);t.scalar(" parent=",d.parent);t.add(" ");t.add(diag_category_text(d.category));t.add(" | ");t.add(d.name);
 t.add(" | ");t.add(d.driver);t.add(" | ");t.add(diag_status_text(d.status));t.add(" | ");t.add(diag_transport_text(d.transport));
 t.add(d.flags&DIAG_FLAG_DETECTED?" | wykryto":" | niewykryte");
 if(d.pci_valid){t.scalar(" BDF=",d.bus,16);t.scalar(":",d.pci_device,16);t.scalar(".",d.function,16);}
 if(d.vendor||d.device){t.scalar(" VID=",d.vendor,16);t.scalar(" PID=",d.device,16);}
 if(d.bar)t.scalar(" BAR=0x",d.bar,16);
 if(d.category==DIAG_PCI)t.scalar(" class=0x",d.value,16);
 if(d.category==DIAG_MEMORY)t.scalar(" RAM_bytes=",d.value);
 if(d.category==DIAG_XHCI){t.scalar(" controller=",d.controller);t.scalar(" ports=",d.ports);t.scalar(" slots=",d.max_slots);t.scalar(" version=0x",d.xhci_version,16);t.scalar(" USBSTS=0x",d.hardware_status,16);}
 if(d.category==DIAG_USB||d.category==DIAG_HID){t.scalar(" controller=",d.controller);t.scalar(" port=",d.port);t.scalar(" slot=",d.slot);t.scalar(" class=",d.usb_class);t.scalar(" config=",d.configuration);t.scalar(" interfaces=",d.interfaces);t.scalar(" HID=",d.hid_interfaces);}
 if(d.category==DIAG_USB||d.category==DIAG_HID)t.scalar(" selected_config=",d.value);
 if(d.category==DIAG_HID){t.scalar(" interface=",d.interface_number);t.scalar(" alt=",d.alternate);t.scalar(" subclass=",d.subclass);t.scalar(" protocol=",d.protocol);t.scalar(" endpoint=0x",d.endpoint,16);t.scalar(" DCI=",d.dci);t.scalar(" MPS=",d.packet_size);t.scalar(" interval=",d.interval);t.scalar(" role=",d.role);}
 if(d.category==DIAG_HDA)t.scalar(" responsive_codecs=",d.value);
 t.add("\n  ");t.add(d.message);if(d.code){t.add(" [");t.add(diag_code_text(d.code));t.add("]");}t.add("\n");
}
inline bool diag_log_matches(const DiagLog& l,unsigned filter){
 if(filter==0)return true;
 if(filter==1)return l.severity==DIAG_INIT_FAILED||l.severity==DIAG_TIMEOUT||l.severity==DIAG_CRITICAL;
 if(filter==2)return l.severity==DIAG_WARNING||l.severity==DIAG_DRIVER_MISSING||l.severity==DIAG_DETECTED_UNSUPPORTED;
 if(filter==3)return l.category==DIAG_USB||l.category==DIAG_XHCI||l.category==DIAG_OHCI||l.category==DIAG_HID;
 if(filter==4)return l.category==DIAG_PCI;
 if(filter==5)return l.category==DIAG_PS2||l.category==DIAG_KEYBOARD||l.category==DIAG_MOUSE||l.category==DIAG_TOUCHPAD||l.category==DIAG_VIRTIO_INPUT||l.category==DIAG_HID;
 if(filter==6)return l.category==DIAG_HDA;
 if(filter==7)return l.category==DIAG_STORAGE||l.category==DIAG_AHCI||l.category==DIAG_VIRTIO_BLOCK||l.category==DIAG_FILESYSTEM;
 return filter==8&&(l.category==DIAG_NETWORK||l.category==DIAG_VIRTIO_NET);
}
