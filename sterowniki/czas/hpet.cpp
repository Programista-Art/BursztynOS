#include "hpet.h"
#include "acpi.h"
#include "pamiec.h"
#include <stdint.h>
#include <stddef.h>
extern void wypisz_log(const char* tekst);
namespace {
constexpr uint64_t STRONA=4096, LIMIT_IDENTITY=4ULL*1024*1024*1024, HPET_VA=0x141000000ULL;
constexpr uint32_t REG_CAPS=0, REG_CONFIG=0x10, REG_COUNTER=0xF0;
constexpr uint64_t CONFIG_ENABLE=1, FEMTO_NA_SEKUNDE=1000000000000000ULL, NANO_NA_SEKUNDE=1000000000ULL;
constexpr uint32_t FLAGI_MMIO=VMM_FLAGA_PRESENT|VMM_FLAGA_ZAPIS|VMM_FLAGA_PWT|VMM_FLAGA_PCD;
volatile uint8_t* baza=nullptr; uint64_t fizyczny=0, okres_fs=0, hz=0;
bool licznik_64=false, gotowy=false; uint32_t ostatni32=0; uint64_t epoka32=0, ostatni_ns=0;
uint64_t irq_off(){uint64_t f;asm volatile("pushfq; popq %0; cli":"=r"(f)::"memory","cc");return f;}
void irq_restore(uint64_t f){if(f&(1ULL<<9))asm volatile("sti":::"memory");}
uint64_t reg64(uint32_t o){return *reinterpret_cast<volatile uint64_t*>(baza+o);}
void write64(uint32_t o,uint64_t v){*reinterpret_cast<volatile uint64_t*>(baza+o)=v;asm volatile("":::"memory");}
void app(char*o,size_t c,size_t*n,const char*s){while(s&&*s&&*n+1<c)o[(*n)++]=*s++;o[*n]='\0';}
void num(char*o,size_t c,size_t*n,uint64_t v,bool hex){char t[24];size_t m=0;uint32_t q=hex?16:10;do{uint32_t d=v%q;t[m++]=static_cast<char>(d<10?'0'+d:'A'+d-10);v/=q;}while(v&&m<sizeof(t));while(m&&*n+1<c)o[(*n)++]=t[--m];o[*n]='\0';}
void logv(const char*p,uint64_t v,const char*s,bool hex=false){char b[128]={};size_t n=0;app(b,sizeof(b),&n,p);num(b,sizeof(b),&n,v,hex);app(b,sizeof(b),&n,s);wypisz_log(b);}
uint64_t muldiv_reszta(uint64_t a,uint64_t b,uint64_t d){constexpr uint64_t K=1000000ULL;uint64_t hi=a/K,lo=a%K;uint64_t hb=hi*b;uint64_t q=hb/d,rem=hb%d;uint64_t tail=rem*K+lo*b;return q*K+tail/d;}
uint64_t tick_ns(uint64_t t){if(!hz)return 0;uint64_t sek=t/hz,rem=t%hz;if(sek>UINT64_MAX/NANO_NA_SEKUNDE)return UINT64_MAX;uint64_t q=NANO_NA_SEKUNDE/hz,r=NANO_NA_SEKUNDE%hz;uint64_t sub=rem*q;if(r)sub+=muldiv_reszta(rem,r,hz);return sek*NANO_NA_SEKUNDE+sub;}
bool test_wrap32(){uint32_t last=0xFFFFFFF0U;uint64_t epoch=0,prev=0;const uint32_t seq[]={0xFFFFFFF8U,4U,0x100U};for(uint32_t n:seq){if(n<last)epoch+=(1ULL<<32);last=n;uint64_t v=epoch|n;if(v<prev)return false;prev=v;}return prev==(1ULL<<32)+0x100U;}
}
bool hpet_inicjalizuj(){gotowy=false;uint64_t a=0;if(!acpi_pobierz_adres_hpet(&a)){wypisz_log("[HPET] Brak HPET w ACPI.");return false;}uint64_t str=a&~(STRONA-1),off=a-str;if(off>STRONA-0x400){wypisz_log("[HPET] Rejestry poza strona MMIO.");return false;}uint64_t va=a<LIMIT_IDENTITY?str:HPET_VA;ZmapujStrone(reinterpret_cast<void*>(va),reinterpret_cast<void*>(str),FLAGI_MMIO);asm volatile("invlpg (%0)"::"r"(va):"memory");baza=reinterpret_cast<volatile uint8_t*>(va+off);fizyczny=a;uint64_t caps=reg64(REG_CAPS);okres_fs=caps>>32;licznik_64=(caps&(1ULL<<13))!=0;if(!okres_fs||okres_fs>FEMTO_NA_SEKUNDE){wypisz_log("[HPET] Nieprawidlowy period.");baza=nullptr;return false;}hz=FEMTO_NA_SEKUNDE/okres_fs;if(!hz||hz>1000000000000ULL){wypisz_log("[HPET] Czestotliwosc poza bezpiecznym zakresem.");baza=nullptr;return false;}write64(REG_CONFIG,reg64(REG_CONFIG)&~CONFIG_ENABLE);write64(REG_COUNTER,0);ostatni32=0;epoka32=0;write64(REG_CONFIG,reg64(REG_CONFIG)|CONFIG_ENABLE);uint64_t x=reg64(REG_COUNTER);for(volatile uint32_t i=0;i<1000;++i)asm volatile("pause");if(reg64(REG_COUNTER)==x){wypisz_log("[HPET] Main counter nie postepuje.");baza=nullptr;return false;}gotowy=true;wypisz_log("[HPET] ACPI HPET znaleziony.");logv("[HPET] MMIO=0x",fizyczny,"",true);logv("[HPET] period=",okres_fs," fs");logv("[HPET] frequency=",hz," Hz");if(!licznik_64)wypisz_log("[HPET] Licznik 32-bit; obsluga wrap aktywna.");return true;}
bool hpet_dostepny(){return gotowy;}uint64_t hpet_adres_fizyczny(){return fizyczny;}uint64_t hpet_period_fs(){return okres_fs;}uint64_t hpet_czestotliwosc_hz(){return hz;}
uint64_t hpet_odczytaj_tick(){if(!gotowy||!baza)return 0;if(licznik_64)return reg64(REG_COUNTER);uint64_t f=irq_off();uint32_t n=static_cast<uint32_t>(reg64(REG_COUNTER));if(n<ostatni32)epoka32+=(1ULL<<32);ostatni32=n;uint64_t w=epoka32|n;irq_restore(f);return w;}
uint64_t czas_monotoniczny_ns(){uint64_t n=tick_ns(hpet_odczytaj_tick()),f=irq_off();if(n<ostatni_ns)n=ostatni_ns;else ostatni_ns=n;irq_restore(f);return n;}uint64_t czas_monotoniczny_us(){return czas_monotoniczny_ns()/1000;}uint64_t czas_monotoniczny_ms(){return czas_monotoniczny_ns()/1000000;}
bool hpet_test_wrap_diagnostyczny(){return test_wrap32();}
bool hpet_czekaj_ns_boot(uint64_t ns){if(!gotowy||!ns)return gotowy;uint64_t s=czas_monotoniczny_ns(),d=s>UINT64_MAX-ns?UINT64_MAX:s+ns,p=s;for(uint64_t i=0;i<100000000ULL;++i){uint64_t n=czas_monotoniczny_ns();if(n>=d)return true;if(n<p)return false;p=n;asm volatile("pause");}return false;}
