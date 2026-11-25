#pragma once
/*
 * Bursztyn OS - polityka wyboru wyjscia audio (czysta logika, testowana na hoscie).
 *
 * Jedno wyjscie PCM dla calego systemu, wybierane RAZ przy starcie, przed otwarciem jakiegokolwiek strumienia:
 *   1. dzialajace Intel HDA (inicjalizuj_hda() == true) - zachowuje dotychczasowe, przetestowane konfiguracje,
 *   2. w przeciwnym razie AC97, jesli jego inicjalizacja zakonczyla sie sukcesem,
 *   3. w przeciwnym razie brak urzadzenia (BWS 110 zwraca PCM_DEVICE).
 * Sama obecnosc urzadzenia na PCI nie jest dowodem dzialania: liczy sie wynik inicjalizacji.
 * Nie ma automatycznego przelaczania w trakcie pracy - zmiana wymaga restartu systemu.
 */
#include <stdint.h>

namespace WyjscieAudio {

enum class Urzadzenie : uint8_t { Brak = 0, Hda = 1, Ac97 = 2 };

struct Dostepnosc {
    bool hda_wykryte, hda_gotowe;     // gotowe = inicjalizacja zakonczona sukcesem
    bool ac97_wykryte, ac97_gotowe;
};

inline Urzadzenie wybierz(const Dostepnosc& d) {
    if (d.hda_gotowe) return Urzadzenie::Hda;
    if (d.ac97_gotowe) return Urzadzenie::Ac97;
    return Urzadzenie::Brak;
}

// AC97 jest inicjalizowane tylko wtedy, gdy HDA nie dziala (brak dotykania drugiego kontrolera bez potrzeby).
inline bool inicjalizowac_ac97(bool hda_gotowe, bool ac97_wykryte) { return !hda_gotowe && ac97_wykryte; }

inline const char* nazwa(Urzadzenie u) {
    switch (u) {
        case Urzadzenie::Hda: return "Intel HDA";
        case Urzadzenie::Ac97: return "Intel ICH AC97";
        default: return "brak";
    }
}

inline const char* opis_braku(const Dostepnosc& d) {
    if (!d.hda_wykryte && !d.ac97_wykryte) return "Nie wykryto kontrolera dzwieku HDA ani AC97.";
    if (d.hda_wykryte && !d.ac97_wykryte) return "Kontroler HDA wykryty, ale jego inicjalizacja nie powiodla sie.";
    if (!d.hda_wykryte) return "Kontroler AC97 wykryty, ale jego inicjalizacja nie powiodla sie.";
    return "Kontrolery HDA i AC97 wykryte, ale zaden nie zostal poprawnie zainicjalizowany.";
}

// Stan globalny: ustawiany jeden raz. Ponowny wybor jest odrzucany (brak przelaczenia w trakcie odtwarzania).
class Wybor {
public:
    bool ustaw(const Dostepnosc& d) {
        if (ustalony_) return false;
        dostepnosc_ = d;
        aktywne_ = wybierz(d);
        ustalony_ = true;
        return true;
    }
    Urzadzenie aktywne() const { return aktywne_; }
    bool ustalony() const { return ustalony_; }
    const Dostepnosc& dostepnosc() const { return dostepnosc_; }
private:
    Dostepnosc dostepnosc_{};
    Urzadzenie aktywne_ = Urzadzenie::Brak;
    bool ustalony_ = false;
};

} // namespace WyjscieAudio
