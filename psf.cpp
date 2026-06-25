/*
 * Mechanizm: Menedżer i Parser Polskiego Systemu Plików (PSF) w wirtualnym RAM dysku
 * Opis: Abstrakcja obsługująca formatowanie, alokację, nawigację pomiędzy
 * Wpisami Katalogowymi a Węzłami oraz zapisywanie i odczyt z użyciem alokatora.
 * Implementacja dostosowana do zrewidowanego nazewnictwa (snake_case).
 */

#include "psf.h"

// Wskaźnik bazowy dla całego wirtualnego dysku
static uint8_t* ram_dysk = nullptr;
static superblok* dysk_superblok = nullptr;

// Pomocnicze parametry ułatwiające szukanie
static uint32_t calkowita_liczba_wezlow = 0;
static uint32_t calkowita_liczba_blokow_danych = 0;

/*
 * --- METODY DOSTĘPOWE (RAW) ---
 */

// Zwraca konkretny blok 512-bajtowy w formie surowej
static inline uint8_t* pobierz_blok(uint32_t nr_bloku) {
    if (!ram_dysk || nr_bloku >= dysk_superblok->ilosc_blokow) return nullptr;
    return &ram_dysk[nr_bloku * PSF_ROZMIAR_BLOKU];
}

// Zwraca wskaźnik na konkretny węzeł na podstawie jego ID (ID zaczyna się od 1)
static wezel_indeksowy* pobierz_wezel(uint32_t id_wezla) {
    if (id_wezla == 0 || id_wezla > calkowita_liczba_wezlow) return nullptr;
    
    uint32_t wezlow_na_blok = PSF_ROZMIAR_BLOKU / sizeof(wezel_indeksowy);
    uint32_t relatywny_indeks = id_wezla - 1; 
    uint32_t nr_bloku = dysk_superblok->start_wezlow + (relatywny_indeks / wezlow_na_blok);
    uint32_t wektor_w_bloku = relatywny_indeks % wezlow_na_blok;
    
    uint8_t* raw_blok = pobierz_blok(nr_bloku);
    if(!raw_blok) return nullptr;

    wezel_indeksowy* tablica_wezlow = (wezel_indeksowy*)raw_blok;
    return &tablica_wezlow[wektor_w_bloku];
}

/*
 * --- PROSTY ALOKATOR MIEJSCA DLA DYSKU (MAPA BITOWA) ---
 */
static void oznacz_blok_jak_zajety(uint32_t id_bloku) {
    uint8_t* tablica_bitowa = pobierz_blok(1); // Blok 1 to zawsze tablica bitowa
    tablica_bitowa[id_bloku / 8] |= (1 << (id_bloku % 8));
}

static uint32_t zaalokuj_wolny_blok_danych() {
    uint8_t* tablica_bitowa = pobierz_blok(1);
    for (uint32_t i = 0; i < calkowita_liczba_blokow_danych; i++) {
        if ((tablica_bitowa[i / 8] & (1 << (i % 8))) == 0) {
            oznacz_blok_jak_zajety(i);
            
            uint8_t* w_cel = pobierz_blok(dysk_superblok->start_danych + i);
            for(int j=0; j<PSF_ROZMIAR_BLOKU; j++) w_cel[j] = 0;
            return i; 
        }
    }
    return 0xFFFFFFFF; // Brak miejsca!
}

static uint32_t zaalokuj_wolny_wezel(uint8_t typ) {
    for (uint32_t i = 1; i <= calkowita_liczba_wezlow; i++) {
        wezel_indeksowy* w = pobierz_wezel(i);
        if (w->typ == TYP_WOLNY) {
            w->typ = typ;
            w->rozmiar_w_bajtach = 0;
            for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) w->wskazniki_blokow[k] = 0xFFFFFFFF;
            return i;
        }
    }
    return 0; // Brak węzłów 
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
 * Interpretuje ciąg np "/katalog/test" szukając od korzenia.
 * Zwraca id węzła macierzystego, a w `nazwa_docelowa` wyodrębnia końcówkę ("test").
 */
static uint32_t rozwiaz_sciezke(const char* sciezka, char* nazwa_docelowa, bool czy_zwracac_rodzica) {
    if (!dysk_superblok || sciezka[0] != '/') return 0; // Wymagamy absolutnej ścieżki startującej od '/'
    
    uint32_t wezel_aktualny_id = dysk_superblok->id_korzenia;
    int indeks = 1; // Pomijamy początkowy '/'
    char bufor_segmentu[PSF_MAX_NAZWA];

    while (sciezka[indeks] != '\0') {
        int idx_bufora = 0;
        // Wytnij nazwę komponentu w głąb ścieżki
        while (sciezka[indeks] != '/' && sciezka[indeks] != '\0' && idx_bufora < PSF_MAX_NAZWA - 1) {
            bufor_segmentu[idx_bufora++] = sciezka[indeks++];
        }
        bufor_segmentu[idx_bufora] = '\0';

        if (sciezka[indeks] == '/') {
            // Skoro napotkano '/', to segment musi odnosić się do nazwy jakiegoś katalogu pośredniego. Szukamy go w aktualnym węźle.
            indeks++; 
            wezel_indeksowy* w = pobierz_wezel(wezel_aktualny_id);
            if(w->typ != TYP_KATALOG) return 0; // Plik nie może mieć dzieci, przerwij operację

            bool znaleziono_krok = false;
            // Przeszukaj zaalokowane w tym katalogu bloki z danymi, bo tam kryją się obiekty potomne wpis_katalogowy
            for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
                uint32_t id_bloku_danych = w->wskazniki_blokow[k];
                if(id_bloku_danych == 0xFFFFFFFF) continue;

                wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku_danych);
                for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
                    if (wpisy[j].id_wezla != 0 && czy_identyczne_str(wpisy[j].nazwa, bufor_segmentu)) {
                        wezel_aktualny_id = wpisy[j].id_wezla;
                        znaleziono_krok = true;
                        break; // Znaleziono, przechodzimy o węzeł w głąb hierarchii (cd)
                    }
                }
                if(znaleziono_krok) break;
            }
            if(!znaleziono_krok) return 0; // Przerwany łańcuch nawigacji, ścieżka do utworzenia nie zawiera ważnego rodzica!
        } 
        else if (sciezka[indeks] == '\0') {
            // Wycięto ostatni znak w podanej stringowej ścieżce, to jest cel ostateczny w parserze.
            if (czy_zwracac_rodzica) {
                // Kontekst tworzenia nowego pliku/folderu wymaga znajomości macierzy oraz nowej nazwy elementu:
                kopiuj_str(nazwa_docelowa, bufor_segmentu, PSF_MAX_NAZWA);
                return wezel_aktualny_id; 
            } else {
                // Kontekst odczytu/zapisu wymaga ID konkretnego już utrworzonego pliku. Namierzmy to!
                wezel_indeksowy* w = pobierz_wezel(wezel_aktualny_id);
                if(w->typ != TYP_KATALOG) return 0;

                for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
                    uint32_t id_bloku_danych = w->wskazniki_blokow[k];
                    if(id_bloku_danych == 0xFFFFFFFF) continue;

                    wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku_danych);
                    for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
                        if (wpisy[j].id_wezla != 0 && czy_identyczne_str(wpisy[j].nazwa, bufor_segmentu)) {
                            return wpisy[j].id_wezla; // Ostateczne rozwiązanie!
                        }
                    }
                }
                return 0; // Podany twór nie istnieje
            }
        }
    }
    
    // Obsługa korzenia ("/") jako wyjątku
    if(indeks == 1 && czy_zwracac_rodzica == false) return dysk_superblok->id_korzenia;

    return 0;
}

// Funkcja dodająca wpis do węzła macierzystego powielając odpowiednie algorytmy rozszerzeń blokowych (przepięć)
static bool dodaj_wpis_do_katalogu(uint32_t wezel_katalogu_id, uint32_t nowy_twor_id, const char* nazwa) {
    wezel_indeksowy* rodzic = pobierz_wezel(wezel_katalogu_id);
    if (!rodzic || rodzic->typ != TYP_KATALOG) return false;

    // Przeszukaj zaalokowane już bloki katalogu w poszukiwaniu wolnego miejsca
    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (rodzic->wskazniki_blokow[k] == 0xFFFFFFFF) {
            uint32_t nowy_blok = zaalokuj_wolny_blok_danych();
            if (nowy_blok == 0xFFFFFFFF) return false; // Dysk pełny!
            rodzic->wskazniki_blokow[k] = nowy_blok;
        }

        uint32_t id_bloku = rodzic->wskazniki_blokow[k];
        wpis_katalogowy* wpisy = (wpis_katalogowy*)pobierz_blok(dysk_superblok->start_danych + id_bloku);
        for(int j = 0; j < PSF_ROZMIAR_BLOKU / (int)sizeof(wpis_katalogowy); j++) {
            if (wpisy[j].id_wezla == 0) {
                wpisy[j].id_wezla = nowy_twor_id;
                kopiuj_str(wpisy[j].nazwa, nazwa, PSF_MAX_NAZWA);
                rodzic->rozmiar_w_bajtach++; 
                return true;
            }
        }
    }
    return false; // Brak miejsca na wpis katalogowy (Wszystkie 10 bloków x 16 wpisów pęka w szwach)
}

/*
 * --- API PUBLICZNE JĄDRA ---
 */

extern "C" void inicjalizuj_psf(void* adres_ram_dysku, uint32_t rozmiar_w_bajtach) {
    if (!adres_ram_dysku || rozmiar_w_bajtach < PSF_ROZMIAR_BLOKU * 10) return; 
    
    // Inicjalizacja podłoża by pozbyć się śmieci na RAM dysku
    uint8_t* dysk = (uint8_t*)adres_ram_dysku;
    for(uint32_t i=0; i<rozmiar_w_bajtach; i++) dysk[i] = 0;

    ram_dysk = dysk;
    dysk_superblok = (superblok*)pobierz_blok(0);

    // Domyślna prosta architektura podziału miejsca 1:4 (Wezły : Dane surowe)
    uint32_t ilosc_blokow = rozmiar_w_bajtach / PSF_ROZMIAR_BLOKU;
    
    dysk_superblok->sygnatura[0] = 'P'; dysk_superblok->sygnatura[1] = 'S'; dysk_superblok->sygnatura[2] = 'F'; dysk_superblok->sygnatura[3] = '1';
    dysk_superblok->calkowity_rozmiar = rozmiar_w_bajtach;
    dysk_superblok->ilosc_blokow = ilosc_blokow;

    // Blok 0 to Superblok, Blok 1..x to Bitmapa bloków danych
    uint32_t blokow_bitmapy = (ilosc_blokow / 8 / PSF_ROZMIAR_BLOKU) + 1;
    dysk_superblok->start_wezlow = 1 + blokow_bitmapy;
    
    uint32_t bloki_reszta = ilosc_blokow - dysk_superblok->start_wezlow;
    uint32_t bloki_wezlow = bloki_reszta / 4; 
    if (bloki_wezlow < 1) bloki_wezlow = 1;

    dysk_superblok->start_danych = dysk_superblok->start_wezlow + bloki_wezlow;
    
    calkowita_liczba_wezlow = bloki_wezlow * (PSF_ROZMIAR_BLOKU / sizeof(wezel_indeksowy));
    calkowita_liczba_blokow_danych = ilosc_blokow - dysk_superblok->start_danych;

    // Instalacja bazowego korzenia ("/") by można się było do czegoś domyślnie odwoływać
    uint32_t id_korzenia = zaalokuj_wolny_wezel(TYP_KATALOG);
    dysk_superblok->id_korzenia = id_korzenia;
}

extern "C" bool utworz_katalog(const char* sciezka) {
    char nowa_nazwa[PSF_MAX_NAZWA];
    uint32_t wezel_rodzica_id = rozwiaz_sciezke(sciezka, nowa_nazwa, true);
    if (wezel_rodzica_id == 0) return false; 

    uint32_t nowy_id = zaalokuj_wolny_wezel(TYP_KATALOG);
    if (nowy_id == 0) return false;

    return dodaj_wpis_do_katalogu(wezel_rodzica_id, nowy_id, nowa_nazwa);
}

extern "C" bool utworz_plik(const char* sciezka) {
    char nowa_nazwa[PSF_MAX_NAZWA];
    uint32_t wezel_rodzica_id = rozwiaz_sciezke(sciezka, nowa_nazwa, true);
    if (wezel_rodzica_id == 0) return false; 

    uint32_t nowy_id = zaalokuj_wolny_wezel(TYP_PLIK);
    if (nowy_id == 0) return false;

    return dodaj_wpis_do_katalogu(wezel_rodzica_id, nowy_id, nowa_nazwa);
}

extern "C" bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc) {
    uint32_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return false; 

    wezel_indeksowy* wezel = pobierz_wezel(plik_id);
    if (!wezel || wezel->typ != TYP_PLIK) return false;

    uint32_t zapisano = 0;
    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (zapisano >= dlugosc) break;

        if (wezel->wskazniki_blokow[k] == 0xFFFFFFFF) {
            uint32_t wpis = zaalokuj_wolny_blok_danych();
            if(wpis == 0xFFFFFFFF) break; // Dysk operacyjny pełny
            wezel->wskazniki_blokow[k] = wpis;
        }

        uint8_t* ptr_bloku = pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
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
    uint32_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return false; 

    wezel_indeksowy* wezel = pobierz_wezel(plik_id);
    if (!wezel || wezel->typ != TYP_PLIK) return false;

    uint32_t przeczytano = 0;
    uint32_t dlugosc_pliku = wezel->rozmiar_w_bajtach;
    
    if(max_dlugosc < dlugosc_pliku) dlugosc_pliku = max_dlugosc;

    for (int k = 0; k < PSF_MAX_BLOKOW_W_WEZLE; k++) {
        if (przeczytano >= dlugosc_pliku) break;
        if (wezel->wskazniki_blokow[k] == 0xFFFFFFFF) break;

        uint8_t* ptr_bloku = pobierz_blok(dysk_superblok->start_danych + wezel->wskazniki_blokow[k]);
        uint32_t porcja = dlugosc_pliku - przeczytano;
        if(porcja > PSF_ROZMIAR_BLOKU) porcja = PSF_ROZMIAR_BLOKU;

        for(uint32_t j=0; j<porcja; j++) {
            bufor[przeczytano + j] = ptr_bloku[j];
        }

        przeczytano += porcja;
    }

    return true;
}
