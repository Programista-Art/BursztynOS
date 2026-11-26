#pragma once
/*
 * Grajek - wspolny interfejs dekoderow i rozpoznawanie formatu.
 *
 * Dekoder zwraca przeplatane probki S16 w natywnej liczbie kanalow (1 albo 2) i natywnej czestotliwosci pliku;
 * konwersja do 48 kHz stereo nalezy do potoku (przetwarzanie.h). Kolejne formaty (FLAC, Ogg Vorbis) dodaje sie
 * przez nowa wartosc FormatAudio, nowa klase pochodna i wpis w utworz_dekoder() - bez zmian w silniku i UI.
 *
 * Metody wirtualne maja domyslne cialo (bez "= 0"): srodowisko Ring 3 nie dostarcza __cxa_pure_virtual.
 */
#include "wav.h"
#include "mp3_info.h"

namespace Grajek {

enum class FormatAudio : uint8_t { Nieznany = 0, WAV = 1, MP3 = 2 /* przyszlosc: FLAC = 3, OGG = 4 */ };

inline const char* nazwa_formatu(FormatAudio f) {
    switch (f) {
        case FormatAudio::WAV: return "WAV";
        case FormatAudio::MP3: return "MP3";
        default: return "?";
    }
}

// Rozszerzenia obslugiwane w wersji 1.0 (bez rozrozniania wielkosci liter).
inline FormatAudio format_z_rozszerzenia(const char* nazwa) {
    if (rozszerzenie(nazwa, "wav")) return FormatAudio::WAV;
    if (rozszerzenie(nazwa, "mp3")) return FormatAudio::MP3;
    return FormatAudio::Nieznany;
}

/*
 * Rozpoznanie po zawartosci (nie po rozszerzeniu): "RIFF....WAVE" albo tag ID3v2 / synchronizacja ramki MPEG.
 * Ostateczna walidacja MP3 (dwie spojne ramki) nastepuje w Mp3::analizuj.
 */
inline FormatAudio format_z_naglowka(const uint8_t* d, size_t n) {
    if (n >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' && d[9] == 'A' && d[10] == 'V' && d[11] == 'E')
        return FormatAudio::WAV;
    if (n >= 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') return FormatAudio::MP3;
    if (n >= 4) { Mp3::Ramka r; if (Mp3::parsuj_ramke(d, &r)) return FormatAudio::MP3; }
    return FormatAudio::Nieznany;
}

struct ParametryStrumienia {
    uint32_t czestotliwosc = 0;
    uint16_t kanaly = 0;
    uint64_t ramki = 0;            // 0 = nieznana dlugosc
    uint32_t przeplywnosc = 0;     // kbit/s (informacyjnie)
};

constexpr size_t MAKS_BLAD = 96;

class Dekoder {
public:
    virtual ~Dekoder() {}
    // Otwiera plik i sprawdza format; przy bledzie wypelnia blad() polskim komunikatem.
    virtual bool otworz(const DostepDoPlikow& dostep, const char* sciezka) { (void)dostep; (void)sciezka; return false; }
    // Dekoduje do `ramki` ramek. Zwraca liczbe ramek; 0 = koniec strumienia albo blad (rozroznia blad()).
    virtual uint32_t dekoduj(int16_t* wyjscie, uint32_t ramki) { (void)wyjscie; (void)ramki; return 0; }
    // Ustawia pozycje na ramke PCM (w natywnej czestotliwosci). false = przewijanie niemozliwe.
    virtual bool szukaj(uint64_t ramka) { (void)ramka; return false; }
    virtual FormatAudio format() const { return FormatAudio::Nieznany; }
    // Ograniczona praca w tle (silnik wola ja, gdy kolejka PCM jest pelna): MP3 - przyrostowy indeks ramek do
    // szybkiego, dokladnego przewijania. `budzet` = maks. liczba ramek MPEG na wywolanie. true = zostala praca.
    virtual bool praca_w_tle(uint32_t budzet) { (void)budzet; return false; }
    enum class Szukanie : uint8_t { Brak, Dokladne, Przyblizone };
    // Sposob ostatniego szukaj(): Dokladne = co do ramki PCM (WAV, MP3 z indeksem), Przyblizone = skok wg TOC Xing
    // albo przeplywnosci CBR (blad pozycji ograniczony dokladnoscia naglowka, zwykle < 1% czasu utworu).
    Szukanie ostatnie_szukanie() const { return szukanie_; }
    // Postep indeksu (ramki PCM objete indeksem) - diagnostyka.
    virtual uint64_t zaindeksowane_ramki() const { return 0; }
    virtual bool indeks_kompletny() const { return true; }

    const ParametryStrumienia& parametry() const { return p_; }
    uint64_t pozycja() const { return pozycja_; }
    bool ma_blad() const { return blad_[0] != '\0'; }
    const char* blad() const { return blad_; }

protected:
    void ustaw_blad(const char* tekst) { kopiuj_tekst(blad_, sizeof(blad_), tekst); }
    ParametryStrumienia p_{};
    uint64_t pozycja_ = 0;
    char blad_[MAKS_BLAD]{};
    Szukanie szukanie_ = Szukanie::Brak;
};

class DekoderWav : public Dekoder {
public:
    static constexpr uint32_t MAKS_RAMEK_NA_WYWOLANIE = 2048;

    bool otworz(const DostepDoPlikow& dostep, const char* sciezka) override {
        blad_[0] = '\0';
        pozycja_ = 0;
        if (!czytnik_.otworz(dostep, sciezka)) { ustaw_blad("Nie można otworzyć pliku."); return false; }
        const Wav::Blad b = Wav::parsuj(czytnik_, info_, nullptr);
        if (b != Wav::Blad::OK) { ustaw_blad(Wav::opis(b)); return false; }
        p_.czestotliwosc = info_.czestotliwosc;
        p_.kanaly = info_.kanaly;
        p_.ramki = info_.ramki;
        p_.przeplywnosc = uint32_t(uint64_t(info_.czestotliwosc) * info_.blok * 8ULL / 1000ULL);
        return czytnik_.ustaw_pozycje(info_.dane_offset);
    }

    uint32_t dekoduj(int16_t* wy, uint32_t ramki) override {
        if (ma_blad() || !wy || !ramki) return 0;
        if (pozycja_ >= info_.ramki) return 0;
        if (ramki > MAKS_RAMEK_NA_WYWOLANIE) ramki = MAKS_RAMEK_NA_WYWOLANIE;
        const uint64_t zostalo = info_.ramki - pozycja_;
        if (ramki > zostalo) ramki = uint32_t(zostalo);
        const uint32_t bajty = ramki * info_.blok;           // <= 2048 * 8
        const uint32_t got = czytnik_.czytaj(surowe_, bajty);
        const uint32_t pelne = got / info_.blok;
        if (pelne == 0) {
            if (czytnik_.blad()) ustaw_blad("Błąd odczytu pliku WAV.");
            pozycja_ = info_.ramki;
            return 0;
        }
        Wav::konwertuj(info_, surowe_, pelne, wy);
        pozycja_ += pelne;
        return pelne;
    }

    bool szukaj(uint64_t ramka) override {
        if (ma_blad()) return false;
        if (ramka > info_.ramki) ramka = info_.ramki;
        if (!czytnik_.ustaw_pozycje(info_.dane_offset + ramka * info_.blok)) return false;
        pozycja_ = ramka;
        szukanie_ = Szukanie::Dokladne;     // bezposrednie przesuniecie probki (O(1), jeden odczyt okna)
        return true;
    }
    FormatAudio format() const override { return FormatAudio::WAV; }
    const Wav::Info& info() const { return info_; }

private:
    Czytnik czytnik_;
    Wav::Info info_{};
    uint8_t surowe_[MAKS_RAMEK_NA_WYWOLANIE * 8]{};
};

// Fabryka zdefiniowana w dekoder_mp3.cpp (Ring 3) albo w testach hosta: zwraca nowy dekoder albo nullptr.
Dekoder* utworz_dekoder_mp3();

inline Dekoder* utworz_dekoder(FormatAudio f) {
    switch (f) {
        case FormatAudio::WAV: return new DekoderWav();
        case FormatAudio::MP3: return utworz_dekoder_mp3();
        default: return nullptr;
    }
}

} // namespace Grajek
