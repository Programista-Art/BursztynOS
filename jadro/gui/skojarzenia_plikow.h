#pragma once
// Skojarzenia typow plikow aplikacji GUI (Ring 3): .txt -> Notatnik; .png .jpg .jpeg .bmp .btp -> Przegladarka
// Obrazow; .mp3 .wav -> Grajek: jedno centralne miejsce, uzywane przez otworz_plik_skojarzony
// (jadro/gui/bursztyn_gui.cpp) - Eksplorator Plikow, pulpit i inne aplikacje. Czysta funkcja (bez BWS), wiec testy
// hosta sprawdzaja to samo mapowanie co system.
//
// Uruchomienie idzie zwyklym, ogolnym mechanizmem BWS 10 (program + argument startowy): jadro waliduje sciezke
// programu i argumentu, PZB programu docelowego decyduje o odczycie pliku. Brak wyjatkow dla aplikacji w jadrze.

namespace SkojarzeniaPlikow {

constexpr const char* NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
constexpr const char* PRZEGLADARKA_OBRAZOW = "/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur";
constexpr const char* GRAJEK = "/programy/grajek.cebula/grajek.bur";   // = AppIdentity::MusicPlayer

inline bool konczy_sie_ci(const char* tekst, const char* sufiks) {
    if (!tekst || !sufiks) return false;
    unsigned dt = 0, ds = 0;
    while (tekst[dt]) ++dt;
    while (sufiks[ds]) ++ds;
    if (ds > dt) return false;
    for (unsigned i = 0; i < ds; ++i) {
        char a = tekst[dt - ds + i], b = sufiks[i];
        if (a >= 'A' && a <= 'Z') a = char(a + 32);
        if (b >= 'A' && b <= 'Z') b = char(b + 32);
        if (a != b) return false;
    }
    return true;
}

// Formaty obslugiwane przez Przegladarke Obrazow (programy/przegladarka-obrazow: BMP, PNG, JPEG, BTP).
inline bool obraz(const char* sciezka) {
    return konczy_sie_ci(sciezka, ".png") || konczy_sie_ci(sciezka, ".jpg") || konczy_sie_ci(sciezka, ".jpeg") ||
           konczy_sie_ci(sciezka, ".bmp") || konczy_sie_ci(sciezka, ".btp");
}

// Formaty Grajka (programy/grajek: WAV PCM, MP3). Rozszerzenie wybiera tylko aplikacje - o przyjeciu pliku
// decyduje dekoder Grajka na podstawie zawartosci.
inline bool muzyka(const char* sciezka) { return konczy_sie_ci(sciezka, ".mp3") || konczy_sie_ci(sciezka, ".wav"); }

constexpr unsigned MAKS_SCIEZKA = 511;   // jak dawniej w otworz_plik_skojarzony (argument startowy BWS)

// Program dla pliku albo nullptr (brak skojarzenia). Tylko sciezki bezwzgledne (< 511 B) z niepusta nazwa pliku.
inline const char* program_dla_pliku(const char* sciezka) {
    if (!sciezka || sciezka[0] != '/') return nullptr;
    unsigned n = 0;
    while (n < MAKS_SCIEZKA && sciezka[n]) ++n;
    if (n == MAKS_SCIEZKA || sciezka[n - 1] == '/') return nullptr;
    if (konczy_sie_ci(sciezka, ".txt")) return NOTATNIK;
    if (obraz(sciezka)) return PRZEGLADARKA_OBRAZOW;
    if (muzyka(sciezka)) return GRAJEK;
    return nullptr;
}

}  // namespace SkojarzeniaPlikow
