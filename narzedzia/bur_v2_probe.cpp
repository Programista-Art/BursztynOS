// Isolated guest test payload; never packaged by the production Makefile.
#include "bws_api.h"
#include "bursztyn_gui.h"
#include "stack_protector.h"
extern "C" uint64_t bws_wywolaj(uint64_t nr, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    register uint64_t r8 asm("r8") = nr;
    register uint64_t r9 asm("r9") = a;
    register uint64_t r10 asm("r10") = b;
    register uint64_t r12 asm("r12") = c;
    register uint64_t r13 asm("r13") = d;
    uint64_t result;
    asm volatile("syscall" : "=a"(result) : "r"(r8), "r"(r9), "r"(r10), "r"(r12), "r"(r13) : "rcx", "r11", "memory", "cc");
    return result;
}
extern "C" {
__attribute__((section(".data.ssp_guard"),used)) uintptr_t __stack_chk_guard = 0;
[[noreturn]] __attribute__((no_stack_protector)) void __stack_chk_fail() {
    bws_wywolaj(BWS_ZAKONCZ_PROCES, BWS_EXIT_STACK_PROTECTOR_VIOLATION,0,0,0);
    for (;;) asm volatile("ud2");
}
}
static void say(const char* s) { bws_wywolaj(BWS_WYPISZ,(uint64_t)s,0,0,0); }
static void value(uint64_t n) {
    char s[19] = "0x0000000000000000";
    for (int i=17;i>=2;--i) { s[i]="0123456789abcdef"[n&15]; n>>=4; }
    say(s);
}
static void callback() { say("[BUR2 CALLBACK OK]\n"); }
static void (*volatile relocated_callback)() = callback;
__attribute__((noinline)) static void smash() {
    volatile char buffer[16]{};
    volatile char* p=buffer;
    asm volatile("" : "+r"(p) :: "memory");
    for(unsigned i=0;i<64;++i) p[i]='A';
    asm volatile("" :: "r"(buffer) : "memory");
}
extern "C" [[noreturn]] void _start() {
    volatile uint64_t local=123;
    relocated_callback();
    auto heap=bws_wywolaj(BWS_ALOKUJ_STERTE,4096,0,0,0);
    if (!heap || !__stack_chk_guard) { say("[BUR2 FAIL]\n"); __stack_chk_fail(); }
    *reinterpret_cast<uint64_t*>(heap)=456;
    uint64_t object=bws_wywolaj(BWS_SHM_CREATE,4096,3,0,0);
    uint64_t shm=object ? bws_wywolaj(BWS_SHM_MAP,object,3,0,0) : 0;
    say("[BUR2 LAYOUT] code=");value((uint64_t)&_start);
    say(" stack=");value((uint64_t)&local);
    say(" heap=");value(heap);
    say(" guard=");value((uint64_t)&__stack_chk_guard);
    say(" shm=");value(shm);say("\n[BUR2 EXEC PASS]\n");
    if(shm)bws_wywolaj(BWS_SHM_UNMAP,shm,0,0,0);
    if(object)bws_wywolaj(BWS_SHM_CLOSE,object,0,0,0);
    char mode[32]{};
    bws_wywolaj(BWS_ARGUMENT_STARTOWY,(uint64_t)mode,sizeof(mode),0,0);
    if(mode[0]) {
        say("[BUR2 NEGATIVE] ");say(mode);say("\n");
        if(mode[0]=='g') *reinterpret_cast<volatile uintptr_t*>(&__stack_chk_guard)=0;
        if(mode[0]=='t') *reinterpret_cast<volatile uint8_t*>(&_start)=0;
        if(mode[0]=='r') *reinterpret_cast<volatile uint8_t*>(const_cast<char*>("read-only"))=0;
        if(mode[0]=='h') { *reinterpret_cast<uint8_t*>(heap)=0xc3; reinterpret_cast<void(*)()>(heap)(); }
        if(mode[0]=='s') { volatile uint8_t code=0xc3; reinterpret_cast<void(*)()>((uintptr_t)&code)(); }
        if(mode[0]=='c') smash();
        say("[BUR2 NEGATIVE FAILED]\n");
    }
    bws_wywolaj(BWS_ZAKONCZ_PROCES,0,0,0,0);
    for (;;) asm volatile("ud2");
}
