#include <initializer_list>
#include "idt.h"
#include <cassert>
#include <cstdio>
static bool enabled;
static unsigned saves,restores;
unsigned long host_idt_irq_save(){++saves;const unsigned long f=enabled?512:0;enabled=false;return f;}
void host_idt_irq_restore(unsigned long f){++restores;enabled=(f&512)!=0;}
static void handler(void*){}
int main(){unsigned checks=0;
#define VERIFY(x) do{++checks;assert(x);}while(0)
    for(bool prior:{false,true}) {
        enabled=prior;
        int vectors[207];
        for(int& v:vectors){v=idt_alokuj_wektor_irq();VERIFY(v>=48&&v<=254&&v!=44);VERIFY(enabled==prior);VERIFY(idt_zarejestruj_irq(v,handler,&v));VERIFY(!idt_zarejestruj_irq(v,handler,nullptr));VERIFY(enabled==prior);}
        VERIFY(idt_alokuj_wektor_irq()==-1);VERIFY(enabled==prior);
        VERIFY(idt_zaalokowane_wektory_liczba()==207);
        VERIFY(!idt_zarejestruj_irq(44,handler,nullptr));VERIFY(!idt_zwolnij_wektor_irq(44));
        for(int v:vectors){VERIFY(idt_wyrejestruj_irq(v));VERIFY(idt_zwolnij_wektor_irq(v));VERIFY(!idt_zwolnij_wektor_irq(v));VERIFY(enabled==prior);}
        VERIFY(idt_zaalokowane_wektory_liczba()==0);VERIFY(saves==restores);
    }
    printf("Production IRQ allocator uniqueness/exhaustion/IF restore: %u checks PASS\n",checks);
}
