constexpr size_t MAX_SCIEZKA_PLIKU_BWS = 512;
uint64_t znane_prawa(
    const proces_t& proces
) {
    /*
     * Nieznane bity sa ignorowane, a nie automatycznie honorowane.
     * Zachowuje to kompatybilnosc z obecnym managerem okien, ktory mogl
     * zostac jeszcze uruchomiony ze stara maska 0xFFFFFFFF.
     */
    return
        proces.uprawnienia &
        static_cast<uint64_t>(
            PRAWA_ZNANE
        );
}
bool proces_ma_prawo(
    const proces_t& proces,
    uint64_t wymagane
) {
    if(instalator_tryb()&&(wymagane&PRAWO_PLIKI_ZAPISZ))return false;
    return
        (znane_prawa(proces) &
         wymagane) ==
        wymagane;
}
size_t dlugosc_stringa_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return 0;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            return i;
        }
    }

    return limit;
}
bool pobierz_string_user(
    uint64_t adres,
    char* cel,
    size_t pojemnosc
) {
    if (adres == 0 ||
        !cel ||
        pojemnosc == 0) {

        return false;
    }

    return
        skopiuj_string_z_uzytkownika(
            cel,
            reinterpret_cast<const char*>(
                adres
            ),
            pojemnosc
        );
}
bool pobierz_sciezke_pliku(
    uint64_t adres,
    char* cel
) {
    return
        pobierz_string_user(
            adres,
            cel,
            MAX_SCIEZKA_PLIKU_BWS
        );
}
bool segment_prefiksu(
    const char* sciezka,
    const char* prefiks
) {
    if (!sciezka ||
        !prefiks) {

        return false;
    }

    size_t i = 0;

    while (prefiks[i] != '\0') {
        if (sciezka[i] !=
            prefiks[i]) {

            return false;
        }

        ++i;
    }

    /*
     * "/system2" nie jest dzieckiem "/system".
     */
    return
        sciezka[i] == '\0' ||
        sciezka[i] == '/';
}
bool sciezka_chroniona(
    const char* sciezka
) {
    return
        segment_prefiksu(
            sciezka,
            "/system"
        ) ||
        segment_prefiksu(
            sciezka,
            "/jadro"
        );
}
bool proces_moze_modyfikowac_sciezke(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_PLIKI_ZAPISZ)) {

        return false;
    }

    /*
     * Zwykle aplikacje i sandbox nie moga modyfikowac krytycznych drzew.
     */
    if (proces.poziom_zaufania >=
            PZB_UZYTKOWNIK &&
        sciezka_chroniona(
            sciezka)) {

        return false;
    }

    return true;
}
bool poziom_pliku_pozwala_modyfikowac(
    const proces_t& proces,
    const char* sciezka
) {
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(sciezka, &meta)) return false;
    /* Stare BSP2 nie maja etykiety: zachowuja dotychczasowa polityke
     * prawa PRAWO_PLIKI_ZAPISZ i ochrony drzew systemowych. */
    return !meta.pzb_dostepny || proces.poziom_zaufania <= meta.poziom_pzb;
}
bool pobierz_rodzica_sciezki(
    const char* sciezka,
    char* rodzic,
    size_t pojemnosc
) {
    if (!sciezka || !rodzic || pojemnosc < 2 || sciezka[0] != '/')
        return false;
    const size_t dlugosc = dlugosc_stringa_limit(sciezka, pojemnosc);
    if (dlugosc == 0 || dlugosc >= pojemnosc) return false;
    size_t slash = dlugosc;
    while (slash > 0 && sciezka[slash] != '/') --slash;
    if (slash == 0) {
        rodzic[0] = '/';
        rodzic[1] = '\0';
        return true;
    }
    if (slash + 1 > pojemnosc) return false;
    for (size_t i = 0; i < slash; ++i) rodzic[i] = sciezka[i];
    rodzic[slash] = '\0';
    return true;
}
uint8_t efektywny_pzb_dla_nowego(const proces_t& proces, const char* sciezka) {
    char rodzic[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_rodzica_sciezki(sciezka, rodzic, sizeof(rodzic)))
        return proces.poziom_zaufania;
    psf_metadane pm{};
    if (!pobierz_metadane_tworu(rodzic, &pm) || !pm.pzb_dostepny)
        return proces.poziom_zaufania;
    uint8_t eff = pm.poziom_pzb > proces.poziom_zaufania ? pm.poziom_pzb : proces.poziom_zaufania;
    if (eff > PZB_MAX_POZIOM) eff = PZB_MAX_POZIOM;
    return eff;
}
bool proces_moze_utworzyc_w_sciezce(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_moze_modyfikowac_sciezke(proces, sciezka)) {
#ifdef BURSZTYN_DIAGNOSTYKA_PSFBWS
        char lb[96] = "[PSF/BWS] create parent=DENY-modyfikacja-sciezki";
        wypisz_log(lb);
#endif
        return false;
    }
    char rodzic[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_rodzica_sciezki(sciezka, rodzic, sizeof(rodzic))) {
#ifdef BURSZTYN_DIAGNOSTYKA_PSFBWS
        wypisz_log("[PSF/BWS] create parent=BRAK-rodzica");
        wypisz_log("[PSF/BWS] result=DENY");
#endif
        return false;
    }
    bool dozwolone = poziom_pliku_pozwala_modyfikowac(proces, rodzic);
#ifdef BURSZTYN_DIAGNOSTYKA_PSFBWS
    psf_metadane meta{};
    bool meta_ok = pobierz_metadane_tworu(rodzic, &meta);
    uint8_t parent_pzb = (meta_ok && meta.pzb_dostepny) ? meta.poziom_pzb : 0;
    uint8_t parent_type = meta_ok ? meta.typ : 0;
    char log_buf[256];
    size_t pos = 0;
    const char* p1 = "[PSF/BWS] create parent=";
    for (size_t i = 0; p1[i]; ++i) log_buf[pos++] = p1[i];
    for (size_t i = 0; rodzic[i] && pos < sizeof(log_buf) - 1; ++i) log_buf[pos++] = rodzic[i];
    log_buf[pos] = '\0';
    wypisz_log(log_buf);
    pos = 0;
    const char* p2 = "[PSF/BWS] parent_type=";
    for (size_t i = 0; p2[i]; ++i) log_buf[pos++] = p2[i];
    log_buf[pos++] = '0' + parent_type;
    log_buf[pos] = '\0';
    wypisz_log(log_buf);
    pos = 0;
    const char* p3 = "[PSF/BWS] parent_pzb=";
    for (size_t i = 0; p3[i]; ++i) log_buf[pos++] = p3[i];
    log_buf[pos++] = '0' + parent_pzb;
    log_buf[pos] = '\0';
    wypisz_log(log_buf);
    pos = 0;
    const char* p4 = "[PSF/BWS] process_pzb=";
    for (size_t i = 0; p4[i]; ++i) log_buf[pos++] = p4[i];
    log_buf[pos++] = '0' + proces.poziom_zaufania;
    log_buf[pos] = '\0';
    wypisz_log(log_buf);
    pos = 0;
    const char* p5 = "[PSF/BWS] result=";
    for (size_t i = 0; p5[i]; ++i) log_buf[pos++] = p5[i];
    const char* rs = dozwolone ? "ALLOW" : "DENY";
    for (size_t i = 0; rs[i]; ++i) log_buf[pos++] = rs[i];
    log_buf[pos] = '\0';
    wypisz_log(log_buf);
#endif
    return dozwolone;
}
bool proces_moze_modyfikowac_obiekt(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_moze_modyfikowac_sciezke(proces, sciezka) ||
        !poziom_pliku_pozwala_modyfikowac(proces, sciezka)) return false;
    if (sciezka[0] == '/' && sciezka[1] == '\0') return true;
    char rodzic[MAX_SCIEZKA_PLIKU_BWS] = {};
    return pobierz_rodzica_sciezki(sciezka, rodzic, sizeof(rodzic)) &&
           poziom_pliku_pozwala_modyfikowac(proces, rodzic);
}
void wyzeruj(
    void* ptr,
    uint64_t rozmiar
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            ptr
        );

    for (uint64_t i = 0;
         i < rozmiar;
         ++i) {

        p[i] = 0;
    }
}
void* alokuj_bufor(
    uint64_t rozmiar,
    bool zeruj
) {
    if (rozmiar == 0) {
        return nullptr;
    }

    void* ptr =
        kmalloc(rozmiar);

    if (!ptr) {
        return nullptr;
    }

    if (zeruj) {
        wyzeruj(
            ptr,
            rozmiar
        );
    }

    return ptr;
}
class BuforKernelowy {
public:
    explicit BuforKernelowy(
        uint64_t rozmiar,
        bool zeruj = false
    )
        : ptr_(
              alokuj_bufor(
                  rozmiar,
                  zeruj
              )
          ),
          rozmiar_(rozmiar) {
    }

    ~BuforKernelowy() {
        if (ptr_) {
            kfree(ptr_);
        }
    }

    BuforKernelowy(
        const BuforKernelowy&
    ) = delete;

    BuforKernelowy& operator=(
        const BuforKernelowy&
    ) = delete;

    void* get() const {
        return ptr_;
    }

    char* jako_char() const {
        return static_cast<char*>(
            ptr_
        );
    }

    uint8_t* jako_u8() const {
        return static_cast<uint8_t*>(
            ptr_
        );
    }

    uint64_t rozmiar() const {
        return rozmiar_;
    }

    explicit operator bool() const {
        return
            ptr_ != nullptr;
    }

private:
    void* ptr_;
    uint64_t rozmiar_;
};
__attribute__((noinline)) uint64_t bws_utworz_plik(
    proces_t& proces,
    uint64_t arg1, uint64_t mode=0
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return mode==BWS_STATUS_PLIKOWY?uint64_t(WynikPlikowy::NieprawidlowaNazwa):0;
    }

    if (!proces_moze_utworzyc_w_sciezce(
            proces,
            sciezka)) {
        wypisz_log("[PZB] file_create=DENY");
        return mode==BWS_STATUS_PLIKOWY?uint64_t(WynikPlikowy::BrakUprawnien):0;
    }
    wypisz_log("[PZB] file_create=ALLOW");

    const uint8_t eff = efektywny_pzb_dla_nowego(proces, sciezka);
    if(mode==BWS_STATUS_PLIKOWY) {
        const auto result=psf_utworz_wynik(sciezka,false,eff);
        psf_metadane meta{};
        if(result==WynikPlikowy::Ok || (result==WynikPlikowy::BladIO && pobierz_metadane_tworu(sciezka,&meta)))
            powiadom_zmiane_systemu_plikow(sciezka,BWS_FILE_CREATE);
        return uint64_t(result);
    }
    const bool wynik = utworz_plik_z_pzb(sciezka, eff);
    if (wynik) powiadom_zmiane_systemu_plikow(sciezka, BWS_FILE_CREATE);
    return wynik ? 1ULL : 0ULL;
}
__attribute__((noinline)) uint64_t bws_zapisz_plik(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (!proces_moze_modyfikowac_obiekt(
            proces,
            sciezka)) {
        wypisz_log("[PZB] file_write=DENY");
        return 0;
    }
    wypisz_log("[PZB] file_write=ALLOW");

    if (arg3 > PSF_MAKS_ROZMIAR_PLIKU) {
        return 0;
    }

    const uint32_t dlugosc =
        static_cast<uint32_t>(
            arg3
        );

    if (dlugosc == 0) {
        const bool wynik = zapisz_do_pliku(sciezka, nullptr, 0);
        if (wynik) powiadom_zmiane_systemu_plikow(sciezka, BWS_FILE_MODIFY);
        return wynik ? 1ULL : 0ULL;
    }

    if (arg2 == 0) {
        return 0;
    }

    // dla dużych plików użyj strumieniowego zapisu fragmentami (1M) aby nie alokować 100M w kernelu
    if (dlugosc > (1U*1024U*1024U)) {
        const uint32_t CHUNK = 64U*1024U;
        BuforKernelowy chunk(CHUNK, false);
        if (!chunk) return 0;
        // najpierw skróć plik do 0
        if (!zapisz_do_pliku(sciezka, nullptr, 0)) return 0;
        uint64_t off = 0;
        while (off < dlugosc) {
            uint32_t porcja = dlugosc - (uint32_t)off > CHUNK ? CHUNK : dlugosc - (uint32_t)off;
            if (!skopiuj_z_przestrzeni_uzytkownika(chunk.get(), reinterpret_cast<const void*>(arg2 + off), porcja)) return 0;
            if (!zapisz_fragment_pliku(sciezka, off, static_cast<char*>(chunk.get()), porcja)) return 0;
            off += porcja;
        }
        powiadom_zmiane_systemu_plikow(sciezka, BWS_FILE_MODIFY);
        return 1ULL;
    }

    BuforKernelowy dane(
        dlugosc,
        false
    );

    if (!dane) {
        return 0;
    }

    if (!skopiuj_z_przestrzeni_uzytkownika(
            dane.get(),
            reinterpret_cast<const void*>(
                arg2
            ),
            dlugosc)) {

        return 0;
    }

    const bool wynik = zapisz_do_pliku(sciezka, dane.jako_char(), dlugosc);
    if (wynik) powiadom_zmiane_systemu_plikow(sciezka, BWS_FILE_MODIFY);
    return wynik ? 1ULL : 0ULL;
}
__attribute__((noinline)) uint64_t bws_pobierz_metadane(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) || arg2 == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(arg2), sizeof(BwsMetadanePliku)))
        return 0;
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka)) return 0;
    psf_metadane psf{};
    if (!pobierz_metadane_tworu(sciezka, &psf)) return 0;
    BwsMetadanePliku wynik{};
    wynik.wersja = BWS_METADANE_WERSJA;
    wynik.typ = psf.typ;
    wynik.rozmiar = psf.rozmiar;
    wynik.czas_utworzenia_rtc = psf.czas_utworzenia_rtc;
    wynik.poziom_pzb = psf.poziom_pzb;
    if (psf.typ == TYP_PLIK) wynik.flagi |= BWS_META_ROZMIAR_DOSTEPNY;
    if (psf.czas_dostepny) wynik.flagi |= BWS_META_CZAS_DOSTEPNY;
    if (psf.pzb_dostepny) wynik.flagi |= BWS_META_PZB_DOSTEPNY;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(arg2), &wynik, sizeof(wynik)) ? 1ULL : 0ULL;
}
__attribute__((noinline)) uint64_t bws_utworz_katalog(
    proces_t& proces,
    uint64_t arg1, uint64_t mode=0
) {
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka)) return mode==BWS_STATUS_PLIKOWY?uint64_t(WynikPlikowy::NieprawidlowaNazwa):0;
    if (!proces_moze_utworzyc_w_sciezce(proces, sciezka)) {
        wypisz_log("[PZB] directory_create=DENY");
        return mode==BWS_STATUS_PLIKOWY?uint64_t(WynikPlikowy::BrakUprawnien):0;
    }
    wypisz_log("[PZB] directory_create=ALLOW");
    const uint8_t eff = efektywny_pzb_dla_nowego(proces, sciezka);
    if(mode==BWS_STATUS_PLIKOWY) {
        const auto result=psf_utworz_wynik(sciezka,true,eff);
        psf_metadane meta{};
        if(result==WynikPlikowy::Ok || (result==WynikPlikowy::BladIO && pobierz_metadane_tworu(sciezka,&meta)))
            powiadom_zmiane_systemu_plikow(sciezka,BWS_FILE_CREATE);
        return uint64_t(result);
    }
    const bool wynik = utworz_katalog_z_pzb(sciezka, eff);
    if (wynik)
        powiadom_zmiane_systemu_plikow(sciezka, BWS_FILE_CREATE);
    return wynik ? 1ULL : 0ULL;
}