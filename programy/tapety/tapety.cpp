/* Tapety: pojedyncza instancja, miniatury i zatwierdzanie przez IPC. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x24000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x25000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1025000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

#include "klient.h"
#include "miniatury.h"

namespace {
constexpr uint32_t TAPETY_MAX = 64;
constexpr size_t KATALOG_BUFOR_MAX = 16384;

struct ElementTapety {
    char sciezka[TAPETY_SCIEZKA_MAX];
    char nazwa[TAPETY_SCIEZKA_MAX];
};

ElementTapety lista_tapet[TAPETY_MAX];
int liczba_tapet = 0;

void log(const char* t) { bws_wywolaj(1, reinterpret_cast<uint64_t>(t), 0, 0, 0); }
uint64_t endpoint;
WidokMiniatur* galeria;
Etykieta* komunikat;
uint32_t tryb = 1;
int wybrana = 0;

int porownaj_tekst(const char* a, const char* b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<int>(static_cast<uint8_t>(*a)) - static_cast<int>(static_cast<uint8_t>(*b));
}

void pobierz_nazwe_wyswietlana(const char* sciezka, const char* plik, char* dst, size_t cap) {
    if (!dst || !cap) return;
    dst[0] = '\0';
    for (size_t i = 0; i < TAPETY_LICZBA; ++i) {
        if (tapety_rowne(sciezka, TAPETY_SCIEZKI[i])) {
            tapety_kopiuj(dst, TAPETY_NAZWY[i], cap);
            return;
        }
    }
    size_t len = 0;
    while (plik[len]) ++len;
    if (len >= 4 && tapety_rowne(plik + len - 4, ".btp", 5)) {
        len -= 4;
    }
    size_t i = 0;
    for (; i < len && i + 1 < cap; ++i) dst[i] = plik[i];
    dst[i] = '\0';
}

void sortuj_tapety() {
    for (int i = 0; i < liczba_tapet - 1; ++i) {
        for (int j = 0; j < liczba_tapet - 1 - i; ++j) {
            if (porownaj_tekst(lista_tapet[j].sciezka, lista_tapet[j + 1].sciezka) > 0) {
                ElementTapety tmp = lista_tapet[j];
                lista_tapet[j] = lista_tapet[j + 1];
                lista_tapet[j + 1] = tmp;
            }
        }
    }
}

void odkryj_tapety() {
    liczba_tapet = 0;
    char* bufor = static_cast<char*>(gui_malloc(KATALOG_BUFOR_MAX));
    if (!bufor) return;
    for (size_t i = 0; i < KATALOG_BUFOR_MAX; ++i) bufor[i] = 0;

    if (!wylistuj_katalog_uzytkownika("/tapety", bufor, KATALOG_BUFOR_MAX)) {
        gui_free(bufor);
        return;
    }

    size_t pos = 0;
    while (pos < KATALOG_BUFOR_MAX && bufor[pos]) {
        size_t end = pos;
        while (end < KATALOG_BUFOR_MAX && bufor[end] && bufor[end] != '\n') ++end;
        if (end <= pos) break;

        if (end - pos > 7 && bufor[pos] == '[' && bufor[pos + 1] == 'P' && bufor[pos + 2] == 'L' &&
            bufor[pos + 3] == 'I' && bufor[pos + 4] == 'K' && bufor[pos + 5] == ']' && bufor[pos + 6] == ' ') {

            const char* nazwa_pliku = bufor + pos + 7;
            const size_t nazwa_len = end - (pos + 7);

            if (nazwa_len >= 5 && nazwa_len < TAPETY_SCIEZKA_MAX - 10) {
                char plik[TAPETY_SCIEZKA_MAX]{};
                for (size_t i = 0; i < nazwa_len; ++i) plik[i] = nazwa_pliku[i];
                plik[nazwa_len] = '\0';

                if (tapety_rowne(plik + nazwa_len - 4, ".btp", 5)) {
                    char pelna[TAPETY_SCIEZKA_MAX]{};
                    size_t p = 0;
                    constexpr char prefix[] = "/tapety/";
                    for (; prefix[p] && p + 1 < sizeof(pelna); ++p) pelna[p] = prefix[p];
                    for (size_t i = 0; plik[i] && p + 1 < sizeof(pelna); ++i) pelna[p++] = plik[i];
                    pelna[p] = '\0';

                    if (tapety_sciezka(pelna)) {
                        uint32_t rozmiar = 0;
                        if (pobierz_rozmiar_pliku(pelna, &rozmiar) && rozmiar >= 14 && rozmiar <= TAPETY_PLIK_MAX) {
                            uint8_t naglowek[14]{};
                            uint32_t przeczytano = 0;
                            if (czytaj_fragment_pliku(pelna, 0, reinterpret_cast<char*>(naglowek), 14, &przeczytano) &&
                                przeczytano == 14) {
                                BtpInfo info{};
                                if (btpnaglowek(naglowek, 14, &info) == BTPOK) {
                                    if (liczba_tapet < static_cast<int>(TAPETY_MAX)) {
                                        tapety_kopiuj(lista_tapet[liczba_tapet].sciezka, pelna, sizeof(lista_tapet[0].sciezka));
                                        pobierz_nazwe_wyswietlana(pelna, plik, lista_tapet[liczba_tapet].nazwa, sizeof(lista_tapet[0].nazwa));
                                        ++liczba_tapet;
                                    } else {
                                        log("[TAPETY APP] Osiagnieto limit 64 tapet; pomijam kolejne pliki.\n");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        pos = end;
        if (pos < KATALOG_BUFOR_MAX && bufor[pos] == '\n') ++pos;
    }

    gui_free(bufor);
    sortuj_tapety();
}

void zastosuj(Przycisk*, void*) {
    if (wybrana < 0 || wybrana >= liczba_tapet) return;
    TapetyZadanie req{}; req.operacja = TAPETY_ZASTOSUJ; req.tryb = tryb;
    tapety_kopiuj(req.sciezka, lista_tapet[wybrana].sciezka, sizeof(req.sciezka));
    TapetyOdpowiedz out{};
    if (!tapety_zapytaj(endpoint, req, &out) || out.status == TAPETY_BLAD)
        komunikat->ustaw_tekst("Nie można wczytać tapety. Pulpit zachował poprzedni obraz.");
    else if (out.status == TAPETY_BLAD_ZAPISU)
        komunikat->ustaw_tekst("Tapeta aktywna. Nie udało się zapisać wyboru na dysku.");
    else komunikat->ustaw_tekst("Tapeta zastosowana i zapisana.");
}
} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    log("[TAPETY APP] Start.\n");
    AplikacjaGUI app;
    Okno okno("Tapety", 940, 610);
    endpoint = ipc_create();
    auto* root = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!root || !endpoint) {
        log("[TAPETY APP] BLAD inicjalizacji okna lub IPC.\n");
        gui_zakoncz_aplikacje();
    }
    root->ustaw_orientacje(Orientacja::Pionowa);
    root->ustaw_wypelnienie(Marginesy(12)); root->ustaw_odstep(8);
    root->dodaj<Etykieta>("Tapeta pulpitu");
    galeria = &root->dodaj<WidokMiniatur>();
    galeria->ustaw_rozmiar_elementu(174, 124);
    galeria->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    galeria->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    odkryj_tapety();

    MiniaturyTapet mini;
    const uint32_t poj = liczba_tapet > 0 ? static_cast<uint32_t>(liczba_tapet) : 1U;
    if (!mini.przygotuj(poj)) {
        log("[TAPETY APP] BLAD alokacji bufora miniatur.\n");
        gui_zakoncz_aplikacje();
    }

    unsigned poprawne = 0;
    for (int i = 0; i < liczba_tapet; ++i) {
        if (mini.wczytaj(static_cast<unsigned>(i), lista_tapet[i].sciezka)) ++poprawne;
        galeria->dodaj_element(i, lista_tapet[i].nazwa, mini.obraz(static_cast<unsigned>(i)), TAPETY_MINI_W, TAPETY_MINI_H);
    }
    log(poprawne > 0 ? "[TAPETY APP] Miniatury gotowe.\n" : "[TAPETY APP] Brak poprawnych miniatur.\n");

    TapetyZadanie req{}; req.operacja = TAPETY_STAN;
    TapetyOdpowiedz stan{};
    wybrana = 0;
    if (tapety_zapytaj(endpoint, req, &stan) && stan.status == TAPETY_OK) {
        tryb = stan.tryb <= 2 ? stan.tryb : 1;
        for (int i = 0; i < liczba_tapet; ++i) {
            if (tapety_rowne(stan.sciezka, lista_tapet[i].sciezka)) {
                wybrana = i;
                break;
            }
        }
    }
    if (liczba_tapet > 0) {
        galeria->ustaw_wybrany_indeks(wybrana);
    }
    galeria->przy_zmianie_zaznaczenia([](WidokMiniatur*, int i, uint64_t, void*) {
        if (i >= 0 && i < liczba_tapet) {
            wybrana = i; komunikat->ustaw_tekst("Wybór oczekuje na Zastosuj.");
        }
    });
    auto& lista = root->dodaj<ListaRozwijana>();
    lista.dodaj_element("Dopasuj"); lista.dodaj_element("Wypełnij"); lista.dodaj_element("Wyśrodkuj");
    lista.ustaw_wybrany_indeks(tryb);
    lista.przy_zmianie_wyboru([](ListaRozwijana*, int i, void*) { if (i >= 0 && i <= 2) tryb = i; });
    auto& row = root->dodaj<Panel>(); row.ustaw_orientacje(Orientacja::Pozioma); row.ustaw_odstep(12);
    row.dodaj<Przycisk>("Zastosuj").przy_kliknieciu(zastosuj);
    row.dodaj<Przycisk>("Anuluj").przy_kliknieciu([](Przycisk*, void* p) { static_cast<Okno*>(p)->zamknij(); }, &okno);
    komunikat = &root->dodaj<Etykieta>(liczba_tapet > 0 ? "Wybierz miniaturę i tryb, następnie naciśnij Zastosuj." : "Brak dostępnych tapet w /tapety.");
    (void)app.uruchom(okno);
    ipc_close(endpoint);
    bws_zakoncz_proces();
}
