#include "diagnostyka_systemu.h"
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*,size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*,const void*,size_t);
uint64_t diag_read_request(uint64_t command,uint64_t output,uint64_t capacity,uint64_t index,uint64_t version){
 if(version!=DIAG_ABI||!output||index>UINT32_MAX)return 0;
 const size_t size=command==DIAG_GET_SUMMARY?sizeof(DiagSummary):
  command==DIAG_GET_DEVICE||command==DIAG_GET_DRIVER?sizeof(DiagDevice):
  command==DIAG_GET_LOG?sizeof(DiagLog):0;
 if(!size||capacity<size||capacity>65536||output>UINT64_MAX-size||
    !czy_bezpieczny_zakres_uzytkownika_do_zapisu(reinterpret_cast<void*>(output),size))return 0;
 if(command==DIAG_GET_SUMMARY){if(index)return 0;DiagSummary s{};diag_get_summary(&s);
  return skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(output),&s,sizeof(s))?1:0;}
 if(command==DIAG_GET_LOG){DiagLog l{};if(!diag_get_log(uint32_t(index),&l))return 0;
  return skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(output),&l,sizeof(l))?1:0;}
 DiagDevice d{};if(!diag_get_device(uint32_t(index),&d,command==DIAG_GET_DRIVER))return 0;
 return skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(output),&d,sizeof(d))?1:0;
}
