#pragma once
/*
 * BWS 56 (popup aplikacji) - autoryzacja i dekodowanie argumentow.
 *
 * Czysta logika bez stanu, uzywana przez dyspozytor wywolan systemowych
 * i testy hosta (te same funkcje, bez osobnej kopii dla testow).
 *
 * arg1 != 0  - otworz/przesun popup, 0 - zamknij wlasny popup
 * arg2       - (x << 32) | y   (int32 w kazdej polowie)
 * arg3       - (szer << 32) | wys
 * arg4       - flagi; jedyna znana: BWS_POPUP_BEZ_WEJSCIA
 *
 * Zwykly (interaktywny) popup: PRAWO_GUI i fokus przy otwieraniu - bez zmian.
 * Popup bez wejscia (dymek paska zadan) nie przechwytuje myszy, wiec moze byc
 * wyswietlony bez fokusu - dlatego jest zarezerwowany WYLACZNIE dla
 * Menedzera Okien: PRAWO_SYSTEM_CONFIG + PZB <= PZB_ZAUFANE (jak
 * operacja_systemowa_dozwolona) ORAZ sciezka programu Menedzera Okien
 * (jak BWS 40 dla paska zadan). Same prawa systemowe nie wystarczaja -
 * Ustawienia, Ustawienia ekranu i Aktualizacje je maja. Sciezka pochodzi
 * z proces_t.sciezka_pliku, ktora ustawia loader z rzeczywiscie
 * wczytanego pliku (nie z manifestu, tytulu okna ani argumentow Ring 3);
 * oba pliki leza w chronionych lokalizacjach. Nieznane bity flag sa
 * odrzucane, nigdy ignorowane.
 */
#include <stddef.h>
#include <stdint.h>
#include "pzb.h"
#include "app_identity.h"

namespace PopupAutoryzacja {

inline constexpr uint64_t ZNANE_FLAGI = BWS_POPUP_BEZ_WEJSCIA;

inline constexpr const char* SCIEZKI_MENEDZERA_OKIEN[] = {
    "/menedzer_okien.bur",
    "/programy/menedzer_okien.cebula/menedzer_okien.bur",
};

enum class Decyzja : uint8_t {
    Dozwolone,
    BrakPrawaGui,
    NieznaneFlagi,
    BrakUprawnienSystemowych,
    NieMenedzerOkien,
    BrakFokusu,
};

// Tozsamosc wywolujacego - wylacznie pola prowadzone przez jadro.
struct Wywolujacy {
    uint64_t prawa = 0;              // znane prawa procesu
    uint8_t poziom = PZB_PIASKOWNICA; // poziom PZB procesu
    const char* sciezka = nullptr;   // proces_t.sciezka_pliku
    size_t pojemnosc_sciezki = 0;    // sizeof(proces_t.sciezka_pliku)
    bool ma_fokus = false;           // bws_gui_aktywny_pid() == pid
};

// Dokladna rownosc ograniczona pojemnoscia bufora; bufor bez NUL w zakresie
// albo z dowolnym dodatkowym znakiem po wzorcu nie jest rowny.
inline bool rowna_sciezka(const char* s, size_t pojemnosc, const char* wzor) {
    if (!s || !wzor) return false;
    size_t i = 0;
    for (; wzor[i] != '\0'; ++i) {
        if (i >= pojemnosc || s[i] != wzor[i]) return false;
    }
    return i < pojemnosc && s[i] == '\0';
}

inline bool menedzer_okien(const Wywolujacy& w) {
    for (const char* sciezka : SCIEZKI_MENEDZERA_OKIEN)
        if (rowna_sciezka(w.sciezka, w.pojemnosc_sciezki, sciezka)) return true;
    return false;
}

inline bool uprawnienia_systemowe(const Wywolujacy& w) {
    return (w.prawa & PRAWO_SYSTEM_CONFIG) == PRAWO_SYSTEM_CONFIG &&
           w.poziom <= PZB_MAX_POZIOM && w.poziom <= PZB_ZAUFANE;
}

inline Decyzja autoryzuj(const Wywolujacy& w, bool otwarty, uint64_t flagi) {
    if ((w.prawa & PRAWO_GUI) != PRAWO_GUI) return Decyzja::BrakPrawaGui;
    if (flagi & ~ZNANE_FLAGI) return Decyzja::NieznaneFlagi;
    if (flagi & BWS_POPUP_BEZ_WEJSCIA) {
        if (!uprawnienia_systemowe(w)) return Decyzja::BrakUprawnienSystemowych;
        if (!menedzer_okien(w)) return Decyzja::NieMenedzerOkien;
        return Decyzja::Dozwolone;
    }
    if (otwarty && !w.ma_fokus) return Decyzja::BrakFokusu;
    return Decyzja::Dozwolone;
}

inline int32_t gorne_i32(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v >> 32)); }
inline int32_t dolne_i32(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFFULL)); }

using UstawPopup = bool (*)(int pid, bool otwarty, int x, int y, int szer, int wys, bool bez_wejscia);

// Cala obsluga BWS 56: autoryzacja, dekodowanie i jedno wywolanie kompozytora
// (ktory sam waliduje rozmiar, polozenie i wlasnosc). Odmowa nie dotyka
// zadnego stanu.
inline uint64_t wykonaj(const Wywolujacy& w, int pid, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, UstawPopup ustaw) {
    const bool otwarty = arg1 != 0;
    if (!ustaw || autoryzuj(w, otwarty, arg4) != Decyzja::Dozwolone) return 0;
    return ustaw(pid, otwarty, gorne_i32(arg2), dolne_i32(arg2),
                 gorne_i32(arg3), dolne_i32(arg3),
                 (arg4 & BWS_POPUP_BEZ_WEJSCIA) != 0) ? 1ULL : 0ULL;
}

}  // namespace PopupAutoryzacja
