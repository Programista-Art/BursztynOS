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
void log(const char* t) { bws_wywolaj(1, reinterpret_cast<uint64_t>(t), 0, 0, 0); }
uint64_t endpoint;
WidokMiniatur* galeria;
Etykieta* komunikat;
uint32_t tryb = 1;
int wybrana = 0;
void zastosuj(Przycisk*, void*) {
    TapetyZadanie req{}; req.operacja = TAPETY_ZASTOSUJ; req.tryb = tryb;
    tapety_kopiuj(req.sciezka, TAPETY_SCIEZKI[wybrana], sizeof(req.sciezka));
    TapetyOdpowiedz out{};
    if (!tapety_zapytaj(endpoint, req, &out) || out.status == TAPETY_BLAD)
        komunikat->ustaw_tekst("Nie można wczytać tapety. Pulpit zachował poprzedni obraz.");
    else if (out.status == TAPETY_BLAD_ZAPISU)
        komunikat->ustaw_tekst("Tapeta aktywna. Nie udało się zapisać wyboru na dysku.");
    else komunikat->ustaw_tekst("Tapeta zastosowana i zapisana.");
}
}
extern "C" __attribute__((noreturn)) void _start() {
    log("[TAPETY APP] Start.\n");
    AplikacjaGUI app;
    Okno okno("Tapety", 940, 610);
    MiniaturyTapet mini;
    endpoint = ipc_create();
    auto* root = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!root || !endpoint || !mini.przygotuj()) {
        log("[TAPETY APP] BLAD inicjalizacji galerii lub IPC.\n");
        gui_zakoncz_aplikacje();
    }
    root->ustaw_orientacje(Orientacja::Pionowa);
    root->ustaw_wypelnienie(Marginesy(12)); root->ustaw_odstep(8);
    root->dodaj<Etykieta>("Tapeta pulpitu");
    galeria = &root->dodaj<WidokMiniatur>();
    galeria->ustaw_rozmiar_elementu(172, 124);
    galeria->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    galeria->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    unsigned poprawne = 0;
    for (unsigned i = 0; i < TAPETY_LICZBA; ++i) {
        if (mini.wczytaj(i)) ++poprawne;
        galeria->dodaj_element(i, TAPETY_NAZWY[i], mini.obraz(i), TAPETY_MINI_W, TAPETY_MINI_H);
    }
    log(poprawne == 10 ? "[TAPETY APP] 10 miniatur gotowych.\n" : "[TAPETY APP] Niektore miniatury niedostepne.\n");
    TapetyZadanie req{}; req.operacja = TAPETY_STAN;
    TapetyOdpowiedz stan{};
    if (tapety_zapytaj(endpoint, req, &stan) && stan.status == TAPETY_OK) {
        tryb = stan.tryb <= 2 ? stan.tryb : 1;
        for (unsigned i = 0; i < TAPETY_LICZBA; ++i)
            if (tapety_rowne(stan.sciezka, TAPETY_SCIEZKI[i])) wybrana = i;
    }
    galeria->ustaw_wybrany_indeks(wybrana);
    galeria->przy_zmianie_zaznaczenia([](WidokMiniatur*, int i, uint64_t, void*) {
        if (i >= 0 && i < static_cast<int>(TAPETY_LICZBA)) {
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
    komunikat = &root->dodaj<Etykieta>("Wybierz miniaturę i tryb, następnie naciśnij Zastosuj.");
    (void)app.uruchom(okno);
    ipc_close(endpoint);
    bws_zakoncz_proces();
}
