// Native diagnostic app, never callable through the JavaScript bridge.
#include "bursztyn_gui.h"
#include "fpu.h"
extern "C" [[noreturn]] void _start();
struct __attribute__((packed)) Header {uint8_t magic[4];uint64_t entry,text_offset,text_size,text_va,data_offset,data_size,data_va;};
extern "C" {__attribute__((section(".naglowek"),used)) Header naglowek={{'B','U','R',0},reinterpret_cast<uint64_t>(&_start),0x1000,0x10000,0x1001000,0x11000,0x80000,0x1011000};}
static bool identical(const FpuState &a,const FpuState &b){for(unsigned i=0;i<416;++i)if(a.bytes[i]!=b.bytes[i])return false;return true;}
static void wait_tick(){bws_zdarzenie e{};gui_czekaj_na_zdarzenie_z_timeoutem(&e,1);}
extern "C" [[noreturn]] void _start(){
    FpuState fresh,actual,expected;fpu_defaults(fresh);fpu_defaults(actual);fpu_defaults(expected);fpu_save(actual);
    bool clean=actual.bytes[0]==0x7f&&actual.bytes[1]==3&&actual.bytes[4]==0&&actual.bytes[24]==0x80&&actual.bytes[25]==0x1f;
    for(unsigned i=32;i<416;++i)if(actual.bytes[i])clean=false;
    char arg[64]{};pobierz_argument_startowy(arg,sizeof(arg));bool follower=arg[0]=='b'||arg[0]=='f';
    if(!clean){wypisz("[FPU PROBE] FAIL fresh state\n");bws_zakoncz_proces();}
    if(!follower&&!uruchom_program_z_argumentem_uzytkownika("/programy/fpu-b.cebula/fpu-b.bur","b")){wypisz("[FPU PROBE] FAIL spawn B\n");bws_zakoncz_proces();}
    fpu_restore(fresh);if(follower)asm volatile("fldpi");else asm volatile("fld1");fpu_save(expected);
    for(unsigned i=160;i<416;++i)expected.bytes[i]=uint8_t(i*(follower?29:13)+(follower?1:0));expected.bytes[25]=follower?0x5f:0x3f;expected.bytes[1]=follower?0x0b:0x07;fpu_restore(expected);
    if(arg[0]=='f'){asm volatile("ud2");}
    bool ok=true;
    for(unsigned i=0;i<48;++i){
        wait_tick();fpu_save(actual);if(!identical(expected,actual)){ok=false;break;}
        for(unsigned j=0;j<10000;++j)asm volatile("pause");fpu_save(actual);if(!identical(expected,actual)){ok=false;break;}
    }
    fpu_restore(fresh);
    if(!ok){wypisz("[FPU PROBE] FAIL isolation\n");bws_zakoncz_proces();}
    if(follower){wypisz("[FPU PROBE] B PASS\n");bws_zakoncz_proces();}
    for(unsigned i=0;i<32;++i)wait_tick();
    if(!uruchom_program_z_argumentem_uzytkownika("/programy/fpu-b.cebula/fpu-b.bur","f")){wypisz("[FPU PROBE] FAIL fault spawn\n");bws_zakoncz_proces();}
    for(unsigned i=0;i<32;++i)wait_tick();
    if(!uruchom_program_z_argumentem_uzytkownika("/programy/fpu-b.cebula/fpu-b.bur","b")){wypisz("[FPU PROBE] FAIL reuse\n");bws_zakoncz_proces();}
    wypisz("[FPU PROBE] A PASS\n");bws_zakoncz_proces();
}
