#pragma once
#include "diagnostyka_systemu_abi.h"
// Weak references keep stand-alone driver fixtures independent of the kernel.
// The kernel always links the implementation; no allocation or GUI dependency.
extern "C" uint32_t diag_publish_impl(uint64_t key, const DiagDevice*) __attribute__((weak));
extern "C" void diag_log_impl(uint32_t category, uint32_t severity, uint32_t code, const char*) __attribute__((weak));
inline uint32_t diag_publish(uint64_t key,const DiagDevice& d) {
 return diag_publish_impl ? diag_publish_impl(key,&d) : 0;
}
inline void diag_log(uint32_t c,uint32_t s,uint32_t code,const char* text) {
 if(diag_log_impl) diag_log_impl(c,s,code,text);
}
inline DiagDevice diag_device(uint32_t category,const char* name,const char* driver,
 uint32_t status,bool detected,const char* message) {
 DiagDevice d{}; d.version=DIAG_ABI;d.size=sizeof(d);d.category=category;
 d.status=status;d.flags=detected?uint32_t(DIAG_FLAG_DETECTED):0U;
 const char* src[]={name,driver,message};char* dst[]={d.name,d.driver,d.message};
 const size_t cap[]={sizeof(d.name),sizeof(d.driver),sizeof(d.message)};
 for(unsigned j=0;j<3;++j)if(src[j]){size_t i=0;for(;src[j][i]&&i+1<cap[j];++i)dst[j][i]=src[j][i];dst[j][i]=0;}
 return d;
}
inline uint32_t diag_simple(uint32_t c,const char* n,const char* driver,uint32_t s,
 bool detected,const char* msg,uint64_t key=0) {return diag_publish(key,diag_device(c,n,driver,s,detected,msg));}
extern "C" void diag_input_counts_impl(uint32_t,uint32_t) __attribute__((weak));
inline void diag_input_counts(uint32_t keyboards,uint32_t pointers){if(diag_input_counts_impl)diag_input_counts_impl(keyboards,pointers);}
extern "C" void diag_set_status_impl(uint32_t,uint32_t,uint32_t,const char*) __attribute__((weak));
inline void diag_set_status(uint32_t id,uint32_t status,uint32_t code,const char* message){if(diag_set_status_impl)diag_set_status_impl(id,status,code,message);}
extern "C" uint32_t diag_pci_parent_impl(uint8_t,uint8_t,uint8_t) __attribute__((weak));
inline uint32_t diag_pci_parent(uint8_t bus,uint8_t slot,uint8_t function){return diag_pci_parent_impl?diag_pci_parent_impl(bus,slot,function):0;}
extern "C" void diag_withdraw_tree_impl(uint32_t) __attribute__((weak));
inline void diag_withdraw_tree(uint32_t id){if(diag_withdraw_tree_impl)diag_withdraw_tree_impl(id);}
void diag_get_summary(DiagSummary*);
bool diag_get_device(uint32_t index,DiagDevice*,bool driver=false);
bool diag_get_log(uint32_t index,DiagLog*);
void diag_capture_serial(const char*) __attribute__((weak));
uint64_t diag_read_request(uint64_t command,uint64_t output,uint64_t capacity,uint64_t index,uint64_t version);
void diag_hda_boot_result(bool);
void diag_boot_services_ready();
void diag_desktop_ready();
void diag_service();
#ifdef DIAG_HOST_TEST
void diag_test_reset();
#endif
