#include "blokowy.h"
#include "ahci.h"
#include "pamiec.h"
#include "grafika.h"
#include "instalator.h"

namespace {
Storage::Device devices[32]{};
Storage::Table table; // duże struktury poza 16 KiB stosem jadra
int installation_target=-1;
bool rd(void*ctx,uint64_t lba,uint32_t n,void*b){return ahci_czytaj(uintptr_t(ctx),lba,n,b);}
bool wr(void*ctx,uint64_t lba,uint32_t n,const void*b){
    if(instalator_tryb()&&installation_target!=int(uintptr_t(ctx)))return false;
    return ahci_zapisz(uintptr_t(ctx),lba,n,b);
}
bool fl(void*ctx){return ahci_flush(uintptr_t(ctx));}
}
void blokowy_inicjalizuj(){
    for(uint32_t i=0;i<ahci_liczba_dyskow();++i){AhciDysk d{};if(ahci_dysk(i,&d))devices[i]={(void*)uintptr_t(i),d.sektory,d.sektor_bajty,d.zapis,rd,wr,fl};}
}
Storage::Device* blokowy_dysk(uint32_t i){return i<ahci_liczba_dyskow()?&devices[i]:nullptr;}
bool blokowy_znajdz_bsp(uint32_t ordinal,Storage::Partition*out){
    if(!out)return false;
    uint32_t found=0;
    for(uint32_t d=0;d<ahci_liczba_dyskow();++d){auto*dev=blokowy_dysk(d);
        if(!Storage::gpt_read(dev,&table))continue;
        for(uint32_t i=0;i<table.count;++i){const auto&e=table.entries[i];
            if(!Storage::equal(e.type,Storage::BspType,16))continue;
            Storage::Partition p{dev,e.start,e.count};
            if(!Storage::complete(p,table.uuid)){klog_serial("[BSP] Pomijam nieukonczona/uszkodzona instalacje.");continue;}
            if(found++==ordinal){*out=p;klog_serial("[BSP] Wybrano kompletny wolumin GPT (kolejnosc HBA/port/wpis).");return true;}
        }
    }
    return false;
}
void blokowy_zezwol_instalatorowi(uint32_t id,bool enabled){installation_target=enabled?int(id):-1;}
bool blokowy_zapis_instalatora(){return installation_target>=0;}
