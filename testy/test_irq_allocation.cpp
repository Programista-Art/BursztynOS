#include <initializer_list>
#include "idt.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
static bool enabled;
static unsigned saves,restores;
unsigned long host_idt_irq_save(){++saves;const unsigned long f=enabled?512:0;enabled=false;return f;}
void host_idt_irq_restore(unsigned long f){++restores;enabled=(f&512)!=0;}
static void handler(void*){}
volatile uint32_t* baza_lapic_wirtualna=nullptr;   // atrapa rejestrow LAPIC (idt.cpp: extern)
void idt_host_test_eoi(uint8_t wektor);
int main(){unsigned checks=0;
#define VERIFY(x) do{++checks;assert(x);}while(0)
    for(bool prior:{false,true}) {
        enabled=prior;
        constexpr int ROZMIAR_PULI=IDT_OSTATNI_WEKTOR_DYNAMICZNY-IDT_PIERWSZY_WEKTOR_DYNAMICZNY+1;
        static_assert(IDT_WEKTOR_ODDANIA_CPU>IDT_OSTATNI_WEKTOR_DYNAMICZNY,"wektor oddania CPU poza pula");
        int vectors[ROZMIAR_PULI];
        for(int& v:vectors){v=idt_alokuj_wektor_irq();VERIFY(v>=IDT_PIERWSZY_WEKTOR_DYNAMICZNY&&v<=IDT_OSTATNI_WEKTOR_DYNAMICZNY&&v!=44&&v!=IDT_WEKTOR_ODDANIA_CPU);VERIFY(enabled==prior);VERIFY(idt_zarejestruj_irq(v,handler,&v));VERIFY(!idt_zarejestruj_irq(v,handler,nullptr));VERIFY(enabled==prior);}
        VERIFY(idt_alokuj_wektor_irq()==-1);VERIFY(enabled==prior);
        VERIFY(idt_zaalokowane_wektory_liczba()==ROZMIAR_PULI);
        VERIFY(!idt_zarejestruj_irq(44,handler,nullptr));VERIFY(!idt_zwolnij_wektor_irq(44));
        VERIFY(!idt_zarejestruj_irq(IDT_WEKTOR_ODDANIA_CPU,handler,nullptr));VERIFY(!idt_zwolnij_wektor_irq(IDT_WEKTOR_ODDANIA_CPU));
        for(int v:vectors){VERIFY(idt_wyrejestruj_irq(v));VERIFY(idt_zwolnij_wektor_irq(v));VERIFY(!idt_zwolnij_wektor_irq(v));VERIFY(enabled==prior);}
        VERIFY(idt_zaalokowane_wektory_liczba()==0);VERIFY(saves==restores);
    }
    // Wektor 253 (programowe oddanie CPU): decyzja przelaczenia i EOI przy przypadkowym dostarczeniu sprzetowym.
    VERIFY(!idt_oddanie_cpu_przelacz(false,1,64,true));   // przerwany Ring 3: nigdy (i tak #GP przez DPL 0)
    VERIFY(!idt_oddanie_cpu_przelacz(true,0,64,true));    // PID 0
    VERIFY(!idt_oddanie_cpu_przelacz(true,-1,64,true)&&!idt_oddanie_cpu_przelacz(true,64,64,true));
    VERIFY(!idt_oddanie_cpu_przelacz(true,1,64,false));   // bez jawnego zezwolenia procesu
    VERIFY(idt_oddanie_cpu_przelacz(true,63,64,true));
    {
        static uint32_t lapic[0x400/4];
        baza_lapic_wirtualna=lapic;
        const unsigned EOI=0xB0/4, ISR7=0x170/4;           // ISR grupa 7 = wektory 224..255
        lapic[EOI]=0xDEADu; idt_host_test_eoi(IDT_WEKTOR_ODDANIA_CPU);
        VERIFY(lapic[EOI]==0xDEADu);                       // programowe int $253: bit ISR pusty -> brak EOI
        lapic[ISR7]=1u<<(IDT_WEKTOR_ODDANIA_CPU%32); idt_host_test_eoi(IDT_WEKTOR_ODDANIA_CPU);
        VERIFY(lapic[EOI]==0);                             // sprzetowe 253: EOI wyslane, LAPIC nie zablokowany
        lapic[EOI]=0xDEADu; lapic[ISR7]=1u<<(254%32); idt_host_test_eoi(IDT_WEKTOR_ODDANIA_CPU);
        VERIFY(lapic[EOI]==0xDEADu);                       // bit innego wektora nie wyzwala EOI dla 253
        baza_lapic_wirtualna=nullptr;
    }
    printf("Production IRQ allocator uniqueness/exhaustion/IF restore: %u checks PASS\n",checks);
}
