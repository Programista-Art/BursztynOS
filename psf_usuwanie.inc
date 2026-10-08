// Shared GUI service. Included by psf.cpp; no disk-format or primitive change.
namespace {
struct ZadanieUsuwania {
    uint64_t id, rodzic;
    uint32_t glebokosc, odwolania;
    char sciezka[512];
};
struct PlanUsuwania {
    ZadanieUsuwania* wpisy;
    uint8_t* bloki=nullptr;
    ~PlanUsuwania() { kfree(bloki);kfree(wpisy); }
};
// Visit data and indirect-table blocks with bounded loops, never recursion.
// Validate before dereferencing: a corrupt pointer must not reach free().
template<class Visit> bool odwiedz_bloki_usuwania(const wezel_indeksowy* w,Visit visit) {
    if(w->blok_posredni_3!=BARK_BLOKU)return false;
    if(w->typ==TYP_KATALOG && (w->blok_posredni_1!=BARK_BLOKU || w->blok_posredni_2!=BARK_BLOKU))return false;
    auto block=[&](uint64_t id) {
        return id==BARK_BLOKU || (id_bloku_danych_poprawne(id) && czy_blok_danych_zajety(id) && visit(id));
    };
    for(uint32_t i=0;i<PSF_MAX_BLOKOW_W_WEZLE;++i)if(!block(w->wskazniki_blokow[i]))return false;
    if(w->blok_posredni_1!=BARK_BLOKU) {
        if(!block(w->blok_posredni_1))return false;
        const auto* table=reinterpret_cast<const uint64_t*>(pobierz_blok_danych(w->blok_posredni_1));
        if(!table)return false;
        for(uint64_t i=0;i<PTRS_PER_BLOCK;++i)if(!block(table[i]))return false;
    }
    if(w->blok_posredni_2!=BARK_BLOKU) {
        if(!block(w->blok_posredni_2))return false;
        const auto* table=reinterpret_cast<const uint64_t*>(pobierz_blok_danych(w->blok_posredni_2));
        if(!table)return false;
        for(uint64_t i=0;i<PTRS_PER_BLOCK;++i)if(table[i]!=BARK_BLOKU) {
            if(!block(table[i]))return false;
            const auto* child=reinterpret_cast<const uint64_t*>(pobierz_blok_danych(table[i]));
            if(!child)return false;
            for(uint64_t j=0;j<PTRS_PER_BLOCK;++j)if(!block(child[j]))return false;
        }
    }
    return true;
}
bool sciezka_usuwania(const char* src, char* dst) {
    if(!src || src[0]!='/')return false;
    size_t out=1, pos=1;dst[0]='/';
    while(pos<512 && src[pos]) {
        while(pos<512 && src[pos]=='/')++pos;
        if(pos==512)return false;
        if(!src[pos])break;
        char name[PSF_MAX_NAZWA]{};size_t n=0;
        while(pos<512 && src[pos] && src[pos]!='/') {
            if(n+1>=sizeof(name))return false;
            name[n++]=src[pos++];
        }
        if(!poprawna_nazwa(name) || out+(out>1)+n>=512)return false;
        if(out>1)dst[out++]='/';
        for(size_t i=0;i<n;++i)dst[out++]=name[i];
    }
    if(pos==512)return false;
    dst[out]=0;return true;
}
WynikPlikowy autoryzuj_usuniecie(const ZadanieUsuwania& job,
                               PsfAutoryzacjaUsuwania auth, void* ctx) {
    const auto* w=pobierz_wezel(job.id);const auto* p=pobierz_wezel(job.rodzic);
    if(!w || !p || p->typ!=TYP_KATALOG ||
       (w->typ!=TYP_KATALOG && w->typ!=TYP_PLIK))return WynikPlikowy::Uszkodzony;
    if(rozwiaz_sciezke(job.sciezka,nullptr,false)!=job.id)return WynikPlikowy::Brak;
    psf_metadane wm{},pm{};metadane_wezla(w,&wm);metadane_wezla(p,&pm);
    return auth(job.sciezka,wm,pm,ctx);
}
}

extern "C" WynikPlikowy psf_usun_drzewo(const char* root,
                                      PsfAutoryzacjaUsuwania auth, void* ctx) {
    BlokadaPSF lock;
    if(!psf_gotowy || !dysk_superblok)return WynikPlikowy::BladIO;
    if(!auth)return WynikPlikowy::BrakUprawnien;
    char canonical[512]{};
    if(!sciezka_usuwania(root,canonical))return WynikPlikowy::NieprawidlowaNazwa;
    if(canonical[1]==0)return WynikPlikowy::Chroniony;
    const uint64_t id=rozwiaz_sciezke(canonical,nullptr,false);
    if(!id)return WynikPlikowy::Brak;
    PlanUsuwania plan{static_cast<ZadanieUsuwania*>(kmalloc(
        sizeof(ZadanieUsuwania)*PSF_USUN_MAX_WEZLOW))};
    if(!plan.wpisy)return WynikPlikowy::BrakPamieci;
    const uint64_t bitmap_size=calkowita_liczba_blokow_danych/8+1;
    if(bitmap_size>SIZE_MAX)return WynikPlikowy::Limit;
    plan.bloki=static_cast<uint8_t*>(kmalloc(static_cast<size_t>(bitmap_size)));
    if(!plan.bloki)return WynikPlikowy::BrakPamieci;
    for(size_t i=0;i<bitmap_size;++i)plan.bloki[i]=0;
    auto* jobs=plan.wpisy;uint32_t count=1;
    jobs[0]={};jobs[0].id=id;
    char root_name[PSF_MAX_NAZWA]{};
    jobs[0].rodzic=rozwiaz_sciezke(canonical,root_name,true);
    for(size_t i=0;;++i){jobs[0].sciezka[i]=canonical[i];if(!canonical[i])break;}
    // Breadth-first preflight, then reverse order gives children-before-parent.
    // No C++ recursion and no mutations until every bound/policy check passes.
    for(uint32_t at=0;at<count;++at) {
        auto& job=jobs[at];
        const auto allowed=autoryzuj_usuniecie(job,auth,ctx);
        if(allowed!=WynikPlikowy::Ok)return allowed;
        auto* w=pobierz_wezel(job.id);
        if(!odwiedz_bloki_usuwania(w,[&](uint64_t b) {
            const uint8_t mask=uint8_t(1U<<(b%8));
            if(plan.bloki[b/8]&mask)return false;
            plan.bloki[b/8]|=mask;return true;
        }))return WynikPlikowy::Uszkodzony;
        if(w->typ!=TYP_KATALOG)continue;
        uint64_t children=0;
        for(uint32_t b=0;b<PSF_MAX_BLOKOW_W_WEZLE;++b) {
            const uint64_t block=w->wskazniki_blokow[b];
            if(block==BARK_BLOKU)continue;
            if(!id_bloku_danych_poprawne(block) || !czy_blok_danych_zajety(block))return WynikPlikowy::Uszkodzony;
            const auto* entries=pobierz_wpisy_katalogowe(block);
            if(!entries)return WynikPlikowy::BladIO;
            for(uint32_t e=0;e<wpisow_katalogowych_na_blok();++e) {
                const auto& entry=entries[e];if(!entry.id_wezla)continue;
                ++children;
                if(!poprawna_nazwa(entry.nazwa) || entry.id_wezla==dysk_superblok->id_korzenia)
                    return WynikPlikowy::Uszkodzony;
                for(uint32_t j=0;j<count;++j)if(jobs[j].id==entry.id_wezla)return WynikPlikowy::Uszkodzony;
                if(count==PSF_USUN_MAX_WEZLOW || job.glebokosc==PSF_USUN_MAX_GLEBOKOSC)
                    return WynikPlikowy::Limit;
                auto& next=jobs[count++];next={};next.id=entry.id_wezla;
                next.rodzic=job.id;next.glebokosc=job.glebokosc+1;
                size_t n=0;while(job.sciezka[n])++n;
                size_t len=0;while(entry.nazwa[len])++len;
                if(n+1+len>=sizeof(next.sciezka))return WynikPlikowy::Limit;
                for(size_t j=0;j<n;++j)next.sciezka[j]=job.sciezka[j];
                next.sciezka[n++]='/';for(size_t j=0;j<=len;++j)next.sciezka[n+j]=entry.nazwa[j];
                if(rozwiaz_sciezke(next.sciezka,nullptr,false)!=entry.id_wezla)return WynikPlikowy::Uszkodzony;
            }
        }
        if(children!=w->rozmiar_w_bajtach)return WynikPlikowy::Uszkodzony;
    }
    // BSP2 has no parent pointers or hardlinks. Reject aliases, including an
    // inode also referenced outside the selected subtree, before freeing it.
    for(uint64_t d=1;d<=calkowita_liczba_wezlow;++d) {
        const auto* w=pobierz_wezel(d);if(!w)return WynikPlikowy::BladIO;
        if(w->typ==TYP_PLIK || w->typ==TYP_KATALOG) {
            bool selected=false;
            for(uint32_t j=0;j<count;++j)if(jobs[j].id==d){selected=true;break;}
            if(!selected && !odwiedz_bloki_usuwania(w,[&](uint64_t b) {
                return !(plan.bloki[b/8] & uint8_t(1U<<(b%8)));
            }))return WynikPlikowy::Uszkodzony;
        }
        if(w->typ!=TYP_KATALOG)continue;
        for(uint32_t b=0;b<PSF_MAX_BLOKOW_W_WEZLE;++b) {
            const uint64_t block=w->wskazniki_blokow[b];if(block==BARK_BLOKU)continue;
            if(!id_bloku_danych_poprawne(block) || !czy_blok_danych_zajety(block))return WynikPlikowy::Uszkodzony;
            const auto* entries=pobierz_wpisy_katalogowe(block);if(!entries)return WynikPlikowy::BladIO;
            for(uint32_t e=0;e<wpisow_katalogowych_na_blok();++e)if(entries[e].id_wezla)
                for(uint32_t j=0;j<count;++j)if(jobs[j].id==entries[e].id_wezla && ++jobs[j].odwolania>1)
                    return WynikPlikowy::Uszkodzony;
        }
    }
    for(uint32_t i=0;i<count;++i)if(jobs[i].odwolania!=1)return WynikPlikowy::Uszkodzony;
    uint32_t removed=0;
    while(count) {
        const auto& job=jobs[--count];
        const auto allowed=autoryzuj_usuniecie(job,auth,ctx);
        if(allowed!=WynikPlikowy::Ok)return removed?WynikPlikowy::Czesciowo:allowed;
        const bool ok=usun_twor_bez_blokady(job.sciezka);
        if(ok)++removed;
        // The legacy primitive commits RAM even if persistence fails. Report
        // that honestly; never delete another sibling after a failed commit.
        if(!ok || !ostatni_flush_ok)return removed?WynikPlikowy::Czesciowo:WynikPlikowy::BladIO;
    }
    return WynikPlikowy::Ok;
}

extern "C" WynikPlikowy psf_usun_wynik(const char* path) {
    BlokadaPSF lock;
    if(!psf_gotowy)return WynikPlikowy::BladIO;
    char canonical[512]{};
    if(!sciezka_usuwania(path,canonical))return WynikPlikowy::NieprawidlowaNazwa;
    if(canonical[1]==0)return WynikPlikowy::Chroniony;
    const auto* w=pobierz_wezel(rozwiaz_sciezke(canonical,nullptr,false));
    if(!w || w->typ==TYP_WOLNY)return WynikPlikowy::Brak;
    if(w->typ==TYP_KATALOG && w->rozmiar_w_bajtach)return WynikPlikowy::Niepusty;
    if(!usun_twor_bez_blokady(canonical) || !ostatni_flush_ok)return WynikPlikowy::BladIO;
    return WynikPlikowy::Ok;
}
