#include "diagnostyka_systemu.h"
namespace DiagStore {
DiagDevice devices[DIAG_MAX_DEVICES]{};
uint64_t keys[DIAG_MAX_DEVICES]{};
DiagLog logs[DIAG_MAX_LOG]{};
uint32_t count{}, log_count{}, generation{}, dropped_devices{}, dropped_logs{};
uint32_t event_errors{},event_warnings{},input_keyboards{},input_pointers{};
bool system_critical_event{};
uint8_t lock{};
struct Guard {
 uint64_t flags{};
 Guard(){
#ifndef DIAG_HOST_TEST
  asm volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
#endif
  while(__atomic_test_and_set(&lock,__ATOMIC_ACQUIRE))asm volatile("pause");
 }
 ~Guard(){__atomic_clear(&lock,__ATOMIC_RELEASE);
#ifndef DIAG_HOST_TEST
  if(flags&(1ULL<<9))asm volatile("sti":::"memory");
#endif
 }
};
bool error(uint32_t s){return s==DIAG_INIT_FAILED||s==DIAG_TIMEOUT||s==DIAG_CRITICAL;}
bool warning(uint32_t s){return s==DIAG_WARNING||s==DIAG_DRIVER_MISSING||s==DIAG_DETECTED_UNSUPPORTED;}
unsigned priority(uint32_t status){switch(status){
 case DIAG_CRITICAL:return 9;case DIAG_TIMEOUT:return 8;case DIAG_INIT_FAILED:return 7;
 case DIAG_WARNING:return 6;case DIAG_DRIVER_MISSING:case DIAG_DETECTED_UNSUPPORTED:return 5;
 case DIAG_UNSUPPORTED:return 4;case DIAG_INACTIVE:return 1;default:return 0;}}
bool hex_char(char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
bool word_char(char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
bool preceding(const char* text,size_t offset,const char* key){
 size_t n=0;while(key[n])++n;if(n>offset)return false;
 for(size_t i=0;i<n;++i)if(text[offset-n+i]!=key[i])return false;
 return true;
}
// Serial output is a separate unrestricted channel. Ring 3 receives textual
// diagnostics with opaque numeric/address tokens removed. Large scalar hardware
// values remain available in the typed ABI; only BAR/USBSTS/PORTSC text fields
// are explicitly allowed here. This also handles unprefixed and decimal pointers.
uint32_t safe_text(char* output,size_t capacity,const char* input){
 if(!capacity)return DIAG_FLAG_TRUNCATED;
 size_t pos=0;uint32_t flags=0;output[0]=0;if(!input)return 0;
 size_t scanned=0;
 for(size_t i=0;i<1024&&input[i];){scanned=i;size_t end=i;bool number=false,redact=false;
  const bool prefixed=i+1<1024&&input[i]=='0'&&(input[i+1]=='x'||input[i+1]=='X');
  if(prefixed){end=i+2;while(end<1024&&hex_char(input[end]))++end;number=end>i+2;redact=end-i-2>=6;}
  else if((i==0||!word_char(input[i-1]))&&hex_char(input[i])){
   while(end<1024&&hex_char(input[end]))++end;
   if(end==1024||!word_char(input[end])){number=true;bool letters=false;
    for(size_t k=i;k<end;++k)if(input[k]<'0'||input[k]>'9')letters=true;
    redact=end-i>=(letters?6U:7U);
   }
  }
  const bool address_field=preceding(input,i,"address=")||preceding(input,i,"addr=")||
      preceding(input,i,"pointer=")||preceding(input,i,"ptr=")||preceding(input,i,"DMA=")||
      preceding(input,i,"MMIO=")||preceding(input,i,"TRB=")||preceding(input,i,"CR3=");
  if(address_field)redact=true;
  const bool allowed=preceding(input,i,"BAR=")||preceding(input,i,"BAR0=")||
      preceding(input,i,"USBSTS=")||preceding(input,i,"PORTSC=");
  if(number&&redact&&!allowed){const char replacement[]="[adres ukryty]";
   for(size_t j=0;replacement[j];++j){if(pos+1>=capacity){flags|=DIAG_FLAG_TRUNCATED;output[pos]=0;return flags;}output[pos++]=replacement[j];}
   flags|=DIAG_FLAG_REDACTED;i=end;scanned=i;
  }else{if(pos+1>=capacity){flags|=DIAG_FLAG_TRUNCATED;break;}output[pos++]=input[i++];scanned=i;}
 }
 if(scanned>=1024)flags|=DIAG_FLAG_TRUNCATED;
 output[pos]=0;return flags;
}
void append_locked(uint32_t c,uint32_t s,uint32_t code,const char* text){
 if(log_count==DIAG_MAX_LOG){if(dropped_logs!=UINT32_MAX)++dropped_logs;return;}
 auto& l=logs[log_count]; l.version=DIAG_ABI;l.size=sizeof(l);l.sequence=log_count++;
 l.category=c;l.severity=s;l.code=code;l.flags=safe_text(l.text,sizeof(l.text),text);
}
bool contains(const char* s,const char* needle){if(!s)return false;for(;*s;++s){size_t i=0;while(needle[i]&&s[i]==needle[i])++i;if(!needle[i])return true;}return false;}
}
using namespace DiagStore;
extern "C" uint32_t diag_publish_impl(uint64_t key,const DiagDevice* input){
 if(!input||input->category>=DIAG_CATEGORY_COUNT||input->status>DIAG_CRITICAL||input->transport>DIAG_TRANSPORT_FIRMWARE)return 0;
 Guard guard;uint32_t i=0;
 for(;i<count;++i)if(keys[i]==key&&devices[i].category==input->category)break;
 if(i==count){if(count==DIAG_MAX_DEVICES){if(dropped_devices!=UINT32_MAX)++dropped_devices;return 0;}++count;keys[i]=key;}
 const bool changed=devices[i].version==0||devices[i].status!=input->status||devices[i].code!=input->code;
 DiagDevice normalized=*input;normalized.id=i+1;normalized.version=DIAG_ABI;normalized.size=sizeof(normalized);normalized.reserved=0;
 char message[sizeof(normalized.message)];
 for(size_t b=0;b<sizeof(message);++b)message[b]=normalized.message[b];
 if(message[sizeof(message)-1])normalized.flags|=DIAG_FLAG_TRUNCATED;
 message[sizeof(message)-1]=0;
 normalized.flags|=safe_text(normalized.message,sizeof(normalized.message),message);
 normalized.name[sizeof(normalized.name)-1]=0;normalized.driver[sizeof(normalized.driver)-1]=0;normalized.message[sizeof(normalized.message)-1]=0;
 const auto* old=reinterpret_cast<const uint8_t*>(&devices[i]);const auto* next=reinterpret_cast<const uint8_t*>(&normalized);
 bool equal=true;for(size_t b=0;b<sizeof(normalized);++b)if(old[b]!=next[b]){equal=false;break;}
 if(equal)return i+1;
 devices[i]=normalized;auto& d=devices[i];
 if(d.parent>count||d.parent==d.id)d.parent=0;
 d.name[sizeof(d.name)-1]=0;d.driver[sizeof(d.driver)-1]=0;d.message[sizeof(d.message)-1]=0;
 ++generation;
 if(changed)append_locked(d.category,d.status,d.code,d.message);
 return d.id;
}
extern "C" void diag_log_impl(uint32_t c,uint32_t s,uint32_t code,const char* text){
 if(c>=DIAG_CATEGORY_COUNT||s>DIAG_CRITICAL||!text)return;
 Guard guard;
 if(c==DIAG_SYSTEM&&s==DIAG_CRITICAL)system_critical_event=true;
 if(error(s)&&event_errors!=UINT32_MAX)++event_errors;
 else if(warning(s)&&event_warnings!=UINT32_MAX)++event_warnings;
 append_locked(c,s,code,text);
}
void diag_capture_serial(const char* text){
 uint32_t c=DIAG_SYSTEM;
 if(contains(text,"USB HID")||contains(text,"USB-HID"))c=DIAG_HID;
 else if(contains(text,"VIRTIO-INPUT"))c=DIAG_VIRTIO_INPUT;
 else if(contains(text,"xHCI")||contains(text,"XHCI"))c=DIAG_XHCI;
 else if(contains(text,"USB"))c=DIAG_USB;
 else if(contains(text,"PCI"))c=DIAG_PCI;
 else if(contains(text,"PS/2")||contains(text,"INPUT"))c=DIAG_KEYBOARD;
 else if(contains(text,"HDA"))c=DIAG_HDA;
 else if(contains(text,"AHCI")||contains(text,"BLK")||contains(text,"PSF"))c=DIAG_STORAGE;
 else if(contains(text,"NET")||contains(text,"E1000"))c=DIAG_NETWORK;
 diag_log_impl(c,DIAG_INACTIVE,0,text);
}
void diag_get_summary(DiagSummary* out){
 if(!out)return;
 Guard guard;*out={};out->version=DIAG_ABI;out->size=sizeof(*out);
 out->serious=system_critical_event;
 out->keyboard=input_keyboards!=0;out->pointer=input_pointers!=0;
 out->errors=event_errors;out->warnings=event_warnings;
 out->generation=generation;out->devices=count;out->logs=log_count;
 out->dropped_devices=dropped_devices;out->dropped_logs=dropped_logs;
 for(uint32_t c=0;c<DIAG_CATEGORY_COUNT;++c)out->categories[c]=DIAG_NOT_FOUND;
 bool usb=false,usb_ok=false;
 for(uint32_t i=0;i<count;++i){const auto& d=devices[i];
  if(d.driver[0])++out->drivers;
  if(error(d.status)){if(out->errors!=UINT32_MAX)++out->errors;}else if(warning(d.status)){if(out->warnings!=UINT32_MAX)++out->warnings;}
  auto& state=out->categories[d.category];
  if(d.status==DIAG_OK||(state!=DIAG_OK&&priority(d.status)>priority(state)))state=d.status;
  if(d.category==DIAG_XHCI||d.category==DIAG_OHCI){usb=true;if(d.status==DIAG_OK)usb_ok=true;}
  if((d.flags&DIAG_FLAG_SYSTEM_REQUIRED)&&error(d.status))out->serious=1;
 }
 out->categories[DIAG_KEYBOARD]=out->keyboard?DIAG_OK:DIAG_NOT_FOUND;
 out->categories[DIAG_MOUSE]=out->pointer?DIAG_OK:DIAG_NOT_FOUND;
 if(out->categories[DIAG_VIRTIO_NET]==DIAG_OK)out->categories[DIAG_NETWORK]=DIAG_OK;
 if(out->categories[DIAG_VIRTIO_BLOCK]==DIAG_OK)out->categories[DIAG_STORAGE]=DIAG_OK;
 if(usb)out->categories[DIAG_USB]=usb_ok?DIAG_OK:DIAG_INIT_FAILED;
 if(!out->keyboard||!out->pointer||(usb&&!usb_ok))out->serious=1;
 if(!out->keyboard&&out->errors!=UINT32_MAX)++out->errors;
 if(!out->pointer&&out->errors!=UINT32_MAX)++out->errors;
 if((dropped_devices||dropped_logs)&&out->warnings!=UINT32_MAX)++out->warnings;
 out->status=out->serious?DIAG_CRITICAL:out->errors?DIAG_WARNING:out->warnings?DIAG_WARNING:DIAG_OK;
}
bool diag_get_device(uint32_t index,DiagDevice* out,bool driver){
 if(!out)return false;
 Guard guard;
 for(uint32_t i=0;i<count;++i){if(driver&&!devices[i].driver[0])continue;
  if(index==0){*out=devices[i];return true;}--index;}
 return false;
}
bool diag_get_log(uint32_t index,DiagLog* out){if(!out)return false;Guard guard;if(index>=log_count)return false;*out=logs[index];return true;}
#ifdef DIAG_HOST_TEST
void diag_test_reset(){Guard guard;count=log_count=generation=dropped_devices=dropped_logs=event_errors=event_warnings=input_keyboards=input_pointers=0;system_critical_event=false;for(auto& d:devices)d={};}
#endif

extern "C" void diag_withdraw_tree_impl(uint32_t id){
 if(!id)return;
 Guard guard;if(id>count)return;
 bool changed=false;
 for(uint32_t i=0;i<count;++i){uint32_t p=i+1;bool descendant=false;
  for(uint32_t n=0;p&&p<=count&&n<count;++n){if(p==id){descendant=true;break;}p=devices[p-1].parent;}
  auto& d=devices[i];if(!descendant||!(d.flags&DIAG_FLAG_DETECTED))continue;
  d.flags&=~DIAG_FLAG_DETECTED;if(!error(d.status))d.status=DIAG_INACTIVE;
  changed=true;
 }
 if(changed){++generation;append_locked(devices[id-1].category,DIAG_INACTIVE,0,"Urządzenie odłączone lub sterownik zatrzymany");}
}

extern "C" uint32_t diag_pci_parent_impl(uint8_t bus,uint8_t slot,uint8_t function){
 Guard guard;for(uint32_t i=0;i<count;++i){const auto& d=devices[i];
  if(d.category==DIAG_PCI&&d.pci_valid&&d.bus==bus&&d.pci_device==slot&&d.function==function)return d.id;}
 return 0;
}

extern "C" void diag_set_status_impl(uint32_t id,uint32_t status,uint32_t code,const char* message){
 if(!id||status>DIAG_CRITICAL)return;
 Guard guard;if(id>count)return;auto& d=devices[id-1];
 if(d.status==status&&d.code==code)return;
 d.status=status;d.code=code;if(message)d.flags|=safe_text(d.message,sizeof(d.message),message);
 ++generation;append_locked(d.category,status,code,d.message);
}

extern "C" void diag_input_counts_impl(uint32_t keyboards,uint32_t pointers){
 Guard guard;if(input_keyboards==keyboards&&input_pointers==pointers)return;
 input_keyboards=keyboards;input_pointers=pointers;++generation;
}
