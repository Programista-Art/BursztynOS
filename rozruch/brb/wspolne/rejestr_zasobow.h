/*
 * BRB - Faza R2: rejestr wlasnosci zasobow ladowania (kazdy zapisywalny cel musi nalezec do przydzialu firmware).
 *
 * Rejestr przechowuje WYLACZNIE zakresy zwrocone przez AllocatePages (adres i liczba stron dokladnie tak, jak
 * firmware je przydzielilo - takze z dopelnieniem wyrownania), z rodzajem zasobu i stanem wlasnosci. Obraz BRB
 * (LoadedImage) i bufor zrodla sa rejestrowane jako zakresy CHRONIONE (nie do zwolnienia przez rejestr).
 * Kontrole: wyrownanie, zero stron, przepelnienie, nakladanie z kazdym zywym zakresem. Zwalnianie: odwrotna
 * kolejnosc, kazdy zakres najwyzej raz; blad FreePages = zakres porzucony i zaraportowany (ponowne zwolnienie
 * mogloby trafic w pamiec przydzielona komus innemu).
 */
#ifndef BRB_REJESTR_ZASOBOW_H
#define BRB_REJESTR_ZASOBOW_H

#include <stdint.h>

namespace brb {

enum class RodzajZasobu : uint8_t {
    Nieznany = 0,
    ObrazBrb,           // chroniony: obraz BRB zaladowany przez firmware
    BuforZrodla,        // chroniony w rejestrze R2: bufor pliku jadra (zwalnia go plik_jadra)
    PodkladJadra,       // obraz jadra + dopelnienie wyrownania (EfiLoaderData)
    TabliceStron,       // pula ramek tablic przejscia (EfiLoaderData)
    StosPrzejscia,      // stos przejscia (EfiLoaderData)
    KodPrzejscia,       // strona kodu przejscia (EfiLoaderCode; tresc w 4C)
    InformacjeRozruchu, // rezerwa na kontrakt i informacje rozruchu (wypelniane w 4B)
    TestowyStraznik,    // wylacznie warianty testowe: strona ze wzorcem straznika
};

constexpr uint32_t REJESTR_MAKS = 16;

struct WpisZasobu {
    uint64_t adres;
    uint64_t strony;
    RodzajZasobu rodzaj;
    bool chroniony;     // nie zwalniany przez rejestr
    bool wlasny;        // true dopoki BRB posiada przydzial
    bool porzucony;     // FreePages zwrocil blad
};

enum class BladRejestru : uint8_t {
    Brak = 0,
    Pelny,
    ZakresNiepoprawny,  // niewyrownany, zero stron, przepelnienie, powyzej dolnej polowy
    Nakladanie,         // nachodzi na zywy zakres (wlasny albo chroniony)
};

struct RejestrZasobow {
    uint32_t liczba;
    WpisZasobu wpisy[REJESTR_MAKS];
    uint32_t zwolnione;
    uint32_t bledy_zwalniania;
};

void rejestr_inicjalizuj(RejestrZasobow* r);
// Sprawdzenie bez dodawania (przed AllocatePages nie znamy adresu; po - weryfikujemy, co firmware zwrocilo).
BladRejestru rejestr_sprawdz(const RejestrZasobow& r, uint64_t adres, uint64_t strony, int32_t* kolizja);
BladRejestru rejestr_dodaj(RejestrZasobow* r, uint64_t adres, uint64_t strony, RodzajZasobu rodzaj, bool chroniony,
                           int32_t* kolizja);
// Czy [adres, adres+dlugosc) lezy w calosci w JEDNYM wlasnym zakresie danego rodzaju.
bool rejestr_posiada(const RejestrZasobow& r, uint64_t adres, uint64_t dlugosc, RodzajZasobu rodzaj);

// Zwolnienie wszystkich wlasnych, niechronionych zakresow w odwrotnej kolejnosci. zwolnij(ctx, adres, strony)
// zwraca true przy sukcesie. Wynik: liczba bledow (zakresy porzucone).
uint32_t rejestr_zwolnij_wszystko(RejestrZasobow* r, bool (*zwolnij)(void* ctx, uint64_t adres, uint64_t strony), void* ctx);
uint32_t rejestr_wlasne(const RejestrZasobow& r);   // liczba wciaz posiadanych niechronionych zakresow

const char* nazwa_zasobu(RodzajZasobu r);

}  // namespace brb

#endif
