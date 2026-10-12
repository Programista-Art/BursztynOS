bool scisla_instalacja=false,pliki_instalacja_ok=true;
bool ekran_aplikacja_instalacja_ok=false;
bool tapety_aplikacja_instalacja_ok=false;
struct PlikKontrolny{char path[128];uint32_t size,crc;};
PlikKontrolny pliki_kontrolne[96]{};
unsigned liczba_plikow_kontrolnych=0;
struct PlanowanyPlik{char path[128];const char*data;uint32_t size;};
PlanowanyPlik planowane_pliki[96]{};
unsigned planowane_liczba=0,planowane_indeks=0;
size_t planowane_katalog_indeks=0;
uint32_t planowane_offset=0,planowane_crc=~0U;
uint64_t planowane_bajty=0,planowane_bajty_razem=0;
bool planowanie_instalacji=false,instalacja_krokowa=false;


bool dlugosc_tekstu_u32(const char* tekst,
                        uint32_t* wynik) {
    if (!tekst || !wynik)
        return false;

    uint64_t dlugosc = 0;

    while (tekst[dlugosc] != '\0') {
        ++dlugosc;

        if (dlugosc > UINT32_MAX)
            return false;
    }

    *wynik =
        static_cast<uint32_t>(dlugosc);

    return true;
}
bool pobierz_rozmiar_bloba(
    const uint8_t* poczatek,
    const uint8_t* koniec,
    uint32_t* wynik) {

    if (!poczatek || !koniec || !wynik)
        return false;

    const uintptr_t start =
        reinterpret_cast<uintptr_t>(
            poczatek);

    const uintptr_t end =
        reinterpret_cast<uintptr_t>(
            koniec);

    if (end < start)
        return false;

    const uint64_t rozmiar =
        static_cast<uint64_t>(
            end - start);

    if (rozmiar == 0 ||
        rozmiar > UINT32_MAX) {
        return false;
    }

    *wynik =
        static_cast<uint32_t>(
            rozmiar);

    return true;
}
bool zapisz_plik_od_nowa_impl(const char* sciezka,
                         const char* dane,
                         uint32_t rozmiar) {
    if (!sciezka)
        return false;

    if (rozmiar != 0 && !dane)
        return false;

    /*
     * usun_twor() moze zwrocic false, gdy plik jeszcze nie istnieje.
     * To nie jest blad instalacji.
     */
    (void)usun_twor(sciezka);

    if (!utworz_plik(sciezka))
        return false;

    if (rozmiar == 0)
        return true;

    if (!zapisz_do_pliku(
            sciezka,
            dane,
            rozmiar)) {

        /*
         * Nie zostawiamy polowicznie zainstalowanego pliku.
         */
        (void)usun_twor(sciezka);
        return false;
    }

    return true;
}
bool zapisz_plik_od_nowa(const char*path,const char*data,uint32_t size){
    if(planowanie_instalacji){
        if(!path||(size&&!data)||planowane_liczba>=96||planowane_bajty_razem>UINT64_MAX-size){
            pliki_instalacja_ok=false;return false;
        }
        auto&e=planowane_pliki[planowane_liczba++];unsigned i=0;
        for(;i+1<sizeof(e.path)&&path[i];++i){e.path[i]=path[i];}
        e.path[i]=0;
        if(path[i]){pliki_instalacja_ok=false;return false;}
        e.data=data;e.size=size;planowane_bajty_razem+=size;
        return true;
    }
    if(scisla_instalacja&&!pliki_instalacja_ok)return false;
    bool ok=zapisz_plik_od_nowa_impl(path,data,size);
    if(scisla_instalacja){
        if(!ok||liczba_plikow_kontrolnych>=96){pliki_instalacja_ok=false;return false;}
        auto&e=pliki_kontrolne[liczba_plikow_kontrolnych++];unsigned i=0;
        for(;i+1<sizeof(e.path)&&path[i];++i){
            e.path[i]=path[i];
        }
        e.path[i]=0;
        if(path[i]){pliki_instalacja_ok=false;return false;}
        e.size=size;e.crc=Storage::crc32(data,size);
    }
    return ok;
}
bool zapisz_blob_bur(
    const char* sciezka,
    const uint8_t* poczatek,
    const uint8_t* koniec) {

    uint32_t rozmiar = 0;

    if (!pobierz_rozmiar_bloba(
            poczatek,
            koniec,
            &rozmiar)) {
        return false;
    }

    /*
     * Minimalna kontrola chroni przed zapisaniem pustego/uszkodzonego
     * symbolu jako programu .bur.
     */
    if (rozmiar < sizeof(NaglowekBur))
        return false;

    return zapisz_plik_od_nowa(
        sciezka,
        reinterpret_cast<const char*>(poczatek),
        rozmiar);
}
bool zapisz_manifest(const char* sciezka,
                     const char* manifest) {
    uint32_t dlugosc = 0;

    if (!dlugosc_tekstu_u32(
            manifest,
            &dlugosc)) {
        return false;
    }

    return zapisz_plik_od_nowa(
        sciezka,
        manifest,
        dlugosc);
}
bool instaluj_paczke(
    const char* sciezka_manifestu,
    const char* manifest,
    const char* sciezka_programu,
    const uint8_t* blob_start,
    const uint8_t* blob_end) {

    if (!zapisz_manifest(
            sciezka_manifestu,
            manifest)) {
        return false;
    }

    if (!zapisz_blob_bur(
            sciezka_programu,
            blob_start,
            blob_end)) {
        (void)usun_twor(
            sciezka_manifestu);

        return false;
    }

    return true;
}
static const char MANIFEST_TEST_DZWIEKU[] =
        "nazwa = \"Test dźwięku\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 3\n"
        "plik_startowy = \"test_dzwieku.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\"\n"
        "]\n";
static bool boot_install(const uint8_t* start, const uint8_t* end) { return instaluj_paczke(
            "/programy/test_dzwieku.cebula/opis.aplikacji",
            MANIFEST_TEST_DZWIEKU,
            "/programy/test_dzwieku.cebula/test_dzwieku.bur",
            start,
            end); }
