/*
 * Mechanizm: Menedżer i Parser Polskiego Systemu Plików 64-bit (BSP64)
 * Opis: Abstrakcja obsługująca formatowanie, alokację, nawigację.
 * Dostosowana do pracy na blokach 4096-bajtowych i pełnych 64-bitowych wskaźnikach.
 */

#include "psf.h"

static uint8_t* ram_dysk = nullptr;
static superblok* dysk_superblok = nullptr;

static uint64_t calkowita_liczba_wezlow = 0;
static uint64_t calkowita_liczba_blokow_danych = 0;

// Bufor ograniczony do 128 KB, by chronić wczesną przestrzeń BSS Jądra.
// Pozwala wczytać potężną powłokę bez crashu braku pamięci (OOM).
#define MAX_LOADER_BUF (128 * 1024)
static uint8_t bufor_wymiany_plikow[MAX_LOADER_BUF];

/*
 * --- METODY DOSTĘPOWE (RAW) ---
 */
static inline uint8_t* pobierz_blok(uint64_t nr_bloku) {
    if (!ram_dysk || !dysk_superblok || nr_bloku >= dysk_superblok->ilosc_blokow) return nullptr;
    return &ram_dysk[nr_bloku * PSF_ROZMIAR_BLOKU];
}

static wezel_indeksowy* pobierz_wezel(uint64_t id_wezla) {
    if (id_wezla == 0 || id_wezla > calkowita_liczba_wezlow) return nullptr;
    
    uint64_t wezlow_na_blok = PSF_ROZMIAR_BLOKU / sizeof(wezel_indeksowy); // W BSP64 wynosi dokładnie 1!
    uint64_t relatywny_indeks = id_wezla - 1; 
    uint64_t nr_bloku = dysk_superblok->start_wezlow + (relatywny_indeks / wezlow_na_blok);
    uint64_t wektor_w_bloku = relatywny_indeks % wezlow_na_blok;
    
    uint8_t* raw_blok = pobierz_blok(nr_bloku);
    if(!raw_blok) return nullptr;

    wezel_indeksowy* tablica_wezlow = (wezel_indeksowy*)raw_blok;
    return &tablica_wezlow[wektor_w_bloku];
}

/*
 * --- PROSTY ALOKATOR MIEJSCA DLA DYSKU (MAPA BITOWA) ---
 */
static void oznacz_blok_jak_zajety(uint64_t id_bloku_danych) {
    uint64_t bajt_w_bitmapie = id_bloku_danych / 8;
    uint64_t nr_bloku_bitmapy = 1 + (bajt_w_bitmapie / PSF_ROZMIAR_BLOKU);
    uint64_t offset_w_bloku = bajt_w_bitmapie % PSF_ROZMIAR_BLOKU;
    
    uint8_t* blok_tablicy = pobierz_blok(nr_bloku_bitmapy);
    if (blok_tablicy) {
        blok_tablicy[offset_w_bloku] |= (1 << (id_bloku_danych % 8));
    }
}

static uint64_t zaalokuj_wolny_blok_danych() {
    for (uint64_t i = 0; i < calkowita_liczba_blokow_danych; i++) {
        uint64_t bajt_w_bitmapie = i / 8;
        uint64_t nr_bloku_bitmapy = 1 + (bajt_w_bitmapie / PSF_ROZMIAR_BLOKU);
        uint64_t offset = bajt_w_bitmapie % PSF_ROZMIAR_BLOKU;
        
        uint8_t* blok_tablicy = pobierz_blok(nr_bloku_bitmapy);
        if (!blok_tablicy) continue;

        if ((blok_tablicy[offset] & (1 << (i % 8))) == 0) {
            oznacz_blok_jak_zajety(i);
            
            uint8_t* w_cel = pobierz_blok(dysk_superblok->start_danych + i);
            if (w_cel) {
                for(int j = 0; j < PSF_ROZMIAR_BLOKU; j++) w_cel[j] = 0;
            }
            return i; 
        }
    }
    return BARK_BLOKU;
}

static uint64_t zaalokuj_wolny_wezel(uint8_t typ) {
    for (uint64_t i = 1; i <= calkowita_liczba_wezlow; i++) {
        wezel_indeksowy* w = pobierz_wezel(i);
        if (w && w->typ == TYP_WOLNY) {
            w->typ = typ;
            w->rozmiar_w_bajtach = 0;
            w->czas_utworzenia = 0;
            w->flagi_zabezpieczen = 0;
            for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) w->wskazniki_blokow[k] = BARK_BLOKU;
            w->blok_posredni_1 = BARK_BLOKU;
            w->blok_posredni_2 = BARK_BLOKU;
            w->blok_posredni_3 = BARK_BLOKU;
            return i;
        }
    }
    return 0; 
}

/*
 * --- NARZĘDZIA STRING C --- 
 */
static bool czy_identyczne_str(const char* a, const char* b) {
    while(*a && *b) {
        if(*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static void kopiuj_str(char* dest, const char* src, int maks) {
    int i = 0;
    while(i < maks - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/*
 * --- PARSER DRZEWA ŚCIEŻEK (ROOT -> FOLDERY) ---
 */
static uint64_t rozwiaz_sciezke(const char* sciezka, char* nazwa_docelowa, bool czy_zwracac_rodzica) {
    if (!dysk_superblok || sciezka[0] != '/') return 0;
    
    uint64_t wezel_aktualny_id = dysk_superblok->id_korzenia;
    int indeks = 1; 
    char bufor_segmentu[PSF_MAX_NAZWA];

    while (sciezka[indeks] != '\0') {
        int idx_bufora = 0;
        
        while (sciezka[indeks] != '/' && sciezka[indeks] != '\0') {
            if (idx_bufora < PSF_MAX_NAZWA - 1) {
                bufor_segmentu[idx_bufora++] = sciezka[indeks];
            }
            indeks++;
        }
        bufor_segmentu[idx_bufora] = '\0';

        if (sciezka[indeks] == '/') {
            indeks++; 
            wezel_indeksowy* w = pobierz_wezel(wezel_aktualny_id);
            if(!w || w->typ != TYP_KATALOG) return 0; 

            bool znaleziono_krok = false;
            for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
                uint64_t id_bloku_danych = w->wskazniki_blokow[k];
                if(id_bloku_danych == BARK_BLOKU) continue;

                wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku_danych);
                if(!wpisy) continue;

                for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
                    if (wpisy[j].id_wezla != 0 && czy_identyczne_str(wpisy[j].nazwa, bufor_segmentu)) {
                        wezel_aktualny_id = wpisy[j].id_wezla;
                        znaleziono_krok = true;
                        break; 
                    }
                }
                if(znaleziono_krok) break;
            }
            if(!znaleziono_krok) return 0; 
        } 
        else if (sciezka[indeks] == '\0') {
            if (czy_zwracac_rodzica) {
                kopiuj_str(nazwa_docelowa, bufor_segmentu, PSF_MAX_NAZWA);
                return wezel_aktualny_id; 
            } else {
                wezel_indeksowy* w = pobierz_wezel(wezel_aktualny_id);
                if(!w || w->typ != TYP_KATALOG) return 0;

                for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
                    uint64_t id_bloku_danych = w->wskazniki_blokow[k];
                    if(id_bloku_danych == BARK_BLOKU) continue;

                    wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku_danych);
                    if(!wpisy) continue;

                    for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
                        if (wpisy[j].id_wezla != 0 && czy_identyczne_str(wpisy[j].nazwa, bufor_segmentu)) {
                            return wpisy[j].id_wezla; 
                        }
                    }
                }
                return 0; 
            }
        }
    }
    
    if(indeks == 1 && czy_zwracac_rodzica == false) return dysk_superblok->id_korzenia;
    return 0;
}

static bool dodaj_wpis_do_katalogu(uint64_t wezel_katalogu_id, uint64_t nowy_twor_id, const char* nazwa) {
    wezel_indeksowy* rodzic = pobierz_wezel(wezel_katalogu_id);
    if (!rodzic || rodzic->typ != TYP_KATALOG) return false;

    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (rodzic->wskazniki_blokow[k] == BARK_BLOKU) {
            uint64_t nowy_blok = zaalokuj_wolny_blok_danych();
            if (nowy_blok == BARK_BLOKU) return false; 
            rodzic->wskazniki_blokow[k] = nowy_blok;
        }

        uint64_t id_bloku = rodzic->wskazniki_blokow[k];
        wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku);
        if(!wpisy) continue;

        for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
            if (wpisy[j].id_wezla == 0) {
                wpisy[j].id_wezla = nowy_twor_id;
                kopiuj_str(wpisy[j].nazwa, nazwa, PSF_MAX_NAZWA);
                rodzic->rozmiar_w_bajtach++; 
                return true;
            }
        }
    }
    return false;
}

/*
 * --- API PUBLICZNE JĄDRA ---
 */

extern "C" void inicjalizuj_psf(void* adres_ram_dysku, uint32_t rozmiar_w_bajtach) {
    if (!adres_ram_dysku || rozmiar_w_bajtach < PSF_ROZMIAR_BLOKU * 10) return; 
    
    uint8_t* dysk = (uint8_t*)adres_ram_dysku;
    for(uint32_t i=0; i<rozmiar_w_bajtach; i++) dysk[i] = 0;

    ram_dysk = dysk;
    dysk_superblok = (superblok*)ram_dysk;

    uint64_t ilosc_blokow = rozmiar_w_bajtach / PSF_ROZMIAR_BLOKU;
    
    dysk_superblok->sygnatura[0] = 'B'; dysk_superblok->sygnatura[1] = 'S'; dysk_superblok->sygnatura[2] = 'P'; dysk_superblok->sygnatura[3] = '2';
    dysk_superblok->calkowity_rozmiar = rozmiar_w_bajtach;
    dysk_superblok->ilosc_blokow = ilosc_blokow;

    uint64_t blokow_bitmapy = (ilosc_blokow / 8 / PSF_ROZMIAR_BLOKU) + 1;
    dysk_superblok->start_wezlow = 1 + blokow_bitmapy;
    
    uint64_t bloki_reszta = ilosc_blokow - dysk_superblok->start_wezlow;
    uint64_t bloki_wezlow = bloki_reszta / 4; 
    if (bloki_wezlow < 1) bloki_wezlow = 1;

    dysk_superblok->start_danych = dysk_superblok->start_wezlow + bloki_wezlow;
    
    calkowita_liczba_wezlow = bloki_wezlow * (PSF_ROZMIAR_BLOKU / sizeof(wezel_indeksowy));
    calkowita_liczba_blokow_danych = ilosc_blokow - dysk_superblok->start_danych;

    uint64_t id_korzenia = zaalokuj_wolny_wezel(TYP_KATALOG);
    dysk_superblok->id_korzenia = id_korzenia;
}

extern "C" bool utworz_katalog(const char* sciezka) {
    char nowa_nazwa[PSF_MAX_NAZWA];
    uint64_t wezel_rodzica_id = rozwiaz_sciezke(sciezka, nowa_nazwa, true);
    if (wezel_rodzica_id == 0) return false; 

    uint64_t nowy_id = zaalokuj_wolny_wezel(TYP_KATALOG);
    if (nowy_id == 0) return false;

    return dodaj_wpis_do_katalogu(wezel_rodzica_id, nowy_id, nowa_nazwa);
}

extern "C" bool utworz_plik(const char* sciezka) {
    char nowa_nazwa[PSF_MAX_NAZWA];
    uint64_t wezel_rodzica_id = rozwiaz_sciezke(sciezka, nowa_nazwa, true);
    if (wezel_rodzica_id == 0) return false; 

    uint64_t nowy_id = zaalokuj_wolny_wezel(TYP_PLIK);
    if (nowy_id == 0) return false;

    return dodaj_wpis_do_katalogu(wezel_rodzica_id, nowy_id, nowa_nazwa);
}

extern "C" bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc) {
    uint64_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return false; 

    wezel_indeksowy* wezel = pobierz_wezel(plik_id);
    if (!wezel || wezel->typ != TYP_PLIK) return false;

    uint32_t zapisano = 0;
    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (zapisano >= dlugosc) break;

        if (wezel->wskazniki_blokow[k] == BARK_BLOKU) {
            uint64_t wpis = zaalokuj_wolny_blok_danych();
            if(wpis == BARK_BLOKU) break; 
            wezel->wskazniki_blokow[k] = wpis;
        }

        uint8_t* ptr_bloku = pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
        if (!ptr_bloku) break;

        uint32_t porcja = dlugosc - zapisano;
        if(porcja > PSF_ROZMIAR_BLOKU) porcja = PSF_ROZMIAR_BLOKU;

        for(uint32_t j=0; j<porcja; j++) {
            ptr_bloku[j] = dane[zapisano + j];
        }

        zapisano += porcja;
    }
    
    wezel->rozmiar_w_bajtach = zapisano;
    return (zapisano == dlugosc);
}

extern "C" bool czytaj_z_pliku(const char* sciezka, char* bufor, uint32_t max_dlugosc) {
    uint64_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return false; 

    wezel_indeksowy* wezel = pobierz_wezel(plik_id);
    if (!wezel || wezel->typ != TYP_PLIK) return false;

    uint32_t przeczytano = 0;
    uint32_t dlugosc_pliku = wezel->rozmiar_w_bajtach;
    
    if(max_dlugosc < dlugosc_pliku) dlugosc_pliku = max_dlugosc;

    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (przeczytano >= dlugosc_pliku) break;
        if (wezel->wskazniki_blokow[k] == BARK_BLOKU) break;

        uint8_t* ptr_bloku = pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
        if (!ptr_bloku) break;

        uint32_t porcja = dlugosc_pliku - przeczytano;
        if(porcja > PSF_ROZMIAR_BLOKU) porcja = PSF_ROZMIAR_BLOKU;

        for(uint32_t j=0; j<porcja; j++) {
            bufor[przeczytano + j] = ptr_bloku[j];
        }

        przeczytano += porcja;
    }

    return true;
}

extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(const char* sciezka, uint64_t* rozmiar_wyj) {
    if (!rozmiar_wyj) return nullptr;
    *rozmiar_wyj = 0;

    uint64_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return nullptr; 

    wezel_indeksowy* wezel = pobierz_wezel(plik_id);
    if (!wezel || wezel->typ != TYP_PLIK) return nullptr;

    uint64_t dlugosc_pliku = wezel->rozmiar_w_bajtach;
    if (dlugosc_pliku == 0 || dlugosc_pliku > MAX_LOADER_BUF) return nullptr;

    uint64_t przeczytano = 0;

    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (przeczytano >= dlugosc_pliku) break;
        if (wezel->wskazniki_blokow[k] == BARK_BLOKU) break;

        uint8_t* ptr_bloku = pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
        if (!ptr_bloku) break;

        uint64_t porcja = dlugosc_pliku - przeczytano;
        if (porcja > PSF_ROZMIAR_BLOKU) porcja = PSF_ROZMIAR_BLOKU;

        for(uint64_t j = 0; j < porcja; j++) {
            bufor_wymiany_plikow[przeczytano + j] = ptr_bloku[j];
        }

        przeczytano += porcja;
    }

    *rozmiar_wyj = przeczytano;
    return bufor_wymiany_plikow;
}

// Zaktualizowana funkcja listująca zawartość z podziałem na linie i tagami [KAT]/[PLIK]
extern "C" bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc) {
    uint64_t kat_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (kat_id == 0) return false; 

    wezel_indeksowy* wezel = pobierz_wezel(kat_id);
    if (!wezel || wezel->typ != TYP_KATALOG) return false;

    uint32_t pozycja = 0;
    bufor[0] = '\0';

    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (wezel->wskazniki_blokow[k] == BARK_BLOKU) continue;

        wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
        if (!wpisy) continue;

        for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
            if (wpisy[j].id_wezla != 0) {
                
                // Pobieramy węzeł, na który wskazuje wpis w katalogu, aby sprawdzić jego typ
                wezel_indeksowy* element = pobierz_wezel(wpisy[j].id_wezla);
                
                // Dodanie prefiksu [KAT] lub [PLIK]
                const char* tag = (element && element->typ == TYP_KATALOG) ? "[KAT]  " : "[PLIK] ";
                int t = 0;
                while (tag[t] != '\0' && pozycja < max_dlugosc - 1) {
                    bufor[pozycja++] = tag[t++];
                }

                // Dodanie samej nazwy (np. "jadro" albo "shell.bur")
                const char* nazwa = wpisy[j].nazwa;
                int idx = 0;
                while (nazwa[idx] != '\0' && pozycja < max_dlugosc - 1) {
                    bufor[pozycja++] = nazwa[idx++];
                }
                
                // Złamanie linii (ENTER) na koniec każdego elementu
                if (pozycja < max_dlugosc - 1) {
                    bufor[pozycja++] = '\n'; 
                }
            }
        }
    }
    bufor[pozycja] = '\0';
    return true;
}