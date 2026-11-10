#include "fuzz_engine.h"
#include "../../bur_v2.h"
#include <cassert>
#include <vector>

static void target(const uint8_t* data,size_t size) {
    (void)bur_v2_waliduj_plik(data,size,ASLR_PIE_WINDOW_BASE);
    if(size<sizeof(NaglowekBurV2))return;
    std::vector<uint8_t> copy(data,data+size);
    NaglowekBurV2 n;memcpy(&n,copy.data(),sizeof(n));
    n.suma_kontrolna_naglowka=bur_v2_oblicz_sume_naglowka(n);
    memcpy(copy.data(),&n,sizeof(n));
    if(bur_v2_waliduj_plik(copy.data(),copy.size(),ASLR_PIE_WINDOW_BASE)!=BUR_V2_OK)return;
    // Exercise production relocation writes in a bounded private DATA buffer.
    if(n.dane_rozmiar_pamieci>1024*1024)return;
    std::vector<uint8_t> writable(n.dane_rozmiar_pamieci,0);
    memcpy(writable.data(),copy.data()+n.dane_przesuniecie,n.dane_rozmiar_pliku);
    assert(bur_v2_zastosuj_relokacje_danych(n,
        reinterpret_cast<const BurV2Relokacja*>(copy.data()+n.reloc_przesuniecie),
        writable.data(),ASLR_PIE_WINDOW_BASE));
}

int main(int argc,char**argv) {
    FuzzEngine engine("fuzz_bur_v2",0x42555202,32768);
    engine.parse_args(argc,argv);
    NaglowekBurV2 n{};bur_v2_ustaw_domyslne_pola_naglowka(&n);
    n.flagi=31;n.wymagane_funkcje=7;
    n.punkt_wejscia_rva=n.tekst_rva=n.tekst_przesuniecie=4096;
    n.tekst_rozmiar=4096;n.offset_straznika_stosu=8192;
    n.dane_przesuniecie=8192;n.dane_rva=12288;
    n.dane_rozmiar_pliku=n.dane_rozmiar_pamieci=4096;
    n.reloc_przesuniecie=12288;n.reloc_liczba=1;n.reloc_rozmiar_wpisu=24;
    n.domyślne_strony_stosu=4;n.suma_kontrolna_naglowka=bur_v2_oblicz_sume_naglowka(n);
    std::vector<uint8_t> seed(12312,0);memcpy(seed.data(),&n,sizeof(n));
    BurV2Relokacja r{12288,4096,BUR_V2_RELOC_ABS64,0};
    memcpy(seed.data()+12288,&r,sizeof(r));engine.add_seed(seed);
    for(uint32_t type : {0U,3U,4U,5U,0xffffffffU}) {
        r.typ=type;memcpy(seed.data()+12288,&r,sizeof(r));engine.add_regression(seed);
    }
    return engine.run(target);
}
